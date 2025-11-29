/**
 * test_chiplet_format.cpp - Unit tests for ChipletFormat parser
 */

#include <gtest/gtest.h>
#include <filesystem>
#include "formats/ChipletFormat.h"

namespace chiplet {
namespace {

// Get path to test fixtures
std::string fixturesPath()
{
    // When running from build directory, fixtures are copied there
    return "fixtures";
}

std::string fixturePath(const std::string& filename)
{
    return fixturesPath() + "/" + filename;
}

// Test loading a minimal .chiplet file
TEST(ChipletFormat, LoadMinimalFile)
{
    ChipletFormat format;
    auto assembly = format.load(fixturePath("minimal.chiplet"));

    ASSERT_NE(assembly, nullptr);
    EXPECT_EQ(assembly->name(), "Test Assembly");
    EXPECT_EQ(assembly->description(), "Minimal test file");
    EXPECT_EQ(assembly->units(), "um");
}

// Test loading file with technologies
TEST(ChipletFormat, LoadWithTechnologies)
{
    ChipletFormat format;
    auto assembly = format.load(fixturePath("with_technologies.chiplet"));

    ASSERT_NE(assembly, nullptr);
    EXPECT_EQ(assembly->name(), "Test Assembly with Technologies");
    EXPECT_EQ(assembly->author(), "Test Author");

    // Check technologies
    EXPECT_NE(assembly->technology("test_tech"), nullptr);
    EXPECT_NE(assembly->technology("interposer_tech"), nullptr);

    auto tech = assembly->technology("test_tech");
    EXPECT_EQ(tech->description(), "Test technology");
    EXPECT_DOUBLE_EQ(tech->dbu(), 0.001);
}

// Test loading file with components
TEST(ChipletFormat, LoadWithComponents)
{
    ChipletFormat format;
    auto assembly = format.load(fixturePath("with_components.chiplet"));

    ASSERT_NE(assembly, nullptr);
    EXPECT_EQ(assembly->name(), "Test Assembly with Components");
    EXPECT_EQ(assembly->created(), "2024-01-15");
    EXPECT_EQ(assembly->modified(), "2024-01-20");

    // Check components
    EXPECT_EQ(assembly->components().size(), 4u);

    // Check substrate
    auto substrate = assembly->component("substrate");
    ASSERT_NE(substrate, nullptr);
    EXPECT_EQ(substrate->type(), ComponentType::Substrate);
    EXPECT_DOUBLE_EQ(substrate->dimensions().width, 10000);

    // Check logic die
    auto logic = assembly->component("logic_die");
    ASSERT_NE(logic, nullptr);
    EXPECT_EQ(logic->type(), ComponentType::Die);
    EXPECT_DOUBLE_EQ(logic->position().x, 1000);
    EXPECT_DOUBLE_EQ(logic->position().y, 1000);
    EXPECT_DOUBLE_EQ(logic->position().z, 650);
    EXPECT_EQ(logic->metadata("vendor"), "Test Vendor");
    EXPECT_EQ(logic->metadata("part_number"), "TEST-001");
}

// Test loading die array
TEST(ChipletFormat, LoadWithDieArray)
{
    ChipletFormat format;
    auto assembly = format.load(fixturePath("with_components.chiplet"));

    ASSERT_NE(assembly, nullptr);

    auto hbm = assembly->component("hbm_stack");
    ASSERT_NE(hbm, nullptr);
    EXPECT_EQ(hbm->type(), ComponentType::DieArray);
    EXPECT_TRUE(hbm->is_array());

    const auto& arr = hbm->array().value();
    EXPECT_EQ(arr.pattern, "grid");
    EXPECT_EQ(arr.countX, 2);
    EXPECT_EQ(arr.countY, 2);
    EXPECT_DOUBLE_EQ(arr.pitchX, 1500);
    EXPECT_DOUBLE_EQ(arr.pitchY, 2000);
    EXPECT_DOUBLE_EQ(arr.startPosition.x, 4000);
    EXPECT_DOUBLE_EQ(arr.startPosition.y, 1000);
    EXPECT_DOUBLE_EQ(arr.startPosition.z, 650);
}

// Test invalid YAML file
TEST(ChipletFormat, InvalidYamlFile)
{
    ChipletFormat format;
    EXPECT_THROW(format.load(fixturePath("invalid_yaml.chiplet")), ChipletFormatException);
}

// Test missing required field
TEST(ChipletFormat, MissingRequiredField)
{
    ChipletFormat format;
    try {
        format.load(fixturePath("invalid_missing_name.chiplet"));
        FAIL() << "Expected ChipletFormatException";
    } catch (const ChipletFormatException& e) {
        // Check that error message mentions "name"
        std::string msg = e.what();
        EXPECT_NE(msg.find("name"), std::string::npos);
    }
}

// Test file not found
TEST(ChipletFormat, FileNotFound)
{
    ChipletFormat format;
    EXPECT_THROW(format.load("nonexistent.chiplet"), ChipletFormatException);
}

// Test round-trip (load then save)
TEST(ChipletFormat, RoundTrip)
{
    ChipletFormat format;
    auto assembly = format.load(fixturePath("with_components.chiplet"));

    ASSERT_NE(assembly, nullptr);

    // Save to temporary file
    std::string tempPath = "test_output.chiplet";
    EXPECT_NO_THROW(format.save(*assembly, tempPath));

    // Load back
    ChipletFormat format2;
    auto assembly2 = format2.load(tempPath);

    ASSERT_NE(assembly2, nullptr);

    // Verify key data survived round-trip
    EXPECT_EQ(assembly2->name(), assembly->name());
    EXPECT_EQ(assembly2->description(), assembly->description());
    EXPECT_EQ(assembly2->author(), assembly->author());
    EXPECT_EQ(assembly2->units(), assembly->units());
    EXPECT_EQ(assembly2->components().size(), assembly->components().size());

    // Check a specific component
    auto logic = assembly2->component("logic_die");
    ASSERT_NE(logic, nullptr);
    EXPECT_DOUBLE_EQ(logic->position().x, 1000);
    EXPECT_EQ(logic->metadata("vendor"), "Test Vendor");

    // Cleanup
    std::filesystem::remove(tempPath);
}

// Test type conversion functions
TEST(ChipletFormat, TypeConversion)
{
    EXPECT_EQ(string_to_component_type("die"), ComponentType::Die);
    EXPECT_EQ(string_to_component_type("die_array"), ComponentType::DieArray);
    EXPECT_EQ(string_to_component_type("interposer"), ComponentType::Interposer);
    EXPECT_EQ(string_to_component_type("substrate"), ComponentType::Substrate);
    EXPECT_EQ(string_to_component_type("unknown"), ComponentType::Die);  // Default

    EXPECT_EQ(component_type_to_string(ComponentType::Die), "die");
    EXPECT_EQ(component_type_to_string(ComponentType::DieArray), "die_array");
    EXPECT_EQ(component_type_to_string(ComponentType::Interposer), "interposer");
    EXPECT_EQ(component_type_to_string(ComponentType::Substrate), "substrate");
}

} // namespace
} // namespace chiplet
