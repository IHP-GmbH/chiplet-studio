// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * ScriptEngine.h - Embedded Python interpreter for chiplet-studio
 *
 * Manages Python scripting integration:
 * - Initializes embedded Python interpreter
 * - Sets up the chiplet_studio module
 * - Provides execution methods for scripts and REPL
 * - Redirects stdout/stderr for GUI console
 */

#ifndef CHIPLET_SCRIPTING_SCRIPT_ENGINE_H
#define CHIPLET_SCRIPTING_SCRIPT_ENGINE_H

#include <QObject>
#include <QString>
#include <memory>
#include <functional>

namespace chiplet {

class Assembly;
class CommandProcessor;

/**
 * ScriptEngine manages Python scripting in the application.
 *
 * Usage:
 *   ScriptEngine engine;
 *   if (engine.initialize()) {
 *       engine.setAssembly(&assembly, &processor);
 *       engine.execute("print('Hello from Python!')");
 *   }
 */
class ScriptEngine : public QObject {
    Q_OBJECT

public:
    explicit ScriptEngine(QObject* parent = nullptr);
    ~ScriptEngine() override;

    /**
     * Initialize the Python interpreter.
     * @return true if initialization succeeded
     */
    bool initialize();

    /**
     * Check if Python is initialized and ready.
     */
    bool is_initialized() const;

    /**
     * Shutdown the Python interpreter.
     * Called automatically on destruction.
     */
    void shutdown();

    /**
     * Set the assembly and command processor for scripts.
     * Scripts can access this assembly via get_current_assembly().
     * @param assembly The assembly to expose to scripts
     * @param processor The command processor for undoable operations
     */
    void set_assembly(Assembly* assembly, CommandProcessor* processor);

    /**
     * Clear the current assembly reference.
     */
    void clear_assembly();

    /**
     * Execute a Python script string.
     * @param code Python code to execute
     * @return true if execution succeeded
     */
    bool execute(const QString& code);

    /**
     * Execute a Python script file.
     * @param path Path to .py file
     * @return true if execution succeeded
     */
    bool execute_file(const QString& path);

    /**
     * Execute a single line in REPL mode.
     * Handles partial statements (e.g., for loops).
     * @param line Single line of Python code
     * @return true if line was executed (vs buffered for multiline)
     */
    bool execute_line(const QString& line);

    /**
     * Get the last error message.
     */
    QString last_error() const;

    /**
     * Check if Python scripting is available.
     * @return true if compiled with Python support
     */
    static bool is_available();

signals:
    /**
     * Emitted when Python prints to stdout.
     */
    void output(const QString& text);

    /**
     * Emitted when Python prints to stderr.
     */
    void error_output(const QString& text);

    /**
     * Emitted when script execution completes.
     * @param success true if script ran without errors
     */
    void execution_finished(bool success);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    void setup_stdout_redirect();
    void setup_move_callback();
};

} // namespace chiplet

#endif // CHIPLET_SCRIPTING_SCRIPT_ENGINE_H
