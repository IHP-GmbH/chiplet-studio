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
#include <QTreeWidget>
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

TEST_F(DrillDownPanelTest, LayerTreeExists) {
    DrillDownPanel panel;
    QTreeWidget* tree = panel.findChild<QTreeWidget*>();
    ASSERT_NE(tree, nullptr);
    EXPECT_EQ(tree->topLevelItemCount(), 0);
}

TEST_F(DrillDownPanelTest, LayerTreeEmptyAfterClearContext) {
    DrillDownPanel panel;
    panel.setContext("comp_1", "CPU", "Tech");
    panel.clearContext();
    QTreeWidget* tree = panel.findChild<QTreeWidget*>();
    ASSERT_NE(tree, nullptr);
    EXPECT_EQ(tree->topLevelItemCount(), 0);
}

TEST_F(DrillDownPanelTest, LayerTreeContextMenuPolicy) {
    DrillDownPanel panel;
    QTreeWidget* tree = panel.findChild<QTreeWidget*>();
    ASSERT_NE(tree, nullptr);
    EXPECT_EQ(tree->contextMenuPolicy(), Qt::CustomContextMenu);
}

namespace {
QToolButton* findToggle(const DrillDownPanel& panel, const QString& text) {
    const QList<QToolButton*> buttons = panel.findChildren<QToolButton*>();
    for (QToolButton* b : buttons) {
        if (b->text() == text) {
            return b;
        }
    }
    return nullptr;
}
} // namespace

// The mini-map is drawn over the lower-right of the artwork, so it is opt-in.
// Member and toggle have to agree, or the first click reads as a no-op.
TEST_F(DrillDownPanelTest, OverviewIsOffOnAFreshPanel) {
    DrillDownPanel panel;
    EXPECT_FALSE(panel.isOverviewVisible());
    QToolButton* toggle = findToggle(panel, "Overview");
    ASSERT_NE(toggle, nullptr);
    EXPECT_FALSE(toggle->isChecked());
}

TEST_F(DrillDownPanelTest, OverviewToggleAndMemberStayInStep) {
    DrillDownPanel panel;
    QToolButton* toggle = findToggle(panel, "Overview");
    ASSERT_NE(toggle, nullptr);

    toggle->click();
    EXPECT_TRUE(panel.isOverviewVisible());
    EXPECT_TRUE(toggle->isChecked());

    panel.setOverviewVisible(false);
    EXPECT_FALSE(toggle->isChecked());
}

// Whatever the user picked holds across the context calls: nothing in that path
// is allowed to quietly reset it.
//
// Scope, because the name could promise more than this runs: no assembly GDS is
// loaded here, and showFullAssembly returns immediately without one, so this is
// not evidence across a real drill-down and Back. That belongs to the rehearsal.
TEST_F(DrillDownPanelTest, OverviewChoiceSurvivesContextCalls) {
    DrillDownPanel panel;
    panel.setOverviewVisible(true);

    panel.setContext("comp_1", "CPU Die", "IHP SG13G2");
    EXPECT_TRUE(panel.isOverviewVisible());
    panel.showFullAssembly();
    EXPECT_TRUE(panel.isOverviewVisible());
    panel.clearContext();
    EXPECT_TRUE(panel.isOverviewVisible());

    // And the same in the other direction.
    panel.setOverviewVisible(false);
    panel.setContext("comp_2", "Interposer", "");
    panel.clearContext();
    EXPECT_FALSE(panel.isOverviewVisible());
}

// Where Back goes depends on what is loaded, and the tooltip tracks it. Only the
// two states reachable without a GDS file are covered here: with no assembly
// loaded MainWindow hides the 2D dock, which does return the user to the 3D
// view, and a drill-down with no assembly behind it says the same. The two
// assembly-mode wordings need a real layout and belong to the rehearsal.
TEST_F(DrillDownPanelTest, BackTooltipTracksWhereBackWouldGo) {
    DrillDownPanel panel;
    QPushButton* backBtn = panel.findChild<QPushButton*>();
    ASSERT_NE(backBtn, nullptr);
    EXPECT_EQ(backBtn->toolTip(), QString("Return to 3D view"));

    panel.setContext("comp_1", "CPU Die", "IHP SG13G2");
    EXPECT_EQ(backBtn->toolTip(), QString("Return to 3D view"));
}

} // namespace chiplet
