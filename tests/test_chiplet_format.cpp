/**
 * test_chiplet_format.cpp - Unit tests for ChipletFormat parser
 */

#include <gtest/gtest.h>
#include <filesystem>
#include "formats/ChipletFormat.h"

namespace chiplet {
namespace {

// Get path to test fixtures (FIXTURES_DIR defined via CMake)
std::string fixturesPath()
{
    return FIXTURES_DIR;
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

// Test parsing connection_stacks section
TEST(ChipletFormat, LoadConnectionStacks)
{
    ChipletFormat format;
    auto assembly = format.load(fixturePath("with_connection_stacks.chiplet"));

    ASSERT_NE(assembly, nullptr);
    EXPECT_EQ(assembly->name(), "Test Assembly with Connection Stacks");

    // Verify connection stacks parsed
    EXPECT_EQ(assembly->connection_stacks().size(), 2u);

    auto cupillar = assembly->connection_stack("cupillar_opt1");
    ASSERT_NE(cupillar, nullptr);
    EXPECT_EQ(cupillar->description, "PacTech Cu Pillar, Table 6.1 Option 1 (35um opening)");
    EXPECT_EQ(cupillar->layers.size(), 2u);
    EXPECT_EQ(cupillar->layers[0].name, "CuPillar");
    EXPECT_EQ(cupillar->layers[0].material, "Cu");
    EXPECT_DOUBLE_EQ(cupillar->layers[0].height, 28.0);
    EXPECT_DOUBLE_EQ(cupillar->layers[0].diameter, 44.0);
    EXPECT_DOUBLE_EQ(cupillar->total_height(), 44.0);

    auto sbump = assembly->connection_stack("sbump_sac305");
    ASSERT_NE(sbump, nullptr);
    EXPECT_DOUBLE_EQ(sbump->total_height(), 80.0);
}

// Test auto-z calculation from interposer thickness + connection stack
TEST(ChipletFormat, AutoZCalculation)
{
    ChipletFormat format;
    auto assembly = format.load(fixturePath("with_connection_stacks.chiplet"));

    ASSERT_NE(assembly, nullptr);

    // die_a has connection=cupillar_opt1, z was 0 -> auto-calculated
    // interposer thickness=13.83, cupillar total=44.0 -> z=57.83
    auto die_a = assembly->component("die_a");
    ASSERT_NE(die_a, nullptr);
    EXPECT_NEAR(die_a->position().z, 57.83, 0.01);

    // die_b has connection=sbump_sac305, z was 0 -> auto-calculated
    // interposer thickness=13.83, sbump total=80.0 -> z=93.83
    auto die_b = assembly->component("die_b");
    ASSERT_NE(die_b, nullptr);
    EXPECT_NEAR(die_b->position().z, 93.83, 0.01);

    // die_c has no connection and explicit z=100 -> unchanged
    auto die_c = assembly->component("die_c");
    ASSERT_NE(die_c, nullptr);
    EXPECT_DOUBLE_EQ(die_c->position().z, 100.0);
}

// Test backward compatibility: files without connection_stacks still load
TEST(ChipletFormat, BackwardCompatNoConnectionStacks)
{
    ChipletFormat format;
    auto assembly = format.load(fixturePath("with_components.chiplet"));

    ASSERT_NE(assembly, nullptr);
    EXPECT_TRUE(assembly->connection_stacks().empty());

    // Components retain their original z values
    auto logic = assembly->component("logic_die");
    ASSERT_NE(logic, nullptr);
    EXPECT_DOUBLE_EQ(logic->position().z, 650.0);
}

// Test round-trip preserves connection stack data
TEST(ChipletFormat, RoundTripConnectionStacks)
{
    ChipletFormat format;
    auto assembly = format.load(fixturePath("with_connection_stacks.chiplet"));

    ASSERT_NE(assembly, nullptr);

    // Save to temporary file
    std::string tempPath = "test_connection_stacks_output.chiplet";
    EXPECT_NO_THROW(format.save(*assembly, tempPath));

    // Load back
    ChipletFormat format2;
    auto assembly2 = format2.load(tempPath);

    ASSERT_NE(assembly2, nullptr);

    // Connection stacks preserved
    EXPECT_EQ(assembly2->connection_stacks().size(), 2u);
    auto cupillar = assembly2->connection_stack("cupillar_opt1");
    ASSERT_NE(cupillar, nullptr);
    EXPECT_DOUBLE_EQ(cupillar->total_height(), 44.0);
    EXPECT_EQ(cupillar->layers.size(), 2u);

    // Component connection field preserved
    auto die_a = assembly2->component("die_a");
    ASSERT_NE(die_a, nullptr);
    EXPECT_EQ(die_a->connection(), "cupillar_opt1");

    // z was auto-calculated on first load and saved explicitly,
    // so on reload it stays as-is (not re-calculated since z != 0)
    EXPECT_NEAR(die_a->position().z, 57.83, 0.01);

    // Cleanup
    std::filesystem::remove(tempPath);
}

// Test loading file with flow section
TEST(ChipletFormat, LoadWithFlow)
{
    ChipletFormat format;
    auto assembly = format.load(fixturePath("with_flow.chiplet"));

    ASSERT_NE(assembly, nullptr);
    EXPECT_EQ(assembly->name(), "Flow Test Assembly");

    // Flow definition should be populated
    EXPECT_TRUE(assembly->has_flow());
    const auto& def = assembly->flow_definition();
    EXPECT_EQ(def.step_count(), 2u);
    EXPECT_EQ(def.working_directory, "/tmp/flow_test");

    // Check environment
    ASSERT_EQ(def.environment.size(), 2u);
    EXPECT_EQ(def.environment.at("PDK_ROOT"), "/opt/pdk");
    EXPECT_EQ(def.environment.at("DEBUG"), "1");

    // Check steps
    EXPECT_EQ(def.steps[0].id, "step_hello");
    EXPECT_EQ(def.steps[0].name, "Hello World");
    EXPECT_EQ(def.steps[0].tool_path, "/bin/echo");
    ASSERT_EQ(def.steps[0].args.size(), 2u);
    EXPECT_EQ(def.steps[0].args[0], "hello");
    EXPECT_EQ(def.steps[0].args[1], "Flow Test Assembly");  // ${assembly.name} resolved

    EXPECT_EQ(def.steps[1].id, "step_goodbye");
    ASSERT_EQ(def.steps[1].depends_on.size(), 1u);
    EXPECT_EQ(def.steps[1].depends_on[0], "step_hello");
}

// Test backward compatibility: files without flow section
TEST(ChipletFormat, BackwardCompatNoFlow)
{
    ChipletFormat format;
    auto assembly = format.load(fixturePath("minimal.chiplet"));

    ASSERT_NE(assembly, nullptr);
    EXPECT_FALSE(assembly->has_flow());
    EXPECT_TRUE(assembly->flow_definition().empty());
}

// Test round-trip preserves flow definition
TEST(ChipletFormat, RoundTripFlow)
{
    ChipletFormat format;
    auto assembly = format.load(fixturePath("with_flow.chiplet"));

    ASSERT_NE(assembly, nullptr);
    EXPECT_TRUE(assembly->has_flow());

    // Save to temporary file
    std::string tempPath = "test_flow_roundtrip.chiplet";
    EXPECT_NO_THROW(format.save(*assembly, tempPath));

    // Load back
    ChipletFormat format2;
    auto assembly2 = format2.load(tempPath);

    ASSERT_NE(assembly2, nullptr);
    EXPECT_TRUE(assembly2->has_flow());

    const auto& def1 = assembly->flow_definition();
    const auto& def2 = assembly2->flow_definition();

    EXPECT_EQ(def2.working_directory, def1.working_directory);
    EXPECT_EQ(def2.environment.size(), def1.environment.size());
    EXPECT_EQ(def2.step_count(), def1.step_count());

    // Verify step data survived round-trip
    EXPECT_EQ(def2.steps[0].id, "step_hello");
    EXPECT_EQ(def2.steps[0].name, "Hello World");
    ASSERT_EQ(def2.steps[0].args.size(), 2u);
    EXPECT_EQ(def2.steps[0].args[1], "Flow Test Assembly");

    EXPECT_EQ(def2.steps[1].id, "step_goodbye");
    ASSERT_EQ(def2.steps[1].depends_on.size(), 1u);
    EXPECT_EQ(def2.steps[1].depends_on[0], "step_hello");

    // Cleanup
    std::filesystem::remove(tempPath);
}

} // namespace
} // namespace chiplet
