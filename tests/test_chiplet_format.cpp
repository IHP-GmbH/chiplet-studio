/**
 * test_chiplet_format.cpp - Unit tests for ChipletFormat parser
 */

#include <gtest/gtest.h>
#include <QtGlobal>
#include <QString>
#include <sstream>
#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#include <yaml-cpp/yaml.h>
#include "formats/ChipletFormat.h"
#include "core/LayerStackup.h"
#include "core/Technology.h"

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
    EXPECT_NE(assembly->technology("intm4tm2"), nullptr);

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

// Unknown format_version values must be rejected loudly, not consumed.
TEST(ChipletFormat, RejectsUnknownFormatVersion)
{
    namespace fs = std::filesystem;
    const fs::path base = fs::temp_directory_path() / "chiplet_format_version";
    fs::remove_all(base);
    fs::create_directories(base);
    const fs::path file = base / "future.chiplet";
    {
        std::ofstream out(file);
        out << "format_version: \"2.0\"\n"
            << "assembly:\n"
            << "  name: \"Future\"\n"
            << "  units: \"um\"\n";
    }

    ChipletFormat format;
    try {
        format.load(file.string());
        FAIL() << "Expected ChipletFormatException";
    } catch (const ChipletFormatException& e) {
        const std::string msg = e.what();
        EXPECT_NE(msg.find("2.0"), std::string::npos);
        EXPECT_NE(msg.find("1.0"), std::string::npos);
    }
    fs::remove_all(base);
}

// An unquoted YAML scalar 1.0 is the same revision and must load.
TEST(ChipletFormat, AcceptsUnquotedFormatVersion)
{
    namespace fs = std::filesystem;
    const fs::path base = fs::temp_directory_path() / "chiplet_format_unquoted";
    fs::remove_all(base);
    fs::create_directories(base);
    const fs::path file = base / "unquoted.chiplet";
    {
        std::ofstream out(file);
        out << "format_version: 1.0\n"
            << "assembly:\n"
            << "  name: \"Unquoted\"\n"
            << "  units: \"um\"\n";
    }

    ChipletFormat format;
    auto assembly = format.load(file.string());
    ASSERT_NE(assembly, nullptr);
    EXPECT_EQ(assembly->name(), "Unquoted");
    fs::remove_all(base);
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

// The interconnect.technology subblock registers the method as a PDK-backed
// technology, same identity scheme as the die/interposer PDKs.
TEST(ChipletFormat, LoadInterconnectTechnology)
{
    ChipletFormat format;
    auto assembly = format.load(fixturePath("with_interconnect_adapter.chiplet"));

    ASSERT_NE(assembly, nullptr);
    Technology* tech = assembly->technology("vendorx_microbump");
    ASSERT_NE(tech, nullptr);
    EXPECT_EQ(tech->description(),
              "Chiplet attachment (VendorX Microsystems (DEMO / non-IHP))");
    // Relative lyp reference resolved against the fixture dir
    EXPECT_NE(tech->layer_properties_path().find("interconnect_test.lyp"),
              std::string::npos);
    // Regular technologies unaffected
    EXPECT_NE(assembly->technology("test_tech"), nullptr);
}

// Round-trip: the interconnect technology is written under interconnect:
// (its canonical home), never duplicated into the technologies: map, and
// the whole block survives a save/load cycle (the writer used to drop it).
TEST(ChipletFormat, RoundTripInterconnectTechnology)
{
    ChipletFormat format;
    auto assembly = format.load(fixturePath("with_interconnect_adapter.chiplet"));
    ASSERT_NE(assembly, nullptr);

    std::string tempPath = "test_interconnect_tech_output.chiplet";
    EXPECT_NO_THROW(format.save(*assembly, tempPath));

    // Canonical placement in the written YAML
    YAML::Node doc = YAML::LoadFile(tempPath);
    ASSERT_TRUE(doc["interconnect"]);
    EXPECT_EQ(doc["interconnect"]["adapter"].as<std::string>(),
              "vendorx_microbump");
    ASSERT_TRUE(doc["interconnect"]["technology"]);
    EXPECT_TRUE(doc["interconnect"]["technology"]["layer_properties"]);
    ASSERT_TRUE(doc["technologies"]);
    EXPECT_TRUE(doc["technologies"]["test_tech"]);
    EXPECT_FALSE(doc["technologies"]["vendorx_microbump"]);

    // Model state survives the reload
    ChipletFormat format2;
    auto assembly2 = format2.load(tempPath);
    ASSERT_NE(assembly2, nullptr);
    EXPECT_EQ(assembly2->interconnect_adapter(), "vendorx_microbump");
    Technology* tech = assembly2->technology("vendorx_microbump");
    ASSERT_NE(tech, nullptr);
    EXPECT_EQ(tech->description(),
              "Chiplet attachment (VendorX Microsystems (DEMO / non-IHP))");
    EXPECT_NE(assembly2->technology("test_tech"), nullptr);

    std::filesystem::remove(tempPath);
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
// Orientation vocabulary: the frame contract defines only face_up and
// flip_chip. The reader is a lenient viewer -- it keeps rendering -- but must
// never be SILENT: a non-canonical face_down aliases to flip_chip WITH a
// warning, and any unknown token stays at the face_up default WITH a warning
// (rather than silently rendering un-mirrored). Uses a scoped qWarning capture.
// ---------------------------------------------------------------------

namespace {

std::vector<std::string>* g_captured_msgs = nullptr;

void captureHandler(QtMsgType, const QMessageLogContext&, const QString& msg)
{
    if (g_captured_msgs) {
        g_captured_msgs->push_back(msg.toStdString());
    }
}

// Load a one-die assembly with the given orientation token, capturing warnings.
std::unique_ptr<Assembly> loadWithOrientation(
    const std::string& token, std::vector<std::string>& out_warnings)
{
    namespace fs = std::filesystem;
    const fs::path base = fs::temp_directory_path() / "chiplet_orientation";
    fs::remove_all(base);
    fs::create_directories(base);
    const fs::path file = base / "orient.chiplet";
    {
        std::ofstream out(file);
        out << "format_version: \"1.0\"\n"
            << "assembly:\n"
            << "  name: \"Orient\"\n"
            << "  units: \"um\"\n"
            << "components:\n"
            << "  - id: die0\n"
            << "    type: die\n"
            << "    orientation: " << token << "\n"
            << "    dimensions: { width: 1000, height: 1000, thickness: 100 }\n";
    }
    g_captured_msgs = &out_warnings;
    QtMessageHandler prev = qInstallMessageHandler(captureHandler);
    ChipletFormat format;
    auto assembly = format.load(file.string());
    qInstallMessageHandler(prev);
    g_captured_msgs = nullptr;
    fs::remove_all(base);
    return assembly;
}

bool anyContains(const std::vector<std::string>& msgs, const std::string& needle)
{
    for (const auto& m : msgs) {
        if (m.find(needle) != std::string::npos) return true;
    }
    return false;
}

}  // namespace

TEST(ChipletFormat, OrientationFaceDownWarnsAndAliasesToFlipChip)
{
    std::vector<std::string> warnings;
    auto assembly = loadWithOrientation("face_down", warnings);
    ASSERT_NE(assembly, nullptr);
    auto* die = assembly->component("die0");
    ASSERT_NE(die, nullptr);
    EXPECT_EQ(die->orientation(), Orientation::FaceDown);  // still renders flipped
    EXPECT_TRUE(anyContains(warnings, "face_down"));       // but never silent
}

TEST(ChipletFormat, OrientationUnknownTokenWarnsAndDefaultsFaceUp)
{
    std::vector<std::string> warnings;
    auto assembly = loadWithOrientation("flip-chip", warnings);  // typo
    ASSERT_NE(assembly, nullptr);
    auto* die = assembly->component("die0");
    ASSERT_NE(die, nullptr);
    EXPECT_EQ(die->orientation(), Orientation::FaceUp);   // not silently mirrored
    EXPECT_TRUE(anyContains(warnings, "flip-chip"));
}

TEST(ChipletFormat, OrientationFlipChipIsSilent)
{
    std::vector<std::string> warnings;
    auto assembly = loadWithOrientation("flip_chip", warnings);
    ASSERT_NE(assembly, nullptr);
    auto* die = assembly->component("die0");
    ASSERT_NE(die, nullptr);
    EXPECT_EQ(die->orientation(), Orientation::FaceDown);
    EXPECT_FALSE(anyContains(warnings, "orientation"));  // canonical: no warning
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

// The optional component-level attachment_surface_z survives a save/load round
// trip, dimensions.thickness stays an independent physical body, and a
// component that never declared the field round-trips as absent (nullopt).
TEST(ChipletFormat, AttachmentSurfaceZRoundTrip)
{
    Assembly assembly;
    assembly.set_name("AttachmentSurfaceZTest");
    auto interposer = std::make_unique<Component>("interposer", ComponentType::Interposer);
    interposer->set_dimensions({1000, 1000, 300.0});
    interposer->set_attachment_surface_z(13.83);
    assembly.add_component(std::move(interposer));

    auto die = std::make_unique<Component>("U1", ComponentType::Die);
    die->set_dimensions({500, 500, 50});
    assembly.add_component(std::move(die));

    std::string tempPath = "test_attachment_surface_z_roundtrip.chiplet";
    ChipletFormat format;
    EXPECT_NO_THROW(format.save(assembly, tempPath));

    ChipletFormat format2;
    auto loaded = format2.load(tempPath);
    ASSERT_NE(loaded, nullptr);

    auto* interp = loaded->component("interposer");
    ASSERT_NE(interp, nullptr);
    ASSERT_TRUE(interp->attachment_surface_z().has_value());
    EXPECT_NEAR(interp->attachment_surface_z().value(), 13.83, 1e-6);
    EXPECT_NEAR(interp->dimensions().thickness, 300.0, 1e-6);  // body, untouched

    auto* u1 = loaded->component("U1");
    ASSERT_NE(u1, nullptr);
    EXPECT_FALSE(u1->attachment_surface_z().has_value());

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

// Component-level attachment_surface_z (the interposer die-attachment surface,
// decoupled from the physical body) wins over dimensions.thickness in the
// fallback mount computation. The two legacy tests above -- no
// attachment_surface_z set -- keep the thickness-as-mount behavior.
TEST(Assembly, CalculateZPrefersAttachmentSurfaceZOverThickness)
{
    Assembly assembly;
    auto interposer = std::make_unique<Component>("interp", ComponentType::Interposer);
    interposer->set_dimensions({1000, 1000, 300.0});  // physical body
    interposer->set_attachment_surface_z(13.83);      // die-attachment surface
    assembly.add_component(std::move(interposer));

    auto die = std::make_unique<Component>("die_no_connection", ComponentType::Die);
    die->set_dimensions({500, 500, 50});
    // No connection stack -> fallback path. It must mount on the attachment
    // surface (13.83), not the 300 um physical thickness.
    assembly.add_component(std::move(die));

    EXPECT_NEAR(assembly.calculate_component_z("die_no_connection"), 13.83, 1e-6);
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

    // CuPillar z_bottom (relative fragment 0.0 + declared
    // attachment_surface_z 13.83) + stack height (32 + 16 = 48) = 61.83.
    // If the fragment did not merge, CuPillar would be absent from the
    // body-less interposer stackup and the result would differ.
    EXPECT_NEAR(assembly->calculate_component_z("die_a"), 61.83, 0.01);

    // Negative control: without the adapter there is no merge, CuPillar is not
    // found, and the die falls back to interposer thickness (200) + stack (48).
    assembly->set_interconnect_adapter("");
    EXPECT_NEAR(assembly->calculate_component_z("die_a"), 248.0, 0.01);

    // Restore global state so later tests see the default configs resolution.
    unsetenv("INTERCONNECT_PDK_ROOT");
    BlenderGDSConfigs::setConfigsDir("");
}

// ---------------------------------------------------------------------
// F1 decoupling regression (interconnect_render_contract.md, L1): the
// fragment is method-pure (z relative to the attachment surface), so the
// SAME fragment must seat dies per whatever surface the interposer stackup
// declares. Same .chiplet, same PDK fragment, different interposer
// declaration -> different (correct) die z. Before F1 the fragment baked
// 13.83 and this was impossible without a fragment per interposer.
// ---------------------------------------------------------------------
TEST(InterconnectMergeZ, RelativeFragmentSeatsOnDeclaredSurface)
{
    const std::string base = fixturePath("interconnect_merge");
    BlenderGDSConfigs::setConfigsDir(base + "/configs_alt");
    setenv("INTERCONNECT_PDK_ROOT", (base + "/pdk").c_str(), 1);

    ChipletFormat format;
    auto assembly = format.load(base + "/merge_z.chiplet");
    ASSERT_NE(assembly, nullptr);

    // configs_alt declares attachment_surface_z 10.0: die z = 10 + 48 = 58.
    EXPECT_NEAR(assembly->calculate_component_z("die_a"), 58.0, 0.01);

    unsetenv("INTERCONNECT_PDK_ROOT");
    BlenderGDSConfigs::setConfigsDir("");
}

// Markerless fragments keep the legacy absolute interpretation (deprecation
// path): z is taken as-is, the base stackup's declared surface is ignored.
TEST(InterconnectMergeZ, LegacyAbsoluteFragmentKeepsAbsoluteZ)
{
    const std::string base = fixturePath("interconnect_merge");
    BlenderGDSConfigs::setConfigsDir(base + "/configs");
    setenv("INTERCONNECT_PDK_ROOT", (base + "/pdk_legacy").c_str(), 1);

    ChipletFormat format;
    auto assembly = format.load(base + "/merge_z.chiplet");
    ASSERT_NE(assembly, nullptr);

    // Legacy fragment bakes CuPillar z=13.83 absolute: 13.83 + 48 = 61.83,
    // even though the base also declares attachment_surface_z (13.83 here;
    // absolute values win for markerless fragments by definition).
    EXPECT_NEAR(assembly->calculate_component_z("die_a"), 61.83, 0.01);

    unsetenv("INTERCONNECT_PDK_ROOT");
    BlenderGDSConfigs::setConfigsDir("");
}

// ---------------------------------------------------------------------
// F2 per-die methods: each die seats on ITS OWN method's body stack (the
// die's connection id selects the fragment). The legacy adapter resolves
// to a decoy fragment with wrong elevations -- the policy must ignore it
// whenever any method id resolves.
// ---------------------------------------------------------------------
TEST(InterconnectMergeZ, PerDieMethodFragmentsSeatEachDie)
{
    const std::string base = fixturePath("interconnect_merge");
    BlenderGDSConfigs::setConfigsDir(base + "/configs");
    setenv("INTERCONNECT_PDK_ROOT", (base + "/pdk_methods").c_str(), 1);

    ChipletFormat format;
    auto assembly = format.load(base + "/mixed_methods.chiplet");
    ASSERT_NE(assembly, nullptr);

    // Methods derived from the dies' connections, sorted.
    const auto ids = assembly->interconnect_method_ids();
    ASSERT_EQ(ids.size(), 2u);
    EXPECT_EQ(ids[0], "method_a");
    EXPECT_EQ(ids[1], "method_b");

    // die_a: LayerA z (13.83 + 0) + stack 10 = 23.83
    // die_b: LayerB z (13.83 + 0) + stack 20 = 33.83
    // If the decoy adapter fragment merged, LayerA would sit at z=5 and
    // die_a would seat at 15.0 instead.
    EXPECT_NEAR(assembly->calculate_component_z("die_a"), 23.83, 0.01);
    EXPECT_NEAR(assembly->calculate_component_z("die_b"), 33.83, 0.01);

    unsetenv("INTERCONNECT_PDK_ROOT");
    BlenderGDSConfigs::setConfigsDir("");
}

// A relative fragment on a base stackup that does NOT declare its surface
// falls back to totalHeight() (loud, best-effort) instead of seating at 0.
TEST(InterconnectMergeZ, RelativeFragmentWithoutDeclaredSurfaceFallsBack)
{
    const std::string base = fixturePath("interconnect_merge");
    BlenderGDSConfigs::setConfigsDir(base + "/configs_nosurface");
    setenv("INTERCONNECT_PDK_ROOT", (base + "/pdk").c_str(), 1);

    ChipletFormat format;
    auto assembly = format.load(base + "/merge_z.chiplet");
    ASSERT_NE(assembly, nullptr);

    // totalHeight() of the body-less base = Passiv top 15.73; 15.73 + 48 =
    // 63.73. Approximate (the pad top is 13.83) but far better than z=48,
    // and the warning names the missing key.
    EXPECT_NEAR(assembly->calculate_component_z("die_a"), 63.73, 0.01);

    unsetenv("INTERCONNECT_PDK_ROOT");
    BlenderGDSConfigs::setConfigsDir("");
}

TEST(InterconnectMergeZ, StackupPathResolvesPlainProductIdOnly)
{
    // The IHP interposer stackup is keyed on the plain product id
    // (intm4tm2). Pre-rename spellings must NOT resolve to it.
    const std::string base = fixturePath("interconnect_merge");
    BlenderGDSConfigs::setConfigsDir(base + "/configs");

    const std::string expected = base + "/configs/stackups/intm4tm2.yaml";
    EXPECT_EQ(BlenderGDSConfigs::stackupPath("intm4tm2"), expected);
    EXPECT_EQ(BlenderGDSConfigs::stackupPath("IntM4TM2"), expected);
    EXPECT_NE(BlenderGDSConfigs::stackupPath("ihp-interposer"), expected);
    EXPECT_NE(BlenderGDSConfigs::stackupPath("interposer_tech"), expected);
    EXPECT_NE(BlenderGDSConfigs::stackupPath("rdl"), expected);

    BlenderGDSConfigs::setConfigsDir("");
}

// ---------------------------------------------------------------------
// ${VAR} ecosystem-root expansion in path entries (resolve_path).
// Hermetic: fake roots under a temp tree plus env vars; never depends on
// real sibling checkouts. Convention: environment -> sibling-checkout
// walk (anchored at the .chiplet dir) -> ChipletFormatException.
// Mirrors the Python reader (chiplet_kicad_plugin/hyp_to_gds.py).
// ---------------------------------------------------------------------

std::filesystem::path writeChipletWithLayout(const std::filesystem::path& dir,
                                             const std::string& layout)
{
    std::filesystem::create_directories(dir);
    std::filesystem::path file = dir / "pathvars.chiplet";
    std::ofstream out(file);
    out << "format_version: \"1.0\"\n"
        << "assembly:\n"
        << "  name: \"PathVars\"\n"
        << "  units: \"um\"\n"
        << "components:\n"
        << "  - id: die_a\n"
        << "    type: die\n"
        << "    layout: \"" << layout << "\"\n"
        << "    dimensions: { width: 100, height: 100, thickness: 10 }\n"
        << "    position: { x: 0, y: 0, z: 0 }\n";
    return file;
}

// Snapshot-and-restore guard for the env vars these tests mutate. The
// verify image bakes the ecosystem roots, so later env-gated tests in
// this binary must see them untouched regardless of test order.
class EnvVarGuard {
public:
    explicit EnvVarGuard(const char* name) : m_name(name) {
        if (const char* v = std::getenv(name)) {
            m_hadValue = true;
            m_value = v;
        }
    }
    ~EnvVarGuard() {
        if (m_hadValue) {
            setenv(m_name.c_str(), m_value.c_str(), 1);
        } else {
            unsetenv(m_name.c_str());
        }
    }
    EnvVarGuard(const EnvVarGuard&) = delete;
    EnvVarGuard& operator=(const EnvVarGuard&) = delete;

private:
    std::string m_name;
    std::string m_value;
    bool m_hadValue = false;
};

TEST(PathVars, EnvExpansionInLayout)
{
    namespace fs = std::filesystem;
    EnvVarGuard guard("INTERPOSER_PDK_ROOT");
    const fs::path base = fs::temp_directory_path() / "chiplet_pathvars_env";
    fs::remove_all(base);

    // Fake interposer PDK root carrying the marker subpath.
    const fs::path root = base / "custom_pdk";
    fs::create_directories(root / "libs.tech" / "klayout");
    setenv("INTERPOSER_PDK_ROOT", root.string().c_str(), 1);

    const fs::path file = writeChipletWithLayout(
        base / "proj", "${INTERPOSER_PDK_ROOT}/libs.tech/klayout/tech/x.gds");

    ChipletFormat format;
    auto assembly = format.load(file.string());
    ASSERT_NE(assembly, nullptr);
    auto die = assembly->component("die_a");
    ASSERT_NE(die, nullptr);
    EXPECT_EQ(die->layout_path(),
              (root / "libs.tech" / "klayout" / "tech" / "x.gds").string());

    fs::remove_all(base);
}

TEST(PathVars, WalkExpansionFromChipletDir)
{
    namespace fs = std::filesystem;
    EnvVarGuard guard("GDS_TO_KICAD_ROOT");
    const fs::path base = fs::temp_directory_path() / "chiplet_pathvars_walk";
    fs::remove_all(base);

    // Sibling checkout two levels above the dir holding the .chiplet.
    fs::create_directories(base / "gds_to_kicad" / "pdks");
    unsetenv("GDS_TO_KICAD_ROOT");

    const fs::path file = writeChipletWithLayout(
        base / "designs" / "proj", "${GDS_TO_KICAD_ROOT}/pdks/sg13g2.lyp");

    ChipletFormat format;
    auto assembly = format.load(file.string());
    ASSERT_NE(assembly, nullptr);
    auto die = assembly->component("die_a");
    ASSERT_NE(die, nullptr);
    EXPECT_EQ(die->layout_path(),
              (base / "gds_to_kicad" / "pdks" / "sg13g2.lyp").string());

    fs::remove_all(base);
}

TEST(PathVars, BogusEnvFallsThroughToWalk)
{
    namespace fs = std::filesystem;
    EnvVarGuard guard("GDS_TO_KICAD_ROOT");
    const fs::path base = fs::temp_directory_path() / "chiplet_pathvars_bogus";
    fs::remove_all(base);

    fs::create_directories(base / "gds_to_kicad" / "pdks");
    // Set-but-invalid env root (marker subpath missing) must fall through
    // to the walk instead of failing or resolving to the bogus root.
    setenv("GDS_TO_KICAD_ROOT", (base / "nonexistent").string().c_str(), 1);

    const fs::path file = writeChipletWithLayout(
        base / "proj", "${GDS_TO_KICAD_ROOT}/pdks/sg13g2.lyp");

    ChipletFormat format;
    auto assembly = format.load(file.string());
    ASSERT_NE(assembly, nullptr);
    auto die = assembly->component("die_a");
    ASSERT_NE(die, nullptr);
    EXPECT_EQ(die->layout_path(),
              (base / "gds_to_kicad" / "pdks" / "sg13g2.lyp").string());

    fs::remove_all(base);
}

TEST(PathVars, WalkAcceptsRepoNameAlias)
{
    namespace fs = std::filesystem;
    EnvVarGuard guard("GDS_TO_KICAD_ROOT");
    const fs::path base = fs::temp_directory_path() / "chiplet_pathvars_alias";
    fs::remove_all(base);

    // Default-clone layout: the sibling uses the GitHub repo name.
    fs::create_directories(base / "gds-to-kicad" / "pdks");
    unsetenv("GDS_TO_KICAD_ROOT");

    const fs::path file = writeChipletWithLayout(
        base / "designs" / "proj", "${GDS_TO_KICAD_ROOT}/pdks/sg13g2.lyp");

    ChipletFormat format;
    auto assembly = format.load(file.string());
    ASSERT_NE(assembly, nullptr);
    auto die = assembly->component("die_a");
    ASSERT_NE(die, nullptr);
    EXPECT_EQ(die->layout_path(),
              (base / "gds-to-kicad" / "pdks" / "sg13g2.lyp").string());

    fs::remove_all(base);
}

TEST(PathVars, WalkPrefersCanonicalOverAlias)
{
    namespace fs = std::filesystem;
    EnvVarGuard guard("GDS_TO_KICAD_ROOT");
    const fs::path base = fs::temp_directory_path() / "chiplet_pathvars_pref";
    fs::remove_all(base);

    // Both names present at the same ancestor: the canonical dir wins,
    // matching the Python walk order.
    fs::create_directories(base / "gds_to_kicad" / "pdks");
    fs::create_directories(base / "gds-to-kicad" / "pdks");
    unsetenv("GDS_TO_KICAD_ROOT");

    const fs::path file = writeChipletWithLayout(
        base / "proj", "${GDS_TO_KICAD_ROOT}/pdks/sg13g2.lyp");

    ChipletFormat format;
    auto assembly = format.load(file.string());
    ASSERT_NE(assembly, nullptr);
    auto die = assembly->component("die_a");
    ASSERT_NE(die, nullptr);
    EXPECT_EQ(die->layout_path(),
              (base / "gds_to_kicad" / "pdks" / "sg13g2.lyp").string());

    fs::remove_all(base);
}

TEST(PathVars, InterposerAliasOpenIntM4TM2)
{
    namespace fs = std::filesystem;
    EnvVarGuard guard("INTERPOSER_PDK_ROOT");
    const fs::path base = fs::temp_directory_path() / "chiplet_pathvars_intm4";
    fs::remove_all(base);

    // Interposer PDK checked out under its GitHub repo name.
    fs::create_directories(base / "OpenIntM4TM2" / "libs.tech" / "klayout");
    unsetenv("INTERPOSER_PDK_ROOT");

    const fs::path file = writeChipletWithLayout(
        base / "proj", "${INTERPOSER_PDK_ROOT}/libs.tech/klayout/tech/x.gds");

    ChipletFormat format;
    auto assembly = format.load(file.string());
    ASSERT_NE(assembly, nullptr);
    auto die = assembly->component("die_a");
    ASSERT_NE(die, nullptr);
    EXPECT_EQ(die->layout_path(),
              (base / "OpenIntM4TM2" / "libs.tech" / "klayout" / "tech"
               / "x.gds").string());

    fs::remove_all(base);
}

TEST(PathVars, PdkRootResolvesViaWalk)
{
    namespace fs = std::filesystem;
    EnvVarGuard guard("PDK_ROOT");
    const fs::path base = fs::temp_directory_path() / "chiplet_pathvars_pdk";
    fs::remove_all(base);

    // Base SG13G2 PDK sibling (standard IHP layout under the root).
    fs::create_directories(base / "IHP-Open-PDK" / "ihp-sg13g2" / "libs.tech"
                           / "klayout");
    unsetenv("PDK_ROOT");

    const fs::path file = writeChipletWithLayout(
        base / "proj",
        "${PDK_ROOT}/ihp-sg13g2/libs.tech/klayout/tech/sg13g2.lyt");

    ChipletFormat format;
    auto assembly = format.load(file.string());
    ASSERT_NE(assembly, nullptr);
    auto die = assembly->component("die_a");
    ASSERT_NE(die, nullptr);
    EXPECT_EQ(die->layout_path(),
              (base / "IHP-Open-PDK" / "ihp-sg13g2" / "libs.tech" / "klayout"
               / "tech" / "sg13g2.lyt").string());

    fs::remove_all(base);
}

TEST(PathVars, UnresolvableVarThrows)
{
    namespace fs = std::filesystem;
    const fs::path base = fs::temp_directory_path() / "chiplet_pathvars_bad";
    fs::remove_all(base);
    unsetenv("NO_SUCH_ECOSYSTEM_ROOT");

    const fs::path file = writeChipletWithLayout(
        base / "proj", "${NO_SUCH_ECOSYSTEM_ROOT}/a.gds");

    ChipletFormat format;
    try {
        format.load(file.string());
        FAIL() << "Expected ChipletFormatException";
    } catch (const ChipletFormatException& e) {
        EXPECT_NE(std::string(e.what()).find("NO_SUCH_ECOSYSTEM_ROOT"),
                  std::string::npos);
    }
    fs::remove_all(base);
}


// ---------------------------------------------------------------------------
// Scalar quoting on the wire (STUDIO-14)
// ---------------------------------------------------------------------------

namespace {

std::string readWholeFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

} // namespace

// STUDIO-14. Polarity is deliberate: this enumerates the field names the
// format declares NON-string and demands quotes on everything else, so a
// newly added string field defaults to "must be quoted" and forgetting to
// touch this test is RED, not silence. Enumerating the string fields instead
// would make every new field pass by default (META-4).
TEST(ChipletFormatWriter, EveryDeclaredStringScalarIsQuoted)
{
    // chiplet-spec chiplet.schema.json, every leaf typed number/integer/boolean.
    const std::vector<std::string> kNonStringFields = {
        "attachment_surface_z", "dbu", "diameter", "external",
        "finalize_required", "height", "pitch", "thickness", "width",
        "x", "y", "z",
    };

    const std::string src = fixturesPath() + "/quoting_roundtrip.chiplet";
    ChipletFormat fmt;
    auto assembly = fmt.load(src);
    ASSERT_TRUE(assembly);

    const std::string out =
        (std::filesystem::temp_directory_path() / "quoting_out.chiplet").string();
    fmt.save(*assembly, out);
    const std::string text = readWholeFile(out);
    ASSERT_FALSE(text.empty());

    std::istringstream in(text);
    std::string line;
    size_t lineNo = 0, checked = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        // `key: value` scalars only; skip block openers, flow maps and seqs.
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        const auto ks = key.find_first_not_of(" -");
        if (ks == std::string::npos) continue;
        key = key.substr(ks);
        if (key.find(' ') != std::string::npos) continue;
        std::string value = line.substr(colon + 1);
        const auto vs = value.find_first_not_of(' ');
        if (vs == std::string::npos) continue;   // block opener
        value = value.substr(vs);
        if (value[0] == '{' || value[0] == '[') continue;  // flow collection

        if (std::find(kNonStringFields.begin(), kNonStringFields.end(), key)
            != kNonStringFields.end()) {
            continue;
        }
        ++checked;
        EXPECT_EQ(value.front(), '"')
            << "line " << lineNo << ": `" << line << "`\n"
            << "  '" << key << "' is not one of the format's non-string fields, "
               "so it is a declared string and must be emitted quoted. Unquoted, "
               "YAML re-types it: 0755 reads back as 493 and 2026-03-22 as a "
               "date under PyYAML, while yaml-cpp returns strings, so the two "
               "readers disagree about the same bytes.";
    }
    EXPECT_GT(checked, 10u) << "the fixture exercised almost nothing";
    std::filesystem::remove(out);
}

// The values that actually re-type, end to end through save.
TEST(ChipletFormatWriter, NumericLookingIdentifiersSurviveASave)
{
    const std::string src = fixturesPath() + "/quoting_roundtrip.chiplet";
    ChipletFormat fmt;
    auto assembly = fmt.load(src);
    ASSERT_TRUE(assembly);
    const std::string out =
        (std::filesystem::temp_directory_path() / "quoting_types.chiplet").string();
    fmt.save(*assembly, out);

    // Re-read with a plain YAML load (not our reader): this is the view the
    // PyYAML-based consumers in the ecosystem get.
    YAML::Node root = YAML::LoadFile(out);
    EXPECT_EQ(root["format_version"].Tag(), "!")
        << "format_version must be an explicitly quoted string; unquoted it is "
           "a schema negative in chiplet-spec (fixture v1_0_unquoted_numeric)";
    EXPECT_EQ(root["assembly"]["created"].as<std::string>(), "2026-03-22");
    EXPECT_EQ(root["components"][0]["id"].as<std::string>(), "0755");
    EXPECT_EQ(root["components"][0]["top_cell"].as<std::string>(), "1.10");
    std::filesystem::remove(out);
}

} // namespace
} // namespace chiplet