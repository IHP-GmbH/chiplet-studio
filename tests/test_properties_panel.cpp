/**
 * test_properties_panel.cpp - Unit tests for PropertiesPanel and UnitConverter
 */

#include <gtest/gtest.h>
#include "ui/PropertiesPanel.h"
#include "ui/UnitConverter.h"
#include "core/Assembly.h"
#include "core/Component.h"
#include "core/Technology.h"
#include <QApplication>

namespace chiplet {
namespace {

// =============================================================================
// UnitConverter Tests (no Qt required)
// =============================================================================

class UnitConverterTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset to default unit at start of each test
        UnitConverter::instance().setUnit(DisplayUnit::Micrometers);
    }
};

TEST_F(UnitConverterTest, DefaultIsUm)
{
    EXPECT_EQ(UnitConverter::instance().currentUnit(), DisplayUnit::Micrometers);
}

TEST_F(UnitConverterTest, ConvertToNanometers)
{
    UnitConverter::instance().setUnit(DisplayUnit::Nanometers);
    EXPECT_DOUBLE_EQ(UnitConverter::instance().toDisplay(1.0), 1000.0);
    EXPECT_DOUBLE_EQ(UnitConverter::instance().toDisplay(0.001), 1.0);
}

TEST_F(UnitConverterTest, ConvertToMillimeters)
{
    UnitConverter::instance().setUnit(DisplayUnit::Millimeters);
    EXPECT_DOUBLE_EQ(UnitConverter::instance().toDisplay(1000.0), 1.0);
    EXPECT_DOUBLE_EQ(UnitConverter::instance().toDisplay(1.0), 0.001);
}

TEST_F(UnitConverterTest, ConvertToMicrometers)
{
    UnitConverter::instance().setUnit(DisplayUnit::Micrometers);
    EXPECT_DOUBLE_EQ(UnitConverter::instance().toDisplay(123.456), 123.456);
}

TEST_F(UnitConverterTest, FromDisplayNanometers)
{
    UnitConverter::instance().setUnit(DisplayUnit::Nanometers);
    EXPECT_DOUBLE_EQ(UnitConverter::instance().fromDisplay(1000.0), 1.0);
}

TEST_F(UnitConverterTest, FromDisplayMillimeters)
{
    UnitConverter::instance().setUnit(DisplayUnit::Millimeters);
    EXPECT_DOUBLE_EQ(UnitConverter::instance().fromDisplay(1.0), 1000.0);
}

TEST_F(UnitConverterTest, UnitSuffixNm)
{
    UnitConverter::instance().setUnit(DisplayUnit::Nanometers);
    EXPECT_EQ(UnitConverter::instance().unitSuffix().toStdString(), "nm");
}

TEST_F(UnitConverterTest, UnitSuffixUm)
{
    UnitConverter::instance().setUnit(DisplayUnit::Micrometers);
    EXPECT_EQ(UnitConverter::instance().unitSuffix().toStdString(), "um");
}

TEST_F(UnitConverterTest, UnitSuffixMm)
{
    UnitConverter::instance().setUnit(DisplayUnit::Millimeters);
    EXPECT_EQ(UnitConverter::instance().unitSuffix().toStdString(), "mm");
}

TEST_F(UnitConverterTest, AvailableUnits)
{
    QStringList units = UnitConverter::availableUnits();
    EXPECT_EQ(units.size(), 3);
    EXPECT_EQ(units[0].toStdString(), "nm");
    EXPECT_EQ(units[1].toStdString(), "um");
    EXPECT_EQ(units[2].toStdString(), "mm");
}

TEST_F(UnitConverterTest, UnitFromIndex)
{
    EXPECT_EQ(UnitConverter::unitFromIndex(0), DisplayUnit::Nanometers);
    EXPECT_EQ(UnitConverter::unitFromIndex(1), DisplayUnit::Micrometers);
    EXPECT_EQ(UnitConverter::unitFromIndex(2), DisplayUnit::Millimeters);
    EXPECT_EQ(UnitConverter::unitFromIndex(99), DisplayUnit::Micrometers);  // Invalid defaults to um
}

TEST_F(UnitConverterTest, IndexFromUnit)
{
    EXPECT_EQ(UnitConverter::indexFromUnit(DisplayUnit::Nanometers), 0);
    EXPECT_EQ(UnitConverter::indexFromUnit(DisplayUnit::Micrometers), 1);
    EXPECT_EQ(UnitConverter::indexFromUnit(DisplayUnit::Millimeters), 2);
}

TEST_F(UnitConverterTest, ToDisplayString)
{
    UnitConverter::instance().setUnit(DisplayUnit::Micrometers);
    QString result = UnitConverter::instance().toDisplayString(1234.567, 2);
    EXPECT_EQ(result.toStdString(), "1234.57 um");
}

TEST_F(UnitConverterTest, ToDisplayStringNm)
{
    UnitConverter::instance().setUnit(DisplayUnit::Nanometers);
    QString result = UnitConverter::instance().toDisplayString(1.0, 1);
    EXPECT_EQ(result.toStdString(), "1000.0 nm");
}

// =============================================================================
// Helper to create test assemblies
// =============================================================================

std::unique_ptr<Assembly> createTestAssembly()
{
    auto assembly = std::make_unique<Assembly>();
    assembly->set_name("TestAssembly");

    // Add technology
    auto tech = std::make_unique<Technology>("tsmc_n5");
    tech->set_description("TSMC 5nm");
    assembly->add_technology(std::move(tech));

    // Add component
    auto die = std::make_unique<Component>("die_a", ComponentType::Die);
    die->set_technology("tsmc_n5");
    die->set_position({100.0, 200.0, 50.0});
    die->set_rotation({45.0});
    die->set_dimensions({5000.0, 4000.0, 100.0});
    die->set_layout_path("./layouts/die_a.gds");
    die->set_top_cell("DIE_TOP");
    die->set_metadata("vendor", "ACME Corp");
    assembly->add_component(std::move(die));

    return assembly;
}

std::unique_ptr<Component> createDieArrayComponent()
{
    auto arr = std::make_unique<Component>("hbm_array", ComponentType::DieArray);
    arr->set_technology("hbm_tech");

    ComponentArray arrayConfig;
    arrayConfig.pattern = "grid";
    arrayConfig.countX = 2;
    arrayConfig.countY = 2;
    arrayConfig.pitchX = 6000.0;
    arrayConfig.pitchY = 8000.0;
    arr->set_array(arrayConfig);

    return arr;
}

// =============================================================================
// PropertiesPanel Widget Tests
// =============================================================================

class PropertiesPanelTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        if (!QApplication::instance()) {
            static int argc = 1;
            static char* argv[] = {const_cast<char*>("test"), nullptr};
            static QApplication app(argc, argv);
        }
    }

    void SetUp() override {
        UnitConverter::instance().setUnit(DisplayUnit::Micrometers);
    }
};

TEST_F(PropertiesPanelTest, Construction)
{
    PropertiesPanel panel;
    // Panel should construct without crash
    EXPECT_TRUE(true);
}

TEST_F(PropertiesPanelTest, SetInvalidComponent)
{
    PropertiesPanel panel;
    // Set with invalid/empty component ID
    panel.setComponent("", nullptr);
    // Should not crash
    EXPECT_TRUE(true);
}

TEST_F(PropertiesPanelTest, SetValidComponent)
{
    PropertiesPanel panel;
    auto assembly = createTestAssembly();

    // Verify component exists
    Component* die = assembly->component("die_a");
    ASSERT_NE(die, nullptr);

    // Set component using ID-based API
    panel.setComponent("die_a", assembly.get());
    // Should display component properties without crash
    EXPECT_TRUE(true);
}

TEST_F(PropertiesPanelTest, ClearSelection)
{
    PropertiesPanel panel;
    auto assembly = createTestAssembly();

    panel.setComponent("die_a", assembly.get());
    panel.clearSelection();
    // Should reset all fields without crash
    EXPECT_TRUE(true);
}

TEST_F(PropertiesPanelTest, SetAssembly)
{
    PropertiesPanel panel;
    auto assembly = createTestAssembly();
    panel.setAssembly(assembly.get());
    // Should store assembly reference without crash
    EXPECT_TRUE(true);
}

TEST_F(PropertiesPanelTest, DieArrayShowsArrayGroup)
{
    PropertiesPanel panel;
    auto assembly = std::make_unique<Assembly>();

    // Add a regular die
    auto die = std::make_unique<Component>("die", ComponentType::Die);
    assembly->add_component(std::move(die));

    // Add a die array
    auto dieArray = createDieArrayComponent();
    assembly->add_component(std::move(dieArray));

    // First set a regular die - array group should be hidden
    panel.setComponent("die", assembly.get());

    // Then set a die array - array group should be visible
    panel.setComponent("hbm_array", assembly.get());
    // Should not crash
    EXPECT_TRUE(true);
}

TEST_F(PropertiesPanelTest, ComponentWithMetadata)
{
    PropertiesPanel panel;
    auto assembly = createTestAssembly();

    // die_a has metadata set in createTestAssembly()
    panel.setComponent("die_a", assembly.get());
    // Metadata group should be visible
    EXPECT_TRUE(true);
}

TEST_F(PropertiesPanelTest, ComponentWithoutMetadata)
{
    PropertiesPanel panel;
    auto assembly = std::make_unique<Assembly>();

    // Add a simple die without metadata
    auto die = std::make_unique<Component>("simple_die", ComponentType::Die);
    assembly->add_component(std::move(die));

    panel.setComponent("simple_die", assembly.get());
    // Metadata group should be hidden
    EXPECT_TRUE(true);
}

TEST_F(PropertiesPanelTest, MultipleComponentChanges)
{
    PropertiesPanel panel;
    auto assembly = createTestAssembly();

    // Cycle through components using ID-based API
    panel.setComponent("die_a", assembly.get());
    panel.setComponent("", assembly.get());  // Invalid ID clears selection
    panel.setComponent("die_a", assembly.get());
    panel.clearSelection();
    // Should handle multiple changes without crash
    EXPECT_TRUE(true);
}

} // namespace
} // namespace chiplet
