/**
 * test_net_graph.cpp - Tests for NetGraphPanel
 */

#include <gtest/gtest.h>
#include <QApplication>
#include <QSignalSpy>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QStackedWidget>
#include <QCheckBox>
#include <QGraphicsItem>

#include "ui/NetGraphPanel.h"
#include "core/Assembly.h"
#include "core/Component.h"
#include "core/Netlist.h"

using namespace chiplet;

class NetGraphPanelTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!QApplication::instance()) {
            static int argc = 1;
            static char* argv[] = {const_cast<char*>("test")};
            static QApplication app(argc, argv);
        }
    }

    // Create an assembly matching chiplet_demo.chiplet netlist
    std::unique_ptr<Assembly> createDemoAssembly() {
        auto assembly = std::make_unique<Assembly>();
        assembly->set_name("Demo");

        assembly->add_component(std::make_unique<Component>("U1", ComponentType::Die));
        assembly->add_component(std::make_unique<Component>("U2", ComponentType::Die));
        assembly->add_component(std::make_unique<Component>("U3", ComponentType::Die));
        assembly->add_component(std::make_unique<Component>("U4", ComponentType::Die));

        Netlist nl;

        Net vdd("VDD", NetClass::Power);
        vdd.add_connection("U2", "vdd");
        vdd.add_connection("U4", "Pin3");
        nl.add_net(std::move(vdd));

        Net gnd("GND", NetClass::Ground);
        gnd.add_connection("U1", "Pin8");
        gnd.add_connection("U2", "vss");
        gnd.add_connection("U3", "VSS");
        gnd.add_connection("U4", "Pin8");
        nl.add_net(std::move(gnd));

        Net aout("Aout_SIG", NetClass::Signal);
        aout.add_connection("U1", "Pin10");
        aout.add_connection("U3", "Aout");
        nl.add_net(std::move(aout));

        Net vinn("VINN", NetClass::Signal);
        vinn.add_connection("U2", "vinn");
        vinn.add_connection("U4", "Pin4");
        nl.add_net(std::move(vinn));

        Net vout("VOUT", NetClass::Signal);
        vout.add_connection("U2", "vout");
        vout.add_connection("U4", "Pin6");
        nl.add_net(std::move(vout));

        assembly->set_netlist(std::move(nl));
        return assembly;
    }

    std::unique_ptr<Assembly> createEmptyAssembly() {
        auto assembly = std::make_unique<Assembly>();
        assembly->set_name("Empty");
        return assembly;
    }
};

TEST_F(NetGraphPanelTest, ConstructionNoCrash) {
    NetGraphPanel panel;
    EXPECT_NE(panel.graphicsView(), nullptr);
    EXPECT_NE(panel.graphicsScene(), nullptr);
    EXPECT_NE(panel.stack(), nullptr);
    EXPECT_EQ(panel.stack()->currentIndex(), 0);
    EXPECT_EQ(panel.nodeCount(), 0);
    EXPECT_EQ(panel.edgeCount(), 0);
}

TEST_F(NetGraphPanelTest, EmptyNetlist) {
    NetGraphPanel panel;

    // Null assembly
    panel.setAssembly(nullptr);
    EXPECT_EQ(panel.stack()->currentIndex(), 0);
    EXPECT_EQ(panel.nodeCount(), 0);
    EXPECT_EQ(panel.edgeCount(), 0);

    // Assembly with no netlist
    auto assembly = createEmptyAssembly();
    panel.setAssembly(assembly.get());
    EXPECT_EQ(panel.stack()->currentIndex(), 0);
    EXPECT_EQ(panel.nodeCount(), 0);
    EXPECT_EQ(panel.edgeCount(), 0);
}

TEST_F(NetGraphPanelTest, BuildFromChipletDemo) {
    NetGraphPanel panel;
    auto assembly = createDemoAssembly();

    panel.setAssembly(assembly.get());

    EXPECT_EQ(panel.stack()->currentIndex(), 1);
    EXPECT_EQ(panel.nodeCount(), 4);  // U1, U2, U3, U4
    EXPECT_GT(panel.edgeCount(), 0);
}

TEST_F(NetGraphPanelTest, FilterByClass) {
    NetGraphPanel panel;
    auto assembly = createDemoAssembly();
    panel.setAssembly(assembly.get());

    int totalEdges = panel.edgeCount();
    ASSERT_GT(totalEdges, 0);

    // Count visible edges before filtering
    auto countVisible = [&]() -> int {
        int count = 0;
        for (auto* item : panel.graphicsScene()->items()) {
            if (item->data(Qt::UserRole + 2).toInt() == 2 && item->isVisible()) {
                ++count;
            }
        }
        return count;
    };

    int visibleBefore = countVisible();
    EXPECT_EQ(visibleBefore, totalEdges);

    // Find and uncheck the Power checkbox
    auto* powerCb = panel.findChild<QCheckBox*>();
    QList<QCheckBox*> checkboxes = panel.findChildren<QCheckBox*>();
    QCheckBox* powerCheckbox = nullptr;
    for (auto* cb : checkboxes) {
        if (cb->text() == "Power") {
            powerCheckbox = cb;
            break;
        }
    }
    ASSERT_NE(powerCheckbox, nullptr);

    powerCheckbox->setChecked(false);
    int visibleAfter = countVisible();
    EXPECT_LT(visibleAfter, visibleBefore);

    // Re-check Power
    powerCheckbox->setChecked(true);
    int visibleRestored = countVisible();
    EXPECT_EQ(visibleRestored, visibleBefore);
}

TEST_F(NetGraphPanelTest, ComponentHighlight) {
    NetGraphPanel panel;
    auto assembly = createDemoAssembly();
    panel.setAssembly(assembly.get());

    // Highlight U2
    panel.highlightComponent("U2");

    // Verify U2 node is selected in the scene
    auto selected = panel.graphicsScene()->selectedItems();
    EXPECT_GE(selected.size(), 1);

    bool foundU2 = false;
    for (auto* item : selected) {
        if (item->data(Qt::UserRole).toString() == "U2") {
            foundU2 = true;
            break;
        }
    }
    EXPECT_TRUE(foundU2);

    // Clear highlight
    panel.clearHighlight();
    selected = panel.graphicsScene()->selectedItems();
    EXPECT_EQ(selected.size(), 0);
}

TEST_F(NetGraphPanelTest, MultiDropNet) {
    NetGraphPanel panel;
    auto assembly = createDemoAssembly();
    panel.setAssembly(assembly.get());

    // GND connects 4 components -> should have a hub
    EXPECT_GE(panel.hubCount(), 1);

    // Count hub items with Ground net class
    int groundHubs = 0;
    for (auto* item : panel.graphicsScene()->items()) {
        if (item->data(Qt::UserRole + 2).toInt() == 3) {  // TYPE_HUB
            int nc = item->data(Qt::UserRole + 1).toInt();
            if (nc == static_cast<int>(NetClass::Ground)) {
                ++groundHubs;
            }
        }
    }
    EXPECT_EQ(groundHubs, 1);
}

TEST_F(NetGraphPanelTest, SelectionEmitsSignal) {
    NetGraphPanel panel;
    auto assembly = createDemoAssembly();
    panel.setAssembly(assembly.get());

    QSignalSpy spy(&panel, &NetGraphPanel::componentSelected);

    // Find a node item and select it
    for (auto* item : panel.graphicsScene()->items()) {
        if (item->data(Qt::UserRole + 2).toInt() == 1) {  // TYPE_NODE
            panel.graphicsScene()->clearSelection();
            item->setSelected(true);
            break;
        }
    }

    EXPECT_GE(spy.count(), 1);
    if (spy.count() > 0) {
        QString selectedId = spy.at(0).at(0).toString();
        EXPECT_FALSE(selectedId.isEmpty());
    }
}

TEST_F(NetGraphPanelTest, ResetOnNewAssembly) {
    NetGraphPanel panel;
    auto assembly = createDemoAssembly();
    panel.setAssembly(assembly.get());
    EXPECT_EQ(panel.nodeCount(), 4);

    // Set empty assembly
    auto empty = createEmptyAssembly();
    panel.setAssembly(empty.get());
    EXPECT_EQ(panel.stack()->currentIndex(), 0);
    EXPECT_EQ(panel.nodeCount(), 0);

    // Set back to demo
    panel.setAssembly(assembly.get());
    EXPECT_EQ(panel.stack()->currentIndex(), 1);
    EXPECT_EQ(panel.nodeCount(), 4);
}
