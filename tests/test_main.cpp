/**
 * test_main.cpp - Custom test main with early Python initialization
 *
 * This file provides a custom main() for the test suite that initializes
 * Python BEFORE Qt to avoid segfaults in Docker environments.
 * Same pattern as src/main.cpp but for tests.
 */

// Include Python headers BEFORE Qt to avoid slots macro conflict
#ifdef HAVE_PYTHON
#ifdef slots
#undef slots
#endif
#include <pybind11/embed.h>
namespace py = pybind11;
#endif

#include <gtest/gtest.h>
#include <QApplication>
#include <memory>

#ifdef HAVE_PYTHON
// Global Python interpreter guard - must be created before Qt
// and destroyed after Qt to avoid segfault in Docker environments
static std::unique_ptr<py::scoped_interpreter> g_pythonInterpreter;

bool initPythonEarly()
{
    try {
        g_pythonInterpreter = std::make_unique<py::scoped_interpreter>();
        return true;
    } catch (const std::exception& e) {
        fprintf(stderr, "Failed to initialize Python: %s\n", e.what());
        return false;
    }
}
#endif

int main(int argc, char **argv)
{
#ifdef HAVE_PYTHON
    // Initialize Python BEFORE Qt to avoid initialization conflicts
    // The CHIPLET_NO_PYTHON env var can disable this for Docker environments
    // where the embedded interpreter crashes
    if (!qEnvironmentVariableIsSet("CHIPLET_NO_PYTHON")) {
        initPythonEarly();
    }
#endif

    // Initialize Qt (QApplication needed for tests that use QWidget)
    QApplication app(argc, argv);

    // Initialize Google Test
    ::testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}
