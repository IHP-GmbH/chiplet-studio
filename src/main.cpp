/**
 * Chiplet Studio - 3D Chiplet Assembly Design Tool
 *
 * Main entry point.
 */

// Include Python headers BEFORE Qt to avoid slots macro conflict
#ifdef HAVE_PYTHON
// Must undef Qt slots macro before Python headers to avoid conflict
// with PyType_Slot structure in Python.h
#ifdef slots
#undef slots
#endif
#include <pybind11/embed.h>
namespace py = pybind11;
#endif

#include <QApplication>
#include <QStandardPaths>
#include <QTimer>
#include <QFile>
#include "ui/MainWindow.h"
#include "ui/CrashRecoveryDialog.h"
#include "core/CommandFactory.h"
#include "core/CommandJournal.h"
#include "core/Assembly.h"

#ifdef HAVE_PYTHON
// Global Python interpreter guard - must be created before Qt and
// destroyed after Qt to avoid segfault in Docker environments
static std::unique_ptr<py::scoped_interpreter> g_pythonInterpreter;

bool initPythonEarly()
{
    try {
        g_pythonInterpreter = std::make_unique<py::scoped_interpreter>();
        return true;
    } catch (const std::exception& e) {
        qWarning("Failed to initialize Python: %s", e.what());
        return false;
    }
}
#endif

int main(int argc, char *argv[])
{
#ifdef HAVE_PYTHON
    // Initialize Python BEFORE Qt to avoid initialization conflicts
    // The CHIPLET_NO_PYTHON env var can disable this for Docker environments
    // where the embedded interpreter crashes
    if (!qEnvironmentVariableIsSet("CHIPLET_NO_PYTHON")) {
        initPythonEarly();
    }
#endif

    QApplication app(argc, argv);
    app.setApplicationName("Chiplet Studio");
    app.setApplicationVersion("0.1.0");

    // Register built-in command types for deserialization
    chiplet::CommandFactory::register_builtin_commands();

    // Check for crash recovery
    std::filesystem::path appDataDir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).toStdString();

    // Create app data directory if it doesn't exist
    if (!std::filesystem::exists(appDataDir)) {
        std::filesystem::create_directories(appDataDir);
    }

    // Check for orphaned journal and offer recovery
    std::unique_ptr<chiplet::Assembly> recoveredAssembly;

    if (chiplet::CommandJournal::has_orphaned_journal(appDataDir)) {
        // Create an assembly for potential recovery
        recoveredAssembly = std::make_unique<chiplet::Assembly>();
        recoveredAssembly->set_name("Recovered Session");

        auto result = chiplet::CrashRecoveryDialog::check_and_recover(
            appDataDir, recoveredAssembly.get());

        if (result != chiplet::CrashRecoveryDialog::Result::Recovered) {
            // User discarded or cancelled - don't use the assembly
            recoveredAssembly.reset();
        }
    }

    chiplet::MainWindow window;

    // If we recovered an assembly, set it in the window
    if (recoveredAssembly) {
        window.setRecoveredAssembly(std::move(recoveredAssembly));
    }

    window.show();

    // Open file passed as command-line argument (e.g., ./chiplet-studio file.chiplet)
    if (argc > 1) {
        QString filePath = QString::fromLocal8Bit(argv[1]);
        if (QFile::exists(filePath)) {
            // Use a single-shot timer to open after the event loop starts
            QTimer::singleShot(0, &window, [&window, filePath]() {
                window.openFile(filePath);
            });
        }
    }

    return app.exec();
}
