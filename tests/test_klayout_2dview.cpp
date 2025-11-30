/**
 * test_klayout_2dview.cpp - Unit tests for KLayout2DView widget
 *
 * These tests verify that KLayout2DView is robust in all environments:
 * - With KLayout and display available: full functionality
 * - With KLayout but no display: graceful degradation
 * - Without KLayout: stub mode with safe no-op operations
 */

#include <gtest/gtest.h>
#include <QApplication>
#include <QSignalSpy>
#include "view2d/KLayout2DView.h"

namespace chiplet {

// Fixture for KLayout2DView tests
class KLayout2DViewTest : public ::testing::Test {
protected:
    static int g_argc;
    static char* g_argv[];
    static QApplication* g_app;

    static void SetUpTestSuite() {
        if (!QApplication::instance()) {
            g_app = new QApplication(g_argc, g_argv);
        }
    }
};

int KLayout2DViewTest::g_argc = 0;
char* KLayout2DViewTest::g_argv[] = {nullptr};
QApplication* KLayout2DViewTest::g_app = nullptr;

// Test that the header compiles and types are correct
TEST(KLayout2DViewAPITest, TypeExists) {
    static_assert(std::is_base_of<QWidget, KLayout2DView>::value,
                  "KLayout2DView must inherit from QWidget");
}

// Construction should never crash (lazy init)
TEST_F(KLayout2DViewTest, ConstructionNeverCrashes) {
    // This should not crash even in headless mode
    KLayout2DView view;
    // Just verify the widget exists
    EXPECT_TRUE(view.inherits("QWidget"));
}

// Initial state should be empty
TEST_F(KLayout2DViewTest, InitialStateEmpty) {
    KLayout2DView view;
    EXPECT_FALSE(view.hasLayout());
    EXPECT_TRUE(view.currentPath().isEmpty());
}

// clearLayout should be safe even when no layout loaded
TEST_F(KLayout2DViewTest, ClearLayoutSafeWhenEmpty) {
    KLayout2DView view;
    // Should not crash
    view.clearLayout();
    EXPECT_FALSE(view.hasLayout());
}

// zoomFit should be safe even when no layout loaded
TEST_F(KLayout2DViewTest, ZoomFitSafeWhenEmpty) {
    KLayout2DView view;
    // Should not crash
    view.zoomFit();
}

// layoutChanged signal should be emitted on clear
TEST_F(KLayout2DViewTest, LayoutChangedSignalOnClear) {
    KLayout2DView view;
    QSignalSpy spy(&view, &KLayout2DView::layoutChanged);
    ASSERT_TRUE(spy.isValid());

    view.clearLayout();
    EXPECT_EQ(spy.count(), 1);
    EXPECT_FALSE(spy.at(0).at(0).toBool());  // hasLayout = false
}

// Test loadLayout behavior (depends on environment)
TEST_F(KLayout2DViewTest, LoadLayoutBehavior) {
    KLayout2DView view;

    // In headless mode or without KLayout, load should fail gracefully
    bool result = view.loadLayout("nonexistent.gds");

    // Should not crash and should return false (either no KLayout or no display)
    if (!view.isViewAvailable()) {
        EXPECT_FALSE(result);
        EXPECT_FALSE(view.hasLayout());
    }
    // If display is available, result depends on whether file exists
}

// Test that layerControlFrame returns nullptr when view not available
TEST_F(KLayout2DViewTest, LayerControlFrameWhenUnavailable) {
    KLayout2DView view;

    // In headless mode, should return nullptr
    if (!view.isViewAvailable()) {
        EXPECT_EQ(view.layerControlFrame(), nullptr);
    }
}

// Test multiple operations don't crash
TEST_F(KLayout2DViewTest, MultipleOperationsSafe) {
    KLayout2DView view;

    // Perform multiple operations - none should crash
    view.clearLayout();
    view.zoomFit();
    view.loadLayout("test.gds");
    view.clearLayout();
    view.zoomFit();
    view.hasLayout();
    view.currentPath();
    view.layerControlFrame();

    // If we get here without crashing, test passes
    SUCCEED();
}

// Test isViewAvailable is consistent
TEST_F(KLayout2DViewTest, IsViewAvailableConsistent) {
    KLayout2DView view;

    bool first = view.isViewAvailable();
    bool second = view.isViewAvailable();
    bool third = view.isViewAvailable();

    // Should be consistent
    EXPECT_EQ(first, second);
    EXPECT_EQ(second, third);
}

#ifdef HAVE_KLAYOUT
// KLayout-specific tests that verify correct behavior with KLayout available
// (but may still be in headless mode)

TEST_F(KLayout2DViewTest, KLayoutAvailableCompiles) {
    // This test verifies KLayout is linked correctly
    KLayout2DView view;
    // isViewAvailable depends on display, not KLayout being linked
    SUCCEED();
}

// Test loading a real GDS file (if display available and file exists)
TEST_F(KLayout2DViewTest, LoadRealGdsIfDisplayAvailable) {
    KLayout2DView view;

    if (!view.isViewAvailable()) {
        // Skip test in headless mode
        GTEST_SKIP() << "Display not available for this test";
    }

    // Try loading test fixture
    QString testFile = "fixtures/test.gds";
    bool result = view.loadLayout(testFile);

    // Result depends on whether file exists
    if (result) {
        EXPECT_TRUE(view.hasLayout());
        EXPECT_EQ(view.currentPath(), testFile);
    }
}

#endif // HAVE_KLAYOUT

} // namespace chiplet
