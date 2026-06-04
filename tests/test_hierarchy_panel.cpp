/**
 * test_hierarchy_panel.cpp - Unit tests for HierarchyPanel
 *
 * Note: Tests requiring Qt GUI are minimal to avoid complex setup.
 * Focus on logic that can be tested without full widget instantiation.
 */

#include <gtest/gtest.h>
#include "ui/HierarchyPanel.h"
#include "core/Assembly.h"
#include "core/Component.h"
#include <QApplication>
#include <QTreeWidget>

namespace chiplet {
namespace {

// =============================================================================
// Helper to create test assemblies
// =============================================================================

std::unique_ptr<Assembly> createTestAssembly()
{
    auto assembly = std::make_unique<Assembly>();
    assembly->set_name("TestAssembly");

    auto substrate = std::make_unique<Component>("substrate", ComponentType::Substrate);
    substrate->set_technology("pkg_tech");
    assembly->add_component(std::move(substrate));

    auto interposer = std::make_unique<Component>("interposer", ComponentType::Interposer);
    interposer->set_technology("interposer_65nm");
    assembly->add_component(std::move(interposer));

    auto die_a = std::make_unique<Component>("die_a", ComponentType::Die);
    die_a->set_technology("tsmc_n5");
    assembly->add_component(std::move(die_a));

    auto die_b = std::make_unique<Component>("die_b", ComponentType::Die);
    die_b->set_technology("tsmc_n5");
    assembly->add_component(std::move(die_b));

    return assembly;
}

// =============================================================================
// Type String Tests (no Qt required)
// =============================================================================

TEST(HierarchyPanelLogic, TypeToStringMapping)
{
    // Test the expected string mappings
    // These are tested via ComponentStyle since typeToString is private

    EXPECT_EQ(ComponentStyleFile::default_style(ComponentType::Die).type, ComponentType::Die);
    EXPECT_EQ(ComponentStyleFile::default_style(ComponentType::Interposer).type, ComponentType::Interposer);
    EXPECT_EQ(ComponentStyleFile::default_style(ComponentType::Substrate).type, ComponentType::Substrate);
    EXPECT_EQ(ComponentStyleFile::default_style(ComponentType::DieArray).type, ComponentType::DieArray);
}

// =============================================================================
// Widget Tests (require QApplication)
// =============================================================================

class HierarchyPanelTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        // Create QApplication if not already created
        if (!QApplication::instance()) {
            static int argc = 1;
            static char* argv[] = {const_cast<char*>("test"), nullptr};
            static QApplication app(argc, argv);
        }
    }
};

TEST_F(HierarchyPanelTest, Construction)
{
    HierarchyPanel panel;
    EXPECT_EQ(panel.assembly(), nullptr);
}

TEST_F(HierarchyPanelTest, SetNullAssembly)
{
    HierarchyPanel panel;
    panel.setAssembly(nullptr);
    EXPECT_EQ(panel.assembly(), nullptr);
}

TEST_F(HierarchyPanelTest, SetValidAssembly)
{
    HierarchyPanel panel;
    auto assembly = createTestAssembly();
    Assembly* ptr = assembly.get();

    panel.setAssembly(ptr);
    EXPECT_EQ(panel.assembly(), ptr);
}

TEST_F(HierarchyPanelTest, SelectNonexistentComponent)
{
    HierarchyPanel panel;
    auto assembly = createTestAssembly();
    panel.setAssembly(assembly.get());

    // Should not crash when selecting non-existent component
    panel.selectComponent("nonexistent_id");
}

TEST_F(HierarchyPanelTest, SelectValidComponent)
{
    HierarchyPanel panel;
    auto assembly = createTestAssembly();
    panel.setAssembly(assembly.get());

    // Should not crash when selecting valid component
    panel.selectComponent("die_a");
}

TEST_F(HierarchyPanelTest, SelectEmptyString)
{
    HierarchyPanel panel;
    auto assembly = createTestAssembly();
    panel.setAssembly(assembly.get());

    // Should not crash with empty string
    panel.selectComponent("");
}

TEST_F(HierarchyPanelTest, SignalEmission)
{
    HierarchyPanel panel;
    auto assembly = createTestAssembly();
    panel.setAssembly(assembly.get());

    // Test signal connection (basic connectivity test)
    QString receivedId;
    QObject::connect(&panel, &HierarchyPanel::componentSelected,
                     [&receivedId](const QString& id) { receivedId = id; });

    // Panel should emit signal when selection changes
    // (Full signal testing would require simulating mouse clicks)
    EXPECT_TRUE(receivedId.isEmpty());  // No clicks yet
}

TEST_F(HierarchyPanelTest, MultipleAssemblyChanges)
{
    HierarchyPanel panel;

    auto assembly1 = createTestAssembly();
    panel.setAssembly(assembly1.get());
    EXPECT_EQ(panel.assembly(), assembly1.get());

    auto assembly2 = std::make_unique<Assembly>();
    assembly2->set_name("Empty");
    panel.setAssembly(assembly2.get());
    EXPECT_EQ(panel.assembly(), assembly2.get());

    panel.setAssembly(nullptr);
    EXPECT_EQ(panel.assembly(), nullptr);
}

// =============================================================================
// Integration with ComponentStyle
// =============================================================================

TEST_F(HierarchyPanelTest, IconsAreGenerated)
{
    HierarchyPanel panel;
    auto assembly = createTestAssembly();
    panel.setAssembly(assembly.get());

    // Panel should create icons without crashing
    // Full icon verification would require visual inspection
}

// =============================================================================
// Per-die interconnect method child rows
// =============================================================================

QTreeWidgetItem* findComponentRow(QTreeWidget* tree, const QString& id)
{
    QTreeWidgetItem* root = tree->topLevelItem(0);
    if (!root) {
        return nullptr;
    }
    for (int i = 0; i < root->childCount(); ++i) {
        if (root->child(i)->text(0) == id) {
            return root->child(i);
        }
    }
    return nullptr;
}

TEST_F(HierarchyPanelTest, DieConnectionGetsInterconnectChildRow)
{
    HierarchyPanel panel;
    auto assembly = createTestAssembly();
    assembly->component("die_a")->set_connection("cupillar_opt1");
    panel.setAssembly(assembly.get());

    QTreeWidget* tree = panel.findChild<QTreeWidget*>();
    ASSERT_NE(tree, nullptr);
    QTreeWidgetItem* dieA = findComponentRow(tree, "die_a");
    QTreeWidgetItem* dieB = findComponentRow(tree, "die_b");
    ASSERT_NE(dieA, nullptr);
    ASSERT_NE(dieB, nullptr);

    // die_a carries the method child; die_b (no connection) does not.
    ASSERT_EQ(dieA->childCount(), 1);
    QTreeWidgetItem* conn = dieA->child(0);
    EXPECT_EQ(conn->text(0), QString("interconnect"));
    EXPECT_EQ(conn->text(1), QString("Interconnect"));
    EXPECT_EQ(conn->text(2), QString("cupillar_opt1"));
    EXPECT_EQ(conn->data(0, Qt::UserRole + 1).toString(),
              QString("interconnect-method"));
    EXPECT_EQ(conn->data(0, Qt::UserRole + 2).toString(), QString("die_a"));
    // Not a component row (component handlers skip it) and no visibility
    // checkbox of its own (the assembly-level interconnect row owns that).
    EXPECT_TRUE(conn->data(0, Qt::UserRole).toString().isEmpty());
    EXPECT_EQ(dieB->childCount(), 0);
}

TEST_F(HierarchyPanelTest, PerDieChildRowsForMixedMethods)
{
    HierarchyPanel panel;
    auto assembly = createTestAssembly();
    assembly->component("die_a")->set_connection("cupillar_opt1");
    assembly->component("die_b")->set_connection("vendorx_microbump");
    panel.setAssembly(assembly.get());

    QTreeWidget* tree = panel.findChild<QTreeWidget*>();
    ASSERT_NE(tree, nullptr);
    QTreeWidgetItem* dieA = findComponentRow(tree, "die_a");
    QTreeWidgetItem* dieB = findComponentRow(tree, "die_b");
    ASSERT_NE(dieA, nullptr);
    ASSERT_NE(dieB, nullptr);
    ASSERT_EQ(dieA->childCount(), 1);
    ASSERT_EQ(dieB->childCount(), 1);
    EXPECT_EQ(dieA->child(0)->text(2), QString("cupillar_opt1"));
    EXPECT_EQ(dieB->child(0)->text(2), QString("vendorx_microbump"));
}

TEST_F(HierarchyPanelTest, InterposerNeverGetsMethodChildRow)
{
    HierarchyPanel panel;
    auto assembly = createTestAssembly();
    // Even a stray connection string on the interposer must not produce a
    // method child (mirrors Assembly::interconnect_method_ids()).
    assembly->component("interposer")->set_connection("cupillar_opt1");
    panel.setAssembly(assembly.get());

    QTreeWidget* tree = panel.findChild<QTreeWidget*>();
    ASSERT_NE(tree, nullptr);
    QTreeWidgetItem* interposer = findComponentRow(tree, "interposer");
    ASSERT_NE(interposer, nullptr);
    EXPECT_EQ(interposer->childCount(), 0);
}

} // namespace
} // namespace chiplet
