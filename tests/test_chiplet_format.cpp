/**
 * test_chiplet_format.cpp - Unit tests for ChipletFormat parser
 */

#include <gtest/gtest.h>
#include <filesystem>
#include <cstdlib>
#include "formats/ChipletFormat.h"
#include "core/LayerStackup.h"

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

// Test parsing the optional interconnect.adapter root block
TEST(ChipletFormat, LoadInterconnectAdapter)
{
    ChipletFormat format;
    auto assembly = format.load(fixturePath("with_interconnect_adapter.chiplet"));

    ASSERT_NE(assembly, nullptr);
    EXPECT_EQ(assembly->interconnect_adapter(), "vendorx_microbump");

    // The vendor connection stack is parsed with its vendor-specific bodies.
    auto stack = assembly->connection_stack("vendorx_microbump");
    ASSERT_NE(stack, nullptr);
    EXPECT_EQ(stack->layers[0].name, "VendorXBumpCu");
    EXPECT_DOUBLE_EQ(stack->total_height(), 24.0);  // 18 + 6
}

// A .chiplet with no interconnect block leaves the adapter empty (interposer-only)
TEST(ChipletFormat, NoInterconnectAdapterIsEmpty)
{
    ChipletFormat format;
    auto assembly = format.load(fixturePath("with_connection_stacks.chiplet"));
    ASSERT_NE(assembly, nullptr);
    EXPECT_TRUE(assembly->interconnect_adapter().empty());
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

// Test orientation field parsing and defaults
TEST(ChipletFormat, OrientationDefault)
{
    Component comp("test_die", ComponentType::Die);
    EXPECT_EQ(comp.orientation(), Orientation::FaceUp);
}

TEST(ChipletFormat, OrientationSetGet)
{
    Component comp("test_die", ComponentType::Die);
    comp.set_orientation(Orientation::FaceDown);
    EXPECT_EQ(comp.orientation(), Orientation::FaceDown);
    comp.set_orientation(Orientation::FaceUp);
    EXPECT_EQ(comp.orientation(), Orientation::FaceUp);
}

TEST(ChipletFormat, OrientationRoundTrip)
{
    // Create assembly with a flip-chip die
    Assembly assembly;
    assembly.set_name("OrientationTest");
    auto die = std::make_unique<Component>("flip_die", ComponentType::Die);
    die->set_orientation(Orientation::FaceDown);
    die->set_dimensions({1000, 1000, 100});
    assembly.add_component(std::move(die));

    auto die_up = std::make_unique<Component>("normal_die", ComponentType::Die);
    die_up->set_dimensions({1000, 1000, 100});
    assembly.add_component(std::move(die_up));

    // Save
    std::string tempPath = "test_orientation_roundtrip.chiplet";
    ChipletFormat format;
    EXPECT_NO_THROW(format.save(assembly, tempPath));

    // Load back
    ChipletFormat format2;
    auto loaded = format2.load(tempPath);
    ASSERT_NE(loaded, nullptr);

    auto* flip = loaded->component("flip_die");
    ASSERT_NE(flip, nullptr);
    EXPECT_EQ(flip->orientation(), Orientation::FaceDown);

    auto* normal = loaded->component("normal_die");
    ASSERT_NE(normal, nullptr);
    EXPECT_EQ(normal->orientation(), Orientation::FaceUp);

    std::filesystem::remove(tempPath);
}

// ---------------------------------------------------------------------
// Gate 1 — coord_frame_contract.md
//
// These tests cover the schema-level plumbing: Anchor enum, parser
// support, serializer output, and the rejection of intermediate KiCad
// output marked with `_metadata.finalize_required: true`.
// Reader-side wiring (LayerMeshBuilder, AssemblyView) is verified in
// later gates.
// ---------------------------------------------------------------------

TEST(ChipletFormat, AnchorEnumDefault)
{
    // Per contract §2.2: when `anchor:` is absent the reader defaults
    // to BboxCenter (preserves pre-contract interposer behavior).
    Component comp("test", ComponentType::Die);
    EXPECT_EQ(comp.anchor(), Anchor::BboxCenter);
    EXPECT_FALSE(comp.anchor_declared());
}

TEST(ChipletFormat, AnchorEnumSetGet)
{
    Component comp("test", ComponentType::Die);
    comp.set_anchor(Anchor::GdsOrigin);
    EXPECT_EQ(comp.anchor(), Anchor::GdsOrigin);

    comp.set_anchor_declared(true);
    EXPECT_TRUE(comp.anchor_declared());

    comp.set_anchor(Anchor::BboxCenter);
    EXPECT_EQ(comp.anchor(), Anchor::BboxCenter);
}

TEST(ChipletFormat, AnchorStringConversion)
{
    EXPECT_EQ(anchor_to_string(Anchor::GdsOrigin), "gds_origin");
    EXPECT_EQ(anchor_to_string(Anchor::BboxCenter), "bbox_center");

    auto a = string_to_anchor("gds_origin");
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(a.value(), Anchor::GdsOrigin);

    auto b = string_to_anchor("bbox_center");
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(b.value(), Anchor::BboxCenter);

    // Unknown value is not silently mapped.
    EXPECT_FALSE(string_to_anchor("center").has_value());
    EXPECT_FALSE(string_to_anchor("").has_value());
    EXPECT_FALSE(string_to_anchor("GdsOrigin").has_value());  // case sensitive
}

TEST(ChipletFormat, AnchorParseField)
{
    ChipletFormat format;
    auto assembly = format.load(fixturePath("with_anchor.chiplet"));
    ASSERT_NE(assembly, nullptr);

    auto* interposer = assembly->component("interposer");
    ASSERT_NE(interposer, nullptr);
    EXPECT_EQ(interposer->anchor(), Anchor::BboxCenter);
    EXPECT_TRUE(interposer->anchor_declared());

    auto* u1 = assembly->component("U1");
    ASSERT_NE(u1, nullptr);
    EXPECT_EQ(u1->anchor(), Anchor::GdsOrigin);
    EXPECT_TRUE(u1->anchor_declared());

    auto* u2 = assembly->component("U2");
    ASSERT_NE(u2, nullptr);
    EXPECT_EQ(u2->anchor(), Anchor::GdsOrigin);
    EXPECT_TRUE(u2->anchor_declared());
}

TEST(ChipletFormat, AnchorMissingDefaultsToBboxCenter)
{
    // with_components.chiplet was authored before the contract and
    // declares no `anchor:` field on any of its 4 components.
    ChipletFormat format;
    auto assembly = format.load(fixturePath("with_components.chiplet"));
    ASSERT_NE(assembly, nullptr);
    ASSERT_EQ(assembly->components().size(), 4u);

    for (const auto& comp : assembly->components()) {
        EXPECT_EQ(comp->anchor(), Anchor::BboxCenter)
            << "component " << comp->id() << " should default to bbox_center";
        EXPECT_FALSE(comp->anchor_declared())
            << "component " << comp->id() << " should be marked undeclared";
    }
}

TEST(ChipletFormat, AnchorRoundTrip)
{
    ChipletFormat format;
    auto assembly = format.load(fixturePath("with_anchor.chiplet"));
    ASSERT_NE(assembly, nullptr);

    std::string tempPath = "test_anchor_roundtrip.chiplet";
    EXPECT_NO_THROW(format.save(*assembly, tempPath));

    ChipletFormat format2;
    auto loaded = format2.load(tempPath);
    ASSERT_NE(loaded, nullptr);

    auto* interposer = loaded->component("interposer");
    ASSERT_NE(interposer, nullptr);
    EXPECT_EQ(interposer->anchor(), Anchor::BboxCenter);
    EXPECT_TRUE(interposer->anchor_declared());

    auto* u1 = loaded->component("U1");
    ASSERT_NE(u1, nullptr);
    EXPECT_EQ(u1->anchor(), Anchor::GdsOrigin);
    EXPECT_TRUE(u1->anchor_declared());

    std::filesystem::remove(tempPath);
}

TEST(ChipletFormat, AnchorAlwaysSerializedEvenIfDefaulted)
{
    // The writer contract (§4) requires `anchor:` to be emitted
    // explicitly so downstream readers never have to guess. After a
    // round trip from a legacy file (no anchor declared), the
    // re-loaded file must have anchor_declared() == true on every
    // component.
    ChipletFormat format;
    auto assembly = format.load(fixturePath("with_components.chiplet"));
    ASSERT_NE(assembly, nullptr);

    std::string tempPath = "test_anchor_serialized.chiplet";
    EXPECT_NO_THROW(format.save(*assembly, tempPath));

    ChipletFormat format2;
    auto loaded = format2.load(tempPath);
    ASSERT_NE(loaded, nullptr);

    for (const auto& comp : loaded->components()) {
        EXPECT_TRUE(comp->anchor_declared())
            << "component " << comp->id() << " must be declared post-save";
    }

    std::filesystem::remove(tempPath);
}

TEST(ChipletFormat, MetadataFinalizeRequiredRejected)
{
    // Files marked with `_metadata.finalize_required: true` must be
    // rejected at load time (contract §5.1). Error message must point
    // the user to the finalizer command.
    ChipletFormat format;
    try {
        format.load(fixturePath("intermediate_kicad.chiplet"));
        FAIL() << "Expected ChipletFormatException";
    } catch (const ChipletFormatException& e) {
        std::string msg = e.what();
        EXPECT_NE(msg.find("intermediate"), std::string::npos)
            << "error must mention intermediate state: " << msg;
        EXPECT_NE(msg.find("hyp_to_gds.py"), std::string::npos)
            << "error must reference the finalizer: " << msg;
    }
}

// ---------------------------------------------------------------------
// Gate 2 — Reader updates (coord_frame_contract.md §3.4 / §5.5)
//
// Assembly::calculate_component_z must always land a die on a sane
// surface. Before Gate 2 it returned 0.0 for any die without a
// connection or with an undefined connection_stack — the contract
// calls that out as a footgun (the formula must hold in all cases).
// ---------------------------------------------------------------------

TEST(Assembly, CalculateZFallsBackToInterposerThicknessWhenConnectionMissing)
{
    Assembly assembly;
    auto interposer = std::make_unique<Component>("interp", ComponentType::Interposer);
    interposer->set_dimensions({1000, 1000, 13.83});
    assembly.add_component(std::move(interposer));

    auto die = std::make_unique<Component>("die_no_connection", ComponentType::Die);
    die->set_dimensions({500, 500, 50});
    // Deliberately no set_connection() call.
    assembly.add_component(std::move(die));

    double z = assembly.calculate_component_z("die_no_connection");
    EXPECT_NEAR(z, 13.83, 1e-6);
}

TEST(Assembly, CalculateZFallsBackWhenConnectionStackUndefined)
{
    Assembly assembly;
    auto interposer = std::make_unique<Component>("interp", ComponentType::Interposer);
    interposer->set_dimensions({1000, 1000, 13.83});
    assembly.add_component(std::move(interposer));

    auto die = std::make_unique<Component>("die_undefined_stack", ComponentType::Die);
    die->set_connection("nonexistent_stack");
    die->set_dimensions({500, 500, 50});
    assembly.add_component(std::move(die));

    double z = assembly.calculate_component_z("die_undefined_stack");
    EXPECT_NEAR(z, 13.83, 1e-6);
}

TEST(Assembly, CalculateZReturnsZeroForUnknownComponent)
{
    Assembly assembly;
    EXPECT_DOUBLE_EQ(assembly.calculate_component_z("not_in_assembly"), 0.0);
}

TEST(Assembly, CalculateZReturnsZeroWhenNoInterposerAndNoConnection)
{
    // Degenerate assembly (no interposer): the fallback has nothing
    // to anchor against, so the function returns 0. This is the only
    // pre-Gate-2 behavior we keep -- it is genuinely degenerate.
    Assembly assembly;
    auto die = std::make_unique<Component>("orphan_die", ComponentType::Die);
    die->set_dimensions({500, 500, 50});
    assembly.add_component(std::move(die));

    EXPECT_DOUBLE_EQ(assembly.calculate_component_z("orphan_die"), 0.0);
}

// ---------------------------------------------------------------------
// Task 9 regression guard: the interposer stackup no longer defines the
// cu-pillar bodies (CuPillar/SnAgCap/SolderBall). calculate_component_z must
// therefore source the cu-pillar mounting surface from the interconnect PDK
// fragment that the assembly's interconnect.adapter selects. Hermetic: points
// the configs dir at a body-less interposer stackup and INTERCONNECT_PDK_ROOT
// at a fake PDK that supplies CuPillar, so the test depends on neither the real
// configs nor the sibling interconnect_pdk repo.
// ---------------------------------------------------------------------
TEST(InterconnectMergeZ, DieZSourcedFromFragmentAfterBodyRemoval)
{
    const std::string base = fixturePath("interconnect_merge");
    BlenderGDSConfigs::setConfigsDir(base + "/configs");
    setenv("INTERCONNECT_PDK_ROOT", (base + "/pdk").c_str(), 1);

    ChipletFormat format;
    auto assembly = format.load(base + "/merge_z.chiplet");
    ASSERT_NE(assembly, nullptr);
    EXPECT_EQ(assembly->interconnect_adapter(), "ihp_cupillar");

    // CuPillar z_bottom (13.83, from the merged fragment) + stack height
    // (32 + 16 = 48) = 61.83. If the fragment did not merge, CuPillar would be
    // absent from the body-less interposer stackup and the result would differ.
    EXPECT_NEAR(assembly->calculate_component_z("die_a"), 61.83, 0.01);

    // Negative control: without the adapter there is no merge, CuPillar is not
    // found, and the die falls back to interposer thickness (200) + stack (48).
    assembly->set_interconnect_adapter("");
    EXPECT_NEAR(assembly->calculate_component_z("die_a"), 248.0, 0.01);

    // Restore global state so later tests see the default configs resolution.
    unsetenv("INTERCONNECT_PDK_ROOT");
    BlenderGDSConfigs::setConfigsDir("");
}

} // namespace
} // namespace chiplet
