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
#include <QFile>
#include "view2d/KLayout2DView.h"

#ifdef HAVE_KLAYOUT
#include <QMenu>
#include "view2d/EmbeddedDispatcher.h"
#include "layAbstractMenu.h"
#endif

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

// -- Cell navigation API tests (safe in all environments) --

TEST_F(KLayout2DViewTest, CellNamesEmptyWhenNoLayout) {
    KLayout2DView view;
    EXPECT_TRUE(view.cellNames().isEmpty());
}

TEST_F(KLayout2DViewTest, CurrentCellNameEmptyWhenNoLayout) {
    KLayout2DView view;
    EXPECT_TRUE(view.currentCellName().isEmpty());
}

TEST_F(KLayout2DViewTest, SetCurrentCellReturnsFalseWhenNoLayout) {
    KLayout2DView view;
    EXPECT_FALSE(view.setCurrentCell("TOP"));
}

TEST_F(KLayout2DViewTest, HierarchyControlFrameNullWhenUnavailable) {
    KLayout2DView view;
    if (!view.isViewAvailable()) {
        EXPECT_EQ(view.hierarchyControlFrame(), nullptr);
    }
}

// -- Layer control API tests (safe in all environments) --

TEST_F(KLayout2DViewTest, LayerCountZeroWhenNoLayout) {
    KLayout2DView view;
    EXPECT_EQ(view.layerCount(), 0);
}

TEST_F(KLayout2DViewTest, SetAllLayersVisibleSafe) {
    KLayout2DView view;
    // Should not crash when no layout loaded
    view.setAllLayersVisible(true);
    view.setAllLayersVisible(false);
    SUCCEED();
}

TEST_F(KLayout2DViewTest, SetLayerVisibleSafe) {
    KLayout2DView view;
    // Should not crash even with invalid index
    view.setLayerVisible(0, true);
    view.setLayerVisible(99, false);
    SUCCEED();
}

TEST_F(KLayout2DViewTest, CellChangedSignalExists) {
    KLayout2DView view;
    QSignalSpy spy(&view, &KLayout2DView::cellChanged);
    ASSERT_TRUE(spy.isValid());
    // Signal should not fire without a layout
    EXPECT_EQ(spy.count(), 0);
}

TEST_F(KLayout2DViewTest, LayerInfosEmptyWhenNoLayout) {
    KLayout2DView view;
    QVector<LayerInfo> infos = view.layerInfos();
    EXPECT_TRUE(infos.isEmpty());
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

// Test loading a real GDS file (requires physical display - skip in CI/headless)
// Note: KLayout's GUI components crash with Xvfb, this test requires real X11
TEST_F(KLayout2DViewTest, LoadRealGdsIfDisplayAvailable) {
    KLayout2DView view;

    // Skip in headless/CI environments - KLayout GUI requires physical display
    // The DISPLAY check with QGuiApplication::screens() isn't sufficient
    // because KLayout crashes even with Xvfb virtual display
    if (!view.isViewAvailable()) {
        GTEST_SKIP() << "Physical display required (KLayout GUI test)";
    }

    // Try loading test fixture (FIXTURES_DIR defined via CMake)
    QString testFile = QString(FIXTURES_DIR) + "/sample_minimal.gds";
    bool result = view.loadLayout(testFile);

    // File should exist and load successfully with display available
    EXPECT_TRUE(result) << "Failed to load " << testFile.toStdString();
    EXPECT_TRUE(view.hasLayout());
    EXPECT_EQ(view.currentPath(), testFile);
}

// Regression test for the SIGSEGV when right-clicking KLayout's hierarchy/layer
// side panels. Root cause: a LayoutViewWidget driven by an EXTERNAL dispatcher
// never runs lay::LayoutViewBase::init_menu()/build() (that path only fires when
// dispatcher() == view). The detached context menus were therefore never built,
// so detached_menu("hcp_context_menu") returned a null QMenu and
// HierarchyControlPanel::context_menu() crashed in QMenu::exec().
// EmbeddedDispatcher must populate and build those detached menus itself.
TEST_F(KLayout2DViewTest, EmbeddedDispatcherBuildsContextMenus) {
    EmbeddedDispatcher dispatcher(nullptr);
    ASSERT_TRUE(dispatcher.isInitialized());

    lay::AbstractMenu* menu = dispatcher.menu();
    ASSERT_NE(menu, nullptr);

    // The detached context menus the side panels look up must exist.
    ASSERT_TRUE(menu->is_valid("@hcp_context_menu"));
    ASSERT_TRUE(menu->is_valid("@lcp_context_menu"));

    // They must resolve to a real (non-null) QMenu - the null deref here was the
    // crash. detached_menu() asserts the item exists, then returns its QMenu.
    QMenu* hcp = menu->detached_menu("hcp_context_menu");
    QMenu* lcp = menu->detached_menu("lcp_context_menu");
    ASSERT_NE(hcp, nullptr);
    ASSERT_NE(lcp, nullptr);

    // And the hierarchy context menu should carry the navigation actions
    // (e.g. "Show As New Top") contributed by the hierarchy panel plugin.
    EXPECT_GT(hcp->actions().size(), 0);
}

// Hierarchy depth controls must be safe no-ops when no layout/view is present.
TEST_F(KLayout2DViewTest, HierLevelControlsSafeWhenEmpty) {
    KLayout2DView view;
    // No view widget yet: getter reports "unknown", setter must not crash.
    EXPECT_EQ(view.maxHierLevels(), -1);
    view.setMaxHierLevels(5);
    view.setMaxHierLevels(-3);  // negative clamps internally, still safe
    EXPECT_FALSE(view.hasLayout());
}

#endif // HAVE_KLAYOUT

} // namespace chiplet
