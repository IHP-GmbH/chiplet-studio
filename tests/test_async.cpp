/**
 * test_async.cpp - Tests for async loading functionality
 *
 * Tests the asynchronous file loading and auto-save systems.
 */

#include <gtest/gtest.h>
#include <QApplication>
#include <QSignalSpy>
#include <QFuture>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>
#include <QTimer>
#include <QTest>
#include <thread>
#include <chrono>
#include <fstream>

#include "formats/ChipletFormat.h"
#include "core/Assembly.h"

// Test fixture for async tests
class AsyncLoadTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Ensure QApplication exists for Qt async operations
        if (!QCoreApplication::instance()) {
            static int argc = 1;
            static char* argv[] = { const_cast<char*>("test") };
            static QCoreApplication app(argc, argv);
        }
    }

    void TearDown() override {
    }

    std::string getFixturePath(const std::string& filename) const {
        return std::string(FIXTURES_DIR) + "/" + filename;
    }
};

// Structure to hold result - uses shared_ptr for QFuture compatibility
// (QFuture::result() requires copyable types)
struct AsyncLoadResult {
    std::shared_ptr<chiplet::Assembly> assembly;
    QString error;
    QString path;
};

// Test: Async loading with QtConcurrent::run
TEST_F(AsyncLoadTest, AsyncChipletLoadWithQtConcurrent) {
    QString path = QString::fromStdString(getFixturePath("minimal.chiplet"));

    // Create future watcher
    QFutureWatcher<AsyncLoadResult> watcher;
    bool finished = false;

    QObject::connect(&watcher, &QFutureWatcher<AsyncLoadResult>::finished, [&finished]() {
        finished = true;
    });

    // Start async load
    QFuture<AsyncLoadResult> future = QtConcurrent::run([path]() -> AsyncLoadResult {
        AsyncLoadResult result;
        try {
            chiplet::ChipletFormat format;
            auto assembly = format.load(path.toStdString());
            result.assembly = std::move(assembly);
        } catch (const std::exception& e) {
            result.error = QString::fromStdString(e.what());
        }
        return result;
    });

    watcher.setFuture(future);

    // Wait for completion (max 5 seconds)
    for (int i = 0; i < 50 && !finished; ++i) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    ASSERT_TRUE(finished) << "Async load did not complete in time";

    AsyncLoadResult result = watcher.result();
    EXPECT_TRUE(result.error.isEmpty()) << "Load error: " << result.error.toStdString();
    ASSERT_NE(result.assembly, nullptr) << "Assembly was not loaded";
    EXPECT_FALSE(result.assembly->name().empty()) << "Assembly name should not be empty";
}

// Test: Slow load simulation (verifies UI doesn't block)
TEST_F(AsyncLoadTest, SlowLoadSimulation) {
    // Simulate a slow load operation
    QFutureWatcher<int> watcher;
    bool finished = false;
    QElapsedTimer timer;

    QObject::connect(&watcher, &QFutureWatcher<int>::finished, [&finished]() {
        finished = true;
    });

    timer.start();

    // Start a "slow" background task (200ms delay)
    QFuture<int> future = QtConcurrent::run([]() -> int {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        return 42;
    });

    watcher.setFuture(future);

    // UI should remain responsive during this time
    int eventLoopIterations = 0;
    while (!finished && timer.elapsed() < 2000) {
        QCoreApplication::processEvents();
        eventLoopIterations++;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    ASSERT_TRUE(finished) << "Slow load did not complete";
    EXPECT_GT(eventLoopIterations, 5) << "Event loop should have run multiple times during slow load";
    EXPECT_EQ(watcher.result(), 42);
}

// Test: Load error handling
TEST_F(AsyncLoadTest, AsyncLoadErrorHandling) {
    QString path = "/nonexistent/file.chiplet";

    QFutureWatcher<AsyncLoadResult> watcher;
    bool finished = false;

    QObject::connect(&watcher, &QFutureWatcher<AsyncLoadResult>::finished, [&finished]() {
        finished = true;
    });

    QFuture<AsyncLoadResult> future = QtConcurrent::run([path]() -> AsyncLoadResult {
        AsyncLoadResult result;
        try {
            chiplet::ChipletFormat format;
            auto assembly = format.load(path.toStdString());
            result.assembly = std::move(assembly);
        } catch (const std::exception& e) {
            result.error = QString::fromStdString(e.what());
        }
        return result;
    });

    watcher.setFuture(future);

    // Wait for completion
    for (int i = 0; i < 50 && !finished; ++i) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    ASSERT_TRUE(finished) << "Async load did not complete in time";

    AsyncLoadResult result = watcher.result();
    EXPECT_FALSE(result.error.isEmpty()) << "Should have error for nonexistent file";
    EXPECT_EQ(result.assembly, nullptr) << "Assembly should be null on error";
}

// Test: ChipletFormat save/load roundtrip (for auto-save verification)
TEST_F(AsyncLoadTest, SaveLoadRoundtrip) {
    // Create a test assembly
    auto assembly = std::make_unique<chiplet::Assembly>();
    assembly->set_name("RoundtripTest");
    assembly->set_description("Test assembly for roundtrip");
    assembly->set_units("um");

    // Add a component
    auto comp = std::make_unique<chiplet::Component>("test_die", chiplet::ComponentType::Die);
    comp->set_position({100.0, 200.0, 0.0});
    comp->set_dimensions({1000.0, 1000.0, 50.0});
    assembly->add_component(std::move(comp));

    // Save to temp file
    std::string tempPath = "/tmp/test_roundtrip.chiplet";
    chiplet::ChipletFormat format;
    ASSERT_NO_THROW(format.save(*assembly, tempPath));

    // Load back
    std::unique_ptr<chiplet::Assembly> loaded;
    ASSERT_NO_THROW(loaded = format.load(tempPath));

    // Verify
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->name(), "RoundtripTest");
    EXPECT_EQ(loaded->description(), "Test assembly for roundtrip");
    EXPECT_EQ(loaded->components().size(), 1u);

    auto* loadedComp = loaded->component("test_die");
    ASSERT_NE(loadedComp, nullptr);
    EXPECT_EQ(loadedComp->type(), chiplet::ComponentType::Die);
    EXPECT_DOUBLE_EQ(loadedComp->position().x, 100.0);
    EXPECT_DOUBLE_EQ(loadedComp->position().y, 200.0);

    // Cleanup
    std::remove(tempPath.c_str());
}

// Test: Concurrent loads don't interfere
TEST_F(AsyncLoadTest, ConcurrentLoads) {
    QString path1 = QString::fromStdString(getFixturePath("minimal.chiplet"));
    QString path2 = QString::fromStdString(getFixturePath("with_components.chiplet"));

    QFutureWatcher<AsyncLoadResult> watcher1, watcher2;
    bool finished1 = false, finished2 = false;

    QObject::connect(&watcher1, &QFutureWatcher<AsyncLoadResult>::finished, [&finished1]() {
        finished1 = true;
    });
    QObject::connect(&watcher2, &QFutureWatcher<AsyncLoadResult>::finished, [&finished2]() {
        finished2 = true;
    });

    // Start two async loads
    QFuture<AsyncLoadResult> future1 = QtConcurrent::run([path1]() -> AsyncLoadResult {
        AsyncLoadResult result;
        result.path = path1;
        try {
            chiplet::ChipletFormat format;
            auto assembly = format.load(path1.toStdString());
            result.assembly = std::move(assembly);
        } catch (const std::exception& e) {
            result.error = QString::fromStdString(e.what());
        }
        return result;
    });

    QFuture<AsyncLoadResult> future2 = QtConcurrent::run([path2]() -> AsyncLoadResult {
        AsyncLoadResult result;
        result.path = path2;
        try {
            chiplet::ChipletFormat format;
            auto assembly = format.load(path2.toStdString());
            result.assembly = std::move(assembly);
        } catch (const std::exception& e) {
            result.error = QString::fromStdString(e.what());
        }
        return result;
    });

    watcher1.setFuture(future1);
    watcher2.setFuture(future2);

    // Wait for both to complete
    for (int i = 0; i < 100 && (!finished1 || !finished2); ++i) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    ASSERT_TRUE(finished1) << "First load did not complete";
    ASSERT_TRUE(finished2) << "Second load did not complete";

    AsyncLoadResult result1 = watcher1.result();
    AsyncLoadResult result2 = watcher2.result();

    EXPECT_TRUE(result1.error.isEmpty()) << "First load error: " << result1.error.toStdString();
    EXPECT_TRUE(result2.error.isEmpty()) << "Second load error: " << result2.error.toStdString();

    ASSERT_NE(result1.assembly, nullptr);
    ASSERT_NE(result2.assembly, nullptr);

    // Verify they loaded different assemblies
    EXPECT_NE(result1.assembly->name(), result2.assembly->name());
}
