/**
 * test_drilldown_panel.cpp - Unit tests for DrillDownPanel widget
 *
 * All tests are headless-safe (no display required).
 */

#include <gtest/gtest.h>
#include <QApplication>
#include <QSignalSpy>
#include <QPushButton>
#include <QComboBox>
#include <QToolButton>
#include "ui/DrillDownPanel.h"
#include "view2d/KLayout2DView.h"

namespace chiplet {

class DrillDownPanelTest : public ::testing::Test {
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

int DrillDownPanelTest::g_argc = 0;
char* DrillDownPanelTest::g_argv[] = {nullptr};
QApplication* DrillDownPanelTest::g_app = nullptr;

TEST_F(DrillDownPanelTest, ConstructionNeverCrashes) {
    DrillDownPanel panel;
    EXPECT_NE(panel.view2d(), nullptr);
}

TEST_F(DrillDownPanelTest, View2dIsKLayout2DView) {
    DrillDownPanel panel;
    KLayout2DView* view = panel.view2d();
    ASSERT_NE(view, nullptr);
    EXPECT_TRUE(view->inherits("QWidget"));
}

TEST_F(DrillDownPanelTest, SetAndClearContext) {
    DrillDownPanel panel;
    // Should not crash
    panel.setContext("comp_1", "CPU Die", "IHP SG13G2");
    panel.clearContext();
}

TEST_F(DrillDownPanelTest, SetContextMultipleTimes) {
    DrillDownPanel panel;
    panel.setContext("comp_1", "CPU Die", "IHP SG13G2");
    panel.setContext("comp_2", "Interposer", "");
    panel.setContext("comp_3", "Memory", "TSMC 28nm");
    panel.clearContext();
    SUCCEED();
}

TEST_F(DrillDownPanelTest, BackSignalEmitted) {
    DrillDownPanel panel;
    QSignalSpy spy(&panel, &DrillDownPanel::backRequested);
    ASSERT_TRUE(spy.isValid());

    // Find the back button and click it
    QPushButton* backBtn = panel.findChild<QPushButton*>();
    ASSERT_NE(backBtn, nullptr);
    backBtn->click();

    EXPECT_EQ(spy.count(), 1);
}

TEST_F(DrillDownPanelTest, LayerPanelDefaultVisible) {
    DrillDownPanel panel;
    EXPECT_TRUE(panel.isLayerPanelVisible());
}

TEST_F(DrillDownPanelTest, HierarchyPanelDefaultHidden) {
    DrillDownPanel panel;
    EXPECT_FALSE(panel.isHierarchyPanelVisible());
}

TEST_F(DrillDownPanelTest, SidePanelToggle) {
    DrillDownPanel panel;

    // Toggle layer panel off then on
    panel.setLayerPanelVisible(false);
    EXPECT_FALSE(panel.isLayerPanelVisible());
    panel.setLayerPanelVisible(true);
    EXPECT_TRUE(panel.isLayerPanelVisible());

    // Toggle hierarchy panel on then off
    panel.setHierarchyPanelVisible(true);
    EXPECT_TRUE(panel.isHierarchyPanelVisible());
    panel.setHierarchyPanelVisible(false);
    EXPECT_FALSE(panel.isHierarchyPanelVisible());
}

TEST_F(DrillDownPanelTest, MultipleOperationsSafe) {
    DrillDownPanel panel;

    panel.setContext("comp_1", "Die", "Tech");
    panel.setLayerPanelVisible(false);
    panel.setHierarchyPanelVisible(true);
    panel.setLayerPanelVisible(true);
    panel.setHierarchyPanelVisible(false);
    panel.clearContext();
    panel.setContext("comp_2", "Interposer", "");
    panel.clearContext();

    SUCCEED();
}

TEST_F(DrillDownPanelTest, CellComboInitiallyEmpty) {
    DrillDownPanel panel;
    QComboBox* combo = panel.findChild<QComboBox*>();
    ASSERT_NE(combo, nullptr);
    EXPECT_EQ(combo->count(), 0);
}

} // namespace chiplet
