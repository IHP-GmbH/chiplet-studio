// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * test_panel_wiring.cpp - The connections MainWindow makes between panels.
 *
 * Each case makes the connection with the PanelWiring.h helper MainWindow
 * calls, on real panels, and drives it through the same signal the user's
 * action emits.
 */

#include <gtest/gtest.h>

#include "core/Assembly.h"
#include "core/CommandProcessor.h"
#include "core/Component.h"
#include "core/commands/CmdSetRenderMode.h"
#include "ui/HierarchyPanel.h"
#include "ui/PanelWiring.h"
#include "ui/PropertiesPanel.h"
#include "ui/UnitConverter.h"
#include "view3d/AssemblyView.h"

#include <QApplication>
#include <QDockWidget>
#include <QLabel>
#include <QMainWindow>
#include <QSignalSpy>
#include <QTabBar>
#include <QTest>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

using namespace chiplet;

namespace {

std::unique_ptr<Assembly> twoDies()
{
    auto assembly = std::make_unique<Assembly>();
    assembly->set_name("PanelWiring");
    auto u1 = std::make_unique<Component>("U1", ComponentType::Die);
    u1->set_metadata("assembly_view", "full");
    assembly->add_component(std::move(u1));
    auto u2 = std::make_unique<Component>("U2", ComponentType::Die);
    u2->set_metadata("assembly_view", "abstract");
    assembly->add_component(std::move(u2));
    return assembly;
}

QString metadataValue(const PropertiesPanel& panel, const QString& key)
{
    const auto* tree = panel.findChild<QTreeWidget*>("metadataTree");
    if (!tree) return QStringLiteral("<no metadataTree>");
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        if (tree->topLevelItem(i)->text(0) == key) return tree->topLevelItem(i)->text(1);
    }
    return QString();
}

// The tab in front where Properties and 2D Layout share a dock area.
QString frontTab(const QMainWindow& window)
{
    for (const QTabBar* bar : window.findChildren<QTabBar*>()) {
        if (bar->isVisible() && bar->count() == 2) return bar->tabText(bar->currentIndex());
    }
    return QStringLiteral("<no tab bar>");
}

// The Hierarchy's Mode column for a component.
QString modeColumn(const HierarchyPanel& hierarchy, const QString& componentId)
{
    const auto* tree = hierarchy.findChild<QTreeWidget*>();
    if (!tree) return QStringLiteral("<no tree>");
    for (QTreeWidgetItemIterator it(const_cast<QTreeWidget*>(tree)); *it; ++it) {
        if ((*it)->data(0, Qt::UserRole).toString() == componentId) return (*it)->text(3);
    }
    return QStringLiteral("<no row>");
}

} // namespace

class PanelWiring : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        if (!QApplication::instance()) {
            static int argc = 1;
            static char* argv[] = {const_cast<char*>("test"), nullptr};
            static QApplication app(argc, argv);
        }
    }
    void SetUp() override { UnitConverter::instance().setUnit(DisplayUnit::Micrometers); }
};

// Hierarchy right-click > Properties: the panel shows that component and its
// tab comes to the front, from behind the 2D Layout. Docks as MainWindow lays
// them out.
TEST_F(PanelWiring, ThePropertiesRequestShowsTheComponentInFront)
{
    auto assembly = twoDies();
    QMainWindow window;
    window.setCentralWidget(new QLabel(QStringLiteral("3D")));
    auto* hierarchyDock = new QDockWidget(QStringLiteral("Hierarchy"), &window);
    auto* hierarchy = new HierarchyPanel(hierarchyDock);
    hierarchyDock->setWidget(hierarchy);
    window.addDockWidget(Qt::LeftDockWidgetArea, hierarchyDock);
    auto* propertiesDock = new QDockWidget(QStringLiteral("Properties"), &window);
    auto* properties = new PropertiesPanel(propertiesDock);
    propertiesDock->setWidget(properties);
    window.addDockWidget(Qt::RightDockWidgetArea, propertiesDock);
    auto* layoutDock = new QDockWidget(QStringLiteral("2D Layout"), &window);
    layoutDock->setWidget(new QLabel(QStringLiteral("2D")));
    window.addDockWidget(Qt::RightDockWidgetArea, layoutDock);
    window.tabifyDockWidget(propertiesDock, layoutDock);
    hierarchy->setAssembly(assembly.get());
    connectPropertiesRequest(hierarchy, properties, propertiesDock,
                             [&assembly]() { return assembly.get(); });

    window.resize(1200, 800);
    window.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&window));

    // U1 in Properties, then the 2D Layout in front: what the user sees after
    // a drill-down. Only the tab bar says which tab is in front: a dock behind
    // another in its tab group still reports isVisible().
    properties->setComponent("U1", assembly.get());
    layoutDock->raise();
    QApplication::processEvents();
    ASSERT_EQ(metadataValue(*properties, "assembly_view"), QStringLiteral("full"));
    ASSERT_EQ(frontTab(window), QStringLiteral("2D Layout"));

    emit hierarchy->showPropertiesRequested(QStringLiteral("U2"));
    QApplication::processEvents();

    EXPECT_EQ(metadataValue(*properties, "assembly_view"), QStringLiteral("abstract"));
    EXPECT_EQ(frontTab(window), QStringLiteral("Properties"));

    // Properties closed with its title-bar button: the request opens it again.
    // A closed dock is hidden, so here isVisible() does tell.
    propertiesDock->close();
    QApplication::processEvents();
    ASSERT_FALSE(properties->isVisible());

    emit hierarchy->showPropertiesRequested(QStringLiteral("U1"));
    QApplication::processEvents();

    EXPECT_EQ(metadataValue(*properties, "assembly_view"), QStringLiteral("full"));
    EXPECT_TRUE(propertiesDock->isVisible());
    EXPECT_TRUE(properties->isVisible());
}

// Undo and redo of a render mode change reach the 3D view and the Hierarchy,
// as the change itself does. The view is never shown, so it has no GL context;
// notesChanged is the first thing it does on a mode change either way.
TEST_F(PanelWiring, UndoAndRedoOfARenderModeReachTheViewAndTheHierarchy)
{
    auto assembly = twoDies();
    HierarchyPanel hierarchy;
    hierarchy.setAssembly(assembly.get());
    AssemblyView view;
    CommandProcessor processor(assembly.get());
    connectRenderModeSync(&processor, &view, &hierarchy);
    QSignalSpy viewSaw(&view, &AssemblyView::notesChanged);
    ASSERT_EQ(modeColumn(hierarchy, "U1"), QStringLiteral("Trans"));
    ASSERT_EQ(modeColumn(hierarchy, "U2"), QStringLiteral("Trans"));

    ASSERT_TRUE(processor.execute(std::make_unique<CmdSetRenderMode>(
        "U1", RenderMode::Transparent, RenderMode::DetailedNoSubstrate)));
    EXPECT_EQ(modeColumn(hierarchy, "U1"), QStringLiteral("Detail-NoSi"));
    EXPECT_EQ(viewSaw.count(), 1);

    processor.undo();
    EXPECT_EQ(modeColumn(hierarchy, "U1"), QStringLiteral("Trans"));
    EXPECT_EQ(viewSaw.count(), 2);

    processor.redo();
    EXPECT_EQ(modeColumn(hierarchy, "U1"), QStringLiteral("Detail-NoSi"));
    EXPECT_EQ(viewSaw.count(), 3);

    EXPECT_EQ(modeColumn(hierarchy, "U2"), QStringLiteral("Trans"));
}
