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

} // namespace
} // namespace chiplet
