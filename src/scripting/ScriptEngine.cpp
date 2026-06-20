// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * ScriptEngine.cpp - Embedded Python interpreter implementation
 *
 * IMPORTANT: Python interpreter lifecycle notes:
 * - The Python interpreter can only be initialized ONCE per process
 * - Calling Py_Finalize() and then Py_Initialize() again is undefined behavior
 * - We use a singleton pattern to ensure the interpreter is created once and lives
 *   for the entire process lifetime
 */

#include "ScriptEngine.h"

// Must undef Qt slots macro before Python headers to avoid conflict
// with PyType_Slot structure in Python.h
#ifdef slots
#undef slots
#endif

#ifdef HAVE_PYTHON
#include <Python.h>  // For Py_IsInitialized()
#include <pybind11/embed.h>
#include <pybind11/stl.h>
namespace py = pybind11;
#endif

#include "core/Assembly.h"
#include "core/CommandProcessor.h"
#include <QDebug>
#include <mutex>

namespace chiplet {

// Global assembly pointer for Python script access
// Declared extern in PyBindings.cpp to enable get_current_assembly()
Assembly* g_scriptAssembly = nullptr;

#ifdef HAVE_PYTHON
// Singleton Python interpreter management
// The interpreter may be created by main() before Qt starts, or here on first use.
// Either way, it's NEVER destroyed (recreating Python interpreter in same process is UB)
class PythonInterpreter {
public:
    static PythonInterpreter& instance() {
        static PythonInterpreter inst;
        return inst;
    }

    bool is_initialized() const { return m_initialized; }

    py::module_& main_module() { return m_mainModule; }
    py::dict& main_namespace() { return m_mainNamespace; }

private:
    PythonInterpreter() {
        try {
            // Check if Python was already initialized by main() (early init for Docker compatibility)
            if (Py_IsInitialized()) {
                qDebug() << "Python interpreter already initialized (early init)";
                // Just get the main module - interpreter already running
                m_mainModule = py::module_::import("__main__");
                m_mainNamespace = m_mainModule.attr("__dict__").cast<py::dict>();
                m_initialized = true;
                m_ownsInterpreter = false;
            } else {
                // Initialize Python interpreter (happens exactly once)
                m_guard = std::make_unique<py::scoped_interpreter>();
                m_mainModule = py::module_::import("__main__");
                m_mainNamespace = m_mainModule.attr("__dict__").cast<py::dict>();
                m_initialized = true;
                m_ownsInterpreter = true;
                qDebug() << "Python interpreter initialized (singleton)";
            }
        } catch (const std::exception& e) {
            qWarning() << "Failed to initialize Python interpreter:" << e.what();
            m_initialized = false;
        }
    }

    // Non-copyable, non-movable
    PythonInterpreter(const PythonInterpreter&) = delete;
    PythonInterpreter& operator=(const PythonInterpreter&) = delete;

    // NOTE: Destructor intentionally does NOT destroy the interpreter
    // The py::scoped_interpreter will be destroyed at process exit
    ~PythonInterpreter() = default;

    std::unique_ptr<py::scoped_interpreter> m_guard;  // Only used if we created the interpreter
    py::module_ m_mainModule;
    py::dict m_mainNamespace;
    bool m_initialized = false;
    bool m_ownsInterpreter = false;
};
#endif

struct ScriptEngine::Impl {
#ifdef HAVE_PYTHON
    bool engineInitialized = false;  // Per-instance state (stdout redirect etc)
    py::object stdoutRedirect;
    py::object stderrRedirect;
#endif
    Assembly* assembly = nullptr;
    CommandProcessor* processor = nullptr;
    QString lastError;
    QString multilineBuffer;
    ScriptEngine* engine = nullptr;
};

ScriptEngine::ScriptEngine(QObject* parent)
    : QObject(parent)
    , m_impl(std::make_unique<Impl>())
{
    m_impl->engine = this;
}

ScriptEngine::~ScriptEngine()
{
    shutdown();
}

bool ScriptEngine::is_available()
{
#ifdef HAVE_PYTHON
    // Allow disabling Python at runtime (useful for Docker environments
    // where pybind11 segfaults during interpreter initialization)
    if (qEnvironmentVariableIsSet("CHIPLET_NO_PYTHON")) {
        return false;
    }
    return true;
#else
    return false;
#endif
}

bool ScriptEngine::initialize()
{
#ifdef HAVE_PYTHON
    if (m_impl->engineInitialized) {
        return true;  // Already set up for this instance
    }

    // Access singleton interpreter (creates it on first call)
    auto& interp = PythonInterpreter::instance();
    if (!interp.is_initialized()) {
        m_impl->lastError = "Python interpreter failed to initialize";
        return false;
    }

    try {
        // Add the Python module directory to sys.path if known at compile time
#ifdef PYTHON_MODULE_DIR
        try {
            py::exec("import sys; sys.path.insert(0, '" PYTHON_MODULE_DIR "')",
                      interp.main_namespace());
        } catch (...) {
            // Non-fatal: path insertion failed
        }
#endif

        // Import chiplet_studio module (optional, may fail if not in path)
        try {
            py::exec("import chiplet_studio as cs", interp.main_namespace());
        } catch (const py::error_already_set& e) {
            // Module might not be in path - that's OK for embedded use
            qWarning() << "chiplet_studio module not in Python path:" << e.what();
        }

        // Set up stdout/stderr redirection for this engine instance
        setup_stdout_redirect();

        m_impl->engineInitialized = true;
        qDebug() << "ScriptEngine initialized (using singleton interpreter)";
        return true;

    } catch (const std::exception& e) {
        m_impl->lastError = QString("Failed to initialize ScriptEngine: %1").arg(e.what());
        qWarning() << m_impl->lastError;
        return false;
    }
#else
    m_impl->lastError = "Python support not compiled in";
    return false;
#endif
}

bool ScriptEngine::is_initialized() const
{
#ifdef HAVE_PYTHON
    return m_impl->engineInitialized && PythonInterpreter::instance().is_initialized();
#else
    return false;
#endif
}

void ScriptEngine::shutdown()
{
#ifdef HAVE_PYTHON
    if (m_impl->engineInitialized) {
        // Clear this instance's context (but NOT the interpreter)
        if (m_impl->assembly == g_scriptAssembly) {
            g_scriptAssembly = nullptr;
        }

        // Restore sys.stdout/stderr to the originals BEFORE dropping our redirect
        // objects. The redirects wrap a py::cpp_function capturing `this`; since
        // the singleton interpreter outlives this ScriptEngine, leaving them
        // installed means any later print() (incl. interpreter shutdown / another
        // engine) calls a dangling callback. Only restore if sys.stdout/stderr
        // still point to OUR redirect (a newer engine may have replaced them).
        try {
            py::gil_scoped_acquire gil;
            py::module_ sys = py::module_::import("sys");
            py::object curOut = sys.attr("stdout");
            py::object curErr = sys.attr("stderr");
            if (!m_impl->stdoutRedirect.is_none() &&
                curOut.is(m_impl->stdoutRedirect)) {
                sys.attr("stdout") = sys.attr("__stdout__");
            }
            if (!m_impl->stderrRedirect.is_none() &&
                curErr.is(m_impl->stderrRedirect)) {
                sys.attr("stderr") = sys.attr("__stderr__");
            }
        } catch (const std::exception& e) {
            qWarning() << "Failed to restore sys.stdout/stderr:" << e.what();
        }

        // Clear Python objects held by this instance
        m_impl->stdoutRedirect = py::object();
        m_impl->stderrRedirect = py::object();

        // Mark this instance as shutdown (interpreter stays alive)
        m_impl->engineInitialized = false;
        m_impl->assembly = nullptr;
        m_impl->processor = nullptr;
        qDebug() << "ScriptEngine shutdown (interpreter remains active)";
    }
#endif
}

void ScriptEngine::set_assembly(Assembly* assembly, CommandProcessor* processor)
{
    m_impl->assembly = assembly;
    m_impl->processor = processor;

#ifdef HAVE_PYTHON
    if (m_impl->engineInitialized) {
        g_scriptAssembly = assembly;
    }
#endif
}

void ScriptEngine::clear_assembly()
{
    m_impl->assembly = nullptr;
    m_impl->processor = nullptr;

#ifdef HAVE_PYTHON
    g_scriptAssembly = nullptr;
#endif
}

bool ScriptEngine::execute(const QString& code)
{
#ifdef HAVE_PYTHON
    if (!m_impl->engineInitialized) {
        m_impl->lastError = "Python not initialized";
        emit execution_finished(false);
        return false;
    }

    try {
        auto& interp = PythonInterpreter::instance();
        py::exec(code.toStdString(), interp.main_namespace());
        emit execution_finished(true);
        return true;
    } catch (const py::error_already_set& e) {
        m_impl->lastError = QString::fromStdString(e.what());
        emit error_output(m_impl->lastError);
        emit execution_finished(false);
        return false;
    } catch (const std::exception& e) {
        m_impl->lastError = QString("Execution error: %1").arg(e.what());
        emit error_output(m_impl->lastError);
        emit execution_finished(false);
        return false;
    }
#else
    m_impl->lastError = "Python support not compiled in";
    emit execution_finished(false);
    return false;
#endif
}

bool ScriptEngine::execute_file(const QString& path)
{
#ifdef HAVE_PYTHON
    if (!m_impl->engineInitialized) {
        m_impl->lastError = "Python not initialized";
        emit execution_finished(false);
        return false;
    }

    try {
        auto& interp = PythonInterpreter::instance();
        py::eval_file(path.toStdString(), interp.main_namespace());
        emit execution_finished(true);
        return true;
    } catch (const py::error_already_set& e) {
        m_impl->lastError = QString::fromStdString(e.what());
        emit error_output(m_impl->lastError);
        emit execution_finished(false);
        return false;
    } catch (const std::exception& e) {
        m_impl->lastError = QString("File execution error: %1").arg(e.what());
        emit error_output(m_impl->lastError);
        emit execution_finished(false);
        return false;
    }
#else
    (void)path;
    m_impl->lastError = "Python support not compiled in";
    emit execution_finished(false);
    return false;
#endif
}

bool ScriptEngine::execute_line(const QString& line)
{
#ifdef HAVE_PYTHON
    if (!m_impl->engineInitialized) {
        m_impl->lastError = "Python not initialized";
        return false;
    }

    // Append to multiline buffer
    if (!m_impl->multilineBuffer.isEmpty()) {
        m_impl->multilineBuffer += "\n" + line;
    } else {
        m_impl->multilineBuffer = line;
    }

    // Try to compile to check if complete
    try {
        std::string code = m_impl->multilineBuffer.toStdString();

        // Use compile to check if the code is complete
        py::object builtins = py::module_::import("builtins");
        py::object compile_func = builtins.attr("compile");

        try {
            compile_func(code, "<input>", "exec");
        } catch (const py::error_already_set& e) {
            std::string errType = py::str(e.type().attr("__name__"));
            if (errType == "SyntaxError") {
                std::string errMsg = py::str(e.value());
                // Check if it's an incomplete statement
                if (errMsg.find("unexpected EOF") != std::string::npos ||
                    errMsg.find("expected an indented block") != std::string::npos) {
                    // Need more input
                    return false;
                }
            }
            // Other error - will be raised during exec
        }

        // Code is complete, execute it
        auto& interp = PythonInterpreter::instance();
        py::exec(code, interp.main_namespace());
        m_impl->multilineBuffer.clear();
        emit execution_finished(true);
        return true;

    } catch (const py::error_already_set& e) {
        m_impl->multilineBuffer.clear();
        m_impl->lastError = QString::fromStdString(e.what());
        emit error_output(m_impl->lastError);
        emit execution_finished(false);
        return true;  // Line was processed (with error)
    } catch (const std::exception& e) {
        m_impl->multilineBuffer.clear();
        m_impl->lastError = QString("Error: %1").arg(e.what());
        emit error_output(m_impl->lastError);
        emit execution_finished(false);
        return true;  // Line was processed (with error)
    }
#else
    (void)line;
    return false;
#endif
}

QString ScriptEngine::last_error() const
{
    return m_impl->lastError;
}

void ScriptEngine::setup_stdout_redirect()
{
#ifdef HAVE_PYTHON
    if (!m_impl->engineInitialized) return;

    try {
        auto& interp = PythonInterpreter::instance();

        // Create a class to capture stdout/stderr
        std::string redirect_code = R"(
import sys

class OutputRedirect:
    def __init__(self, callback):
        self.callback = callback
        self.buffer = ""

    def write(self, text):
        self.buffer += text
        if '\n' in self.buffer:
            lines = self.buffer.split('\n')
            for line in lines[:-1]:
                self.callback(line)
            self.buffer = lines[-1]

    def flush(self):
        if self.buffer:
            self.callback(self.buffer)
            self.buffer = ""
)";
        py::exec(redirect_code, interp.main_namespace());

        // Create C++ callback lambdas
        auto stdout_callback = [this](const std::string& text) {
            emit output(QString::fromStdString(text));
        };
        auto stderr_callback = [this](const std::string& text) {
            emit error_output(QString::fromStdString(text));
        };

        // Set up the redirects
        py::object OutputRedirect = interp.main_namespace()["OutputRedirect"];
        m_impl->stdoutRedirect = OutputRedirect(py::cpp_function(stdout_callback));
        m_impl->stderrRedirect = OutputRedirect(py::cpp_function(stderr_callback));

        py::module_::import("sys").attr("stdout") = m_impl->stdoutRedirect;
        py::module_::import("sys").attr("stderr") = m_impl->stderrRedirect;

    } catch (const std::exception& e) {
        qWarning() << "Failed to set up stdout redirect:" << e.what();
    }
#endif
}

} // namespace chiplet
