/**
 * test_chiplet_format.cpp - Unit tests for ChipletFormat parser
 */

#include <gtest/gtest.h>
#include <QtGlobal>
#include <QString>
#include <QCryptographicHash>
#include <QTemporaryDir>
#include <sstream>
#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#include <yaml-cpp/yaml.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/resource.h>
#include <csignal>
#include <fcntl.h>
#include <thread>
#include <atomic>
#include <chrono>
#include "formats/ChipletFormat.h"
#include "formats/TopLevelKeyIntent.h"
#include "ui/AutoSaveState.h"
#include <chiplet_format_io/chiplet_format_io.hpp>
#include "core/DecimalText.h"
#include "core/LayerStackup.h"
#include "core/Technology.h"
#include "core/flow/FlowDefinition.h"
#include "core/flow/FlowStep.h"
#include <memory>
#include <algorithm>
#include <dlfcn.h>
#include <cstring>
#include <cerrno>
#include <iostream>

// A deterministic seam at the one instant a test cannot otherwise reach.
//
// save_snapshot with an empty request publishes with ::link, which fails EEXIST
// if anything is at the name. That is what closes the interval between the last
// admission check and the write: the freshness question and the write become
// one operation. Nothing outside can place a file in that interval, because it
// is a few instructions wide and has no seam in it, so this defines the syscall
// itself. glibc's link lives in a shared object, so a definition in this binary
// interposes for every call in it, including the one under test, and forwards
// to the real implementation through dlsym(RTLD_NEXT).
//
// It costs the program nothing: no hook in the writer, no callback parameter,
// no build flag, and nothing about this exists outside this file. Armed for one
// call on one path it creates the target and then lets the real link run, so
// the EEXIST the writer sees is a real one, from a real file, that appeared at
// exactly the right moment. Disarmed, which is its state for every other test
// in this binary, it is a pass-through. Arm it only through SeamArmed below,
// which disarms on the way out however the test leaves.
struct ChipletLinkSeam {
    std::atomic<bool> armed{false};
    std::atomic<int> fired{0};
    std::string path;
    bool dangling = false;
};

inline ChipletLinkSeam& chipletLinkSeam()
{
    static ChipletLinkSeam seam;
    return seam;
}

inline const char* chipletSeamBytes()
{
    return "# placed between the last check and the exclusive create\n";
}

extern "C" int link(const char* oldPath, const char* newPath) noexcept
{
    using RealLink = int (*)(const char*, const char*);
    static RealLink real = reinterpret_cast<RealLink>(::dlsym(RTLD_NEXT, "link"));

    ChipletLinkSeam& seam = chipletLinkSeam();
    if (seam.armed.load() && newPath != nullptr && seam.path == newPath) {
        seam.armed.store(false);   // one call, one path
        seam.fired.fetch_add(1);
        if (seam.dangling) {
            (void)::symlink("chiplet-seam-target-that-does-not-exist", newPath);
        } else {
            const int fd = ::open(newPath, O_CREAT | O_EXCL | O_WRONLY, 0600);
            if (fd >= 0) {
                const char* bytes = chipletSeamBytes();
                const ssize_t n = ::write(fd, bytes, std::strlen(bytes));
                (void)n;
                ::close(fd);
            }
        }
    }
    if (real == nullptr) {
        // Reported rather than papered over: a null next-link would otherwise
        // surface as an unexplained save failure a long way from its cause.
        errno = ENOSYS;
        return -1;
    }
    return real(oldPath, newPath);
}

namespace chiplet {
namespace {

// Arms the seam for one path and disarms it however the test leaves, so a
// whole-binary interposition cannot be inherited by whatever runs next.
class SeamArmed {
public:
    SeamArmed(const std::string& path, bool dangling)
    {
        auto& seam = chipletLinkSeam();
        m_before = seam.fired.load();
        seam.path = path;
        seam.dangling = dangling;
        seam.armed.store(true);
    }
    ~SeamArmed()
    {
        auto& seam = chipletLinkSeam();
        seam.armed.store(false);
        seam.path.clear();
    }
    SeamArmed(const SeamArmed&) = delete;
    SeamArmed& operator=(const SeamArmed&) = delete;
    int firedSince() const { return chipletLinkSeam().fired.load() - m_before; }

private:
    int m_before = 0;
};

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
    const Technology* tech = assembly->technology("vendorx_microbump");
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
    const Technology* tech = assembly2->technology("vendorx_microbump");
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
    // interposer thickness=10.8103, cupillar total=44.0 -> z=54.8103
    auto die_a = assembly->component("die_a");
    ASSERT_NE(die_a, nullptr);
    EXPECT_DOUBLE_EQ(die_a->position().z, 54.8103);

    // die_b has connection=sbump_sac305, z was 0 -> auto-calculated
    // interposer thickness=10.8103, sbump total=80.0 -> z=90.8103
    auto die_b = assembly->component("die_b");
    ASSERT_NE(die_b, nullptr);
    EXPECT_DOUBLE_EQ(die_b->position().z, 90.8103);

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
    EXPECT_DOUBLE_EQ(die_a->position().z, 54.8103);

    // Cleanup
    std::filesystem::remove(tempPath);
}

// A z derived from the plane is written in the shortest text that reads back
// as it, the way the contract writes it: 10.8103 + 44 goes out as 54.8103,
// where 17 significant digits give 54.810299999999998. Any other double keeps
// yaml-cpp's text.
TEST(ChipletFormat, APlaneDerivedZIsWrittenAsItsDecimal)
{
    ChipletFormat format;
    auto assembly = format.load(fixturePath("with_connection_stacks.chiplet"));
    ASSERT_NE(assembly, nullptr);

    Component* interposer = assembly->component("interposer");
    ASSERT_NE(interposer, nullptr);
    interposer->set_attachment_surface_z(10.8103);
    Component* die_c = assembly->component("die_c");
    ASSERT_NE(die_c, nullptr);
    Position3D p = die_c->position();
    p.x = 7.8103;  // not derived from the plane: written as today
    die_c->set_position(p);

    const std::string tempPath = "test_plane_derived_z.chiplet";
    ASSERT_NO_THROW(format.save(*assembly, tempPath));
    std::ifstream in(tempPath, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    std::filesystem::remove(tempPath);

    EXPECT_NE(text.find("position: {x: 1000, y: 1000, z: 54.8103}\n"), std::string::npos)
        << text;
    EXPECT_NE(text.find("position: {x: 4000, y: 1000, z: 90.8103}\n"), std::string::npos)
        << text;
    EXPECT_NE(text.find("attachment_surface_z: 10.8103\n"), std::string::npos) << text;
    // die_c is the file's last line, and nothing follows it.
    EXPECT_NE(text.find("position: {x: 7.8102999999999998, y: 1000, z: 100}"),
              std::string::npos)
        << text;
}

// The seated z is summed as decimals. 10.8103 + 18 as doubles is
// 28.810299999999998, which no decimal text would round back to.
TEST(DecimalText, APlaneAndAHeightSumToTheDecimal)
{
    ASSERT_NE(10.8103 + 18.0, 28.8103);  // the noise the sum removes
    EXPECT_EQ(decimalSum(10.8103, 18.0), 28.8103);
    EXPECT_EQ(shortestText(decimalSum(10.8103, 18.0)), "28.8103");
    EXPECT_EQ(decimalSum(10.8103, 44.0), 54.8103);
    EXPECT_EQ(decimalSum(2.16, 0.8503), 3.0103);  // TopVia1 top, TopMetal1 bottom
    EXPECT_EQ(decimalSum(0.1, 0.2), 0.3);
    // A height with more decimals than the plane keeps all of them.
    ASSERT_NE(10.8103 + 18.12345, 28.93375);
    EXPECT_EQ(decimalSum(10.8103, 18.12345), 28.93375);
    EXPECT_EQ(shortestText(decimalSum(10.8103, 18.12345)), "28.93375");
    EXPECT_EQ(decimalPlaces(18.12345), 5);
    EXPECT_EQ(decimalPlaces(10.8103), 4);
    EXPECT_EQ(decimalPlaces(48.0), 0);
    EXPECT_EQ(decimalPlaces(0.1 + 0.2), 1);
    EXPECT_EQ(shortestText(54.8103), "54.8103");
    EXPECT_EQ(shortestText(248.0), "248");
}

// Seated through the stack: a die on a 10.8103 plane under an 18 um stack
// lands on 28.8103 exactly, not on the double sum.
TEST(Assembly, CalculateZSumsThePlaneAndTheStackAsDecimals)
{
    Assembly assembly;
    auto interposer = std::make_unique<Component>("interp", ComponentType::Interposer);
    interposer->set_dimensions({1000, 1000, 300.0});
    interposer->set_attachment_surface_z(10.8103);
    assembly.add_component(std::move(interposer));

    ConnectionStack stack;
    stack.id = "bump18";
    ConnectionStackLayer layer;
    layer.name = "Bump";
    layer.height = 18.0;
    stack.layers.push_back(layer);
    assembly.add_connection_stack(stack);

    auto die = std::make_unique<Component>("die", ComponentType::Die);
    die->set_connection("bump18");
    assembly.add_component(std::move(die));

    EXPECT_EQ(assembly.calculate_component_z("die"), 28.8103);
}

// A stack whose height has five decimals, more than the plane's four: the die
// seats on the exact decimal sum, not on the plane's decimal places.
TEST(Assembly, CalculateZKeepsEveryDecimalOfTheHeight)
{
    Assembly assembly;
    auto interposer = std::make_unique<Component>("interp", ComponentType::Interposer);
    interposer->set_dimensions({1000, 1000, 300.0});
    interposer->set_attachment_surface_z(10.8103);
    assembly.add_component(std::move(interposer));

    ConnectionStack stack;
    stack.id = "bump5";
    ConnectionStackLayer layer;
    layer.name = "Bump";
    layer.height = 18.12345;
    stack.layers.push_back(layer);
    assembly.add_connection_stack(stack);

    auto die = std::make_unique<Component>("die", ComponentType::Die);
    die->set_connection("bump5");
    assembly.add_component(std::move(die));

    EXPECT_EQ(assembly.calculate_component_z("die"), 28.93375);
    EXPECT_EQ(shortestText(assembly.calculate_component_z("die")), "28.93375");
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

    // Size before index. Without this a future regression turns a red test into
    // a crash, and a crash is a worse diagnosis than a failure: it does not say
    // which assertion fell.
    ASSERT_GE(def2.steps.size(), 2u);

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
// coord_frame_contract.md: the schema plumbing
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
    interposer->set_attachment_surface_z(10.8103);
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
    EXPECT_DOUBLE_EQ(interp->attachment_surface_z().value(), 10.8103);
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
// Reader updates (coord_frame_contract.md §3.4 / §5.5)
//
// Assembly::calculate_component_z must always land a die on a sane
// surface. Before these reader updates it returned 0.0 for any die without
// a connection or with an undefined connection_stack; the contract
// calls that out as a footgun (the formula must hold in all cases).
// ---------------------------------------------------------------------

TEST(Assembly, CalculateZFallsBackToInterposerThicknessWhenConnectionMissing)
{
    Assembly assembly;
    auto interposer = std::make_unique<Component>("interp", ComponentType::Interposer);
    interposer->set_dimensions({1000, 1000, 10.8103});
    assembly.add_component(std::move(interposer));

    auto die = std::make_unique<Component>("die_no_connection", ComponentType::Die);
    die->set_dimensions({500, 500, 50});
    // Deliberately no set_connection() call.
    assembly.add_component(std::move(die));

    double z = assembly.calculate_component_z("die_no_connection");
    EXPECT_DOUBLE_EQ(z, 10.8103);
}

TEST(Assembly, CalculateZFallsBackWhenConnectionStackUndefined)
{
    Assembly assembly;
    auto interposer = std::make_unique<Component>("interp", ComponentType::Interposer);
    interposer->set_dimensions({1000, 1000, 10.8103});
    assembly.add_component(std::move(interposer));

    auto die = std::make_unique<Component>("die_undefined_stack", ComponentType::Die);
    die->set_connection("nonexistent_stack");
    die->set_dimensions({500, 500, 50});
    assembly.add_component(std::move(die));

    double z = assembly.calculate_component_z("die_undefined_stack");
    EXPECT_DOUBLE_EQ(z, 10.8103);
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
    interposer->set_attachment_surface_z(10.8103);    // die-attachment surface
    assembly.add_component(std::move(interposer));

    auto die = std::make_unique<Component>("die_no_connection", ComponentType::Die);
    die->set_dimensions({500, 500, 50});
    // No connection stack -> fallback path. It must mount on the attachment
    // surface (10.8103), not the 300 um physical thickness.
    assembly.add_component(std::move(die));

    EXPECT_DOUBLE_EQ(assembly.calculate_component_z("die_no_connection"), 10.8103);
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
    // earlier behavior we keep -- it is genuinely degenerate.
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

// Path variables. Expansion was MORE lax with names nobody had vetoed than
// with names that had been. A name in the table had to point at a directory
// carrying that root's marker subpath; a name absent from the table skipped the
// marker check entirely, and its environment value was substituted raw. So
// ${PDK_ROOT} was validated and any name a document cared to invent, as long as
// the host happened to have it set, was pasted into a layout path unchecked.
//
// UnresolvableVarThrows above does NOT discriminate this: it leaves the unknown
// name unset, and an unset unknown name was refused before the fix too, by a
// different branch with a different message. The variable being SET is the
// whole difference between the two behaviours, which is why this test sets it
// to a directory that really exists. The value is not the problem; the NAME is.
TEST(PathVars, AnUnknownNameIsRefusedEvenWhenTheEnvironmentDefinesIt)
{
    namespace fs = std::filesystem;
    EnvVarGuard guard("PATHVAR_INVENTED_ROOT");
    const fs::path base = fs::temp_directory_path() / "chiplet_pathvars_unknown";
    fs::remove_all(base);

    const fs::path root = base / "anything";
    fs::create_directories(root);
    setenv("PATHVAR_INVENTED_ROOT", root.string().c_str(), 1);

    const fs::path file = writeChipletWithLayout(
        base / "proj", "${PATHVAR_INVENTED_ROOT}/a.gds");

    ChipletFormat format;
    try {
        auto assembly = format.load(file.string());
        ADD_FAILURE()
            << "the environment value was substituted for a name the table "
               "never vetted; layout resolved to "
            << (assembly && assembly->component("die_a")
                    ? assembly->component("die_a")->layout_path()
                    : std::string("<no component>"));
    } catch (const ChipletFormatException& e) {
        const std::string msg = e.what();
        EXPECT_NE(msg.find("PATHVAR_INVENTED_ROOT"), std::string::npos) << msg;
        // The refusal has to name what CAN appear, or an author has no way to
        // tell a typo from a name this host will never accept.
        EXPECT_NE(msg.find("GDS_TO_KICAD_ROOT"), std::string::npos)
            << "the refusal does not name the accepted vocabulary: " << msg;
    }
    fs::remove_all(base);
}


// ---------------------------------------------------------------------------
// Adapter id contract (validate at LOAD) and scalar quoting
// ---------------------------------------------------------------------------

namespace {

std::string readWholeFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

} // namespace

// The oracle is chiplet-system-spec's, vendored verbatim. Assert its hash
// first: a case "fixed" by editing the local copy instead of the implementation
// is the failure this guards against, and without the hash it would pass
// silently.
TEST(AdapterId, ParityWithSpecOracle)
{
    const std::string path = fixturesPath() + "/adapter_id_cases.json";
    const std::string text = readWholeFile(path);
    ASSERT_FALSE(text.empty()) << "vendored oracle missing: " << path;

    const QByteArray digest =
        QCryptographicHash::hash(QByteArray(text.data(),
                                            static_cast<int>(text.size())),
                                 QCryptographicHash::Sha256).toHex();
    EXPECT_EQ(digest.toStdString(),
              "0364a6f2a2da84f9651af8283b1c456638e6897d174d105f271ed46ddfb80eb1")
        << "tests/fixtures/adapter_id_cases.json is not the copy declared in "
           "its PROVENANCE.md. Do not edit it here; change it upstream in "
           "chiplet-system-spec and re-copy.";

    // Parsed as YAML: JSON is a YAML subset and yaml-cpp is already a
    // dependency, so this needs no new JSON library.
    YAML::Node cases = YAML::Load(text);
    ASSERT_TRUE(cases["accept"] && cases["accept"].IsSequence());
    ASSERT_TRUE(cases["reject"] && cases["reject"].IsSequence());

    // Iterate the file; never restate the counts in prose or in a literal.
    size_t nAccept = 0, nReject = 0;
    for (const auto& n : cases["accept"]) {
        const auto id = n.as<std::string>();
        EXPECT_TRUE(is_valid_adapter_id(id))
            << "schema ACCEPTS this id, we reject it: " << id;
        ++nAccept;
    }
    for (const auto& n : cases["reject"]) {
        const auto id = n.as<std::string>();
        EXPECT_FALSE(is_valid_adapter_id(id))
            << "schema REJECTS this id, we accept it: " << id;
        ++nReject;
    }
    EXPECT_GT(nAccept, 0u);
    EXPECT_GT(nReject, 0u);
}

// Prove the check can fail. Without this, an implementation that
// returned true unconditionally would still pass every accept case, and the
// reject loop is the only thing standing between a document and a path.
TEST(AdapterId, RejectsTheDeckSuffixTheBarePatternWouldAccept)
{
    EXPECT_TRUE(is_valid_adapter_id("ihp_cupillar"));
    EXPECT_TRUE(is_valid_adapter_id("x.drcx"));   // only a .drc SUFFIX is out
    EXPECT_FALSE(is_valid_adapter_id("evil.drc")) // the pattern alone accepts this
        << "the id pattern without the schema's .drc negative accepts a deck "
           "name; both halves of the contract are required";
    EXPECT_FALSE(is_valid_adapter_id("intm4tm2\n"))
        << "a trailing newline must not pass: this is the \\Z-vs-$ trap, and "
           "regex_match is what closes it in C++";
}

// A hostile adapter id must cost the interconnect axis, not the document.
TEST(AdapterId, HostileAdapterIsDroppedAndTheDocumentStillLoads)
{
    const std::string path =
        (std::filesystem::temp_directory_path() / "hostile_adapter.chiplet").string();
    {
        std::ofstream f(path);
        f << "format_version: \"1.0\"\n"
             "assembly:\n  name: \"victim\"\n  units: \"um\"\n"
             "interconnect:\n  adapter: \"evil.drc\"\n"
             "components:\n"
             "  - id: \"die0\"\n    type: die\n    anchor: gds_origin\n"
             "    dimensions: { width: 10, height: 10, thickness: 1 }\n";
    }
    ChipletFormat fmt;
    std::unique_ptr<Assembly> asm1;
    ASSERT_NO_THROW(asm1 = fmt.load(path));
    ASSERT_TRUE(asm1);
    EXPECT_TRUE(asm1->interconnect_adapter().empty())
        << "a path-shaped adapter id reached the assembly";
    EXPECT_EQ(asm1->components().size(), 1u)
        << "the rest of the document must still load";
    std::filesystem::remove(path);
}

// Scalar quoting. Polarity is deliberate: this enumerates the field names the
// format declares NON-string and demands quotes on everything else, so a
// newly added string field defaults to "must be quoted" and forgetting to
// touch this test is RED, not silence. Enumerating the string fields instead
// would make every new field pass by default.
//
// chiplet-system-spec chiplet.schema.json, every leaf typed number/integer/boolean.
//
// ONE list, used by the quoting-coverage test and by the separator sweep below.
// Polarity is deliberate: a field added to the format and forgotten
// here is treated as a string, which is the noisy side.
const std::vector<std::string>& nonStringFieldNames()
{
    static const std::vector<std::string> kFields = {
        "attachment_surface_z", "dbu", "diameter", "external",
        "finalize_required", "height", "pitch", "thickness", "width",
        "x", "y", "z",
    };
    return kFields;
}

TEST(ChipletFormatWriter, EveryDeclaredStringScalarIsQuoted)
{
    const std::vector<std::string>& kNonStringFields = nonStringFieldNames();

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
           "a schema negative in chiplet-system-spec "
           "(fixture v1_0_unquoted_numeric)";
    EXPECT_EQ(root["assembly"]["created"].as<std::string>(), "2026-03-22");
    EXPECT_EQ(root["components"][0]["id"].as<std::string>(), "0755");
    EXPECT_EQ(root["components"][0]["top_cell"].as<std::string>(), "1.10");
    std::filesystem::remove(out);
}


// --- save() must not destroy the file it fails to write ---
//
// These four do NOT all prove the same thing, and it is worth being exact about
// which does what, because a green block reads as if it did.
//
// Run against the OLD writer (`std::ofstream file(path); file << text;`, no
// check of the stream), exactly ONE of them goes red:
// FailedSaveLeavesTheOriginalIntact. That is the one that demonstrates the
// defect; it found the old code writing "Name That Must Never Land" into a file
// it had just truncated inside a directory the user could not write.
//
// The other three PASS against the old writer too, and that is not a flaw in
// them: the old writer had no temporary file, so it could not leak one, could
// not lose a symlink to a rename, and could not stamp mkstemp's 0600 on a saved
// file. They are regression guards for the NEW implementation - each names a
// way the atomic-replace shape can quietly go wrong - and they only ever go red
// if this writer breaks. Do not cite them as evidence that the bug existed.

// A save that cannot complete leaves the previous contents in place.
//
// The failure is induced by making the directory unwritable, which is also the
// case the old writer got WRONG in the most dangerous direction: opening an
// existing writable file for truncation needs no write permission on its
// directory, so the old code happily truncated and reported success. The new
// writer cannot create its temp file there and refuses, which is the intended
// behaviour change: an atomic replace needs a writable directory, and failing
// loudly beats truncating quietly.
TEST(AtomicSave, FailedSaveLeavesTheOriginalIntact)
{
    if (::geteuid() == 0) {
        GTEST_SKIP() << "running as root: directory permissions are not enforced";
    }

    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "chiplet_atomic_save_fail";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::string target = (dir / "design.chiplet").string();

    ChipletFormat format;
    auto assembly = format.load(fixturePath("minimal.chiplet"));

    assembly->set_name("Original Name");
    format.save(*assembly, target);
    const std::string before = readWholeFile(target);
    ASSERT_NE(before.find("Original Name"), std::string::npos);

    // The second save carries DIFFERENT content, so "unchanged" below is a
    // real assertion and not a tautology about saving the same bytes twice.
    assembly->set_name("Name That Must Never Land");
    ASSERT_EQ(::chmod(dir.c_str(), 0500), 0);

    EXPECT_THROW(format.save(*assembly, target), ChipletFormatException);

    ASSERT_EQ(::chmod(dir.c_str(), 0700), 0);
    const std::string after = readWholeFile(target);
    EXPECT_EQ(after, before);
    EXPECT_EQ(after.find("Name That Must Never Land"), std::string::npos);

    std::filesystem::remove_all(dir);
}

// mkstemp creates 0600. Without an explicit fchmod every saved file would
// silently become owner-only, which no user asked for and nothing would report.
// A write that fails PART WAY THROUGH, which the test above does not reach.
//
// FailedSaveLeavesTheOriginalIntact takes away permission on the directory, so
// it fails at mkstemp, before a byte is written. The interesting case is the
// other one: the temp file is created, some of the document lands, and then the
// disk fills. That is the shape that destroys a design under a writer that
// truncates the target first, and it is protected here by construction rather
// than by evidence, which is a different thing from being protected.
//
// RLIMIT_FSIZE reproduces it without needing a full disk: the limit applies to
// the temp file the same as to any other, so mkstemp succeeds at zero bytes and
// the write then fails with EFBIG. SIGXFSZ has to be ignored around it, because
// its default action is to kill the process, which would look like a crash in
// this code rather than the refusal being asserted.
TEST(AtomicSave, AWriteThatFailsPartWayThroughLeavesNothingBehind)
{
    struct rlimit original {};
    if (::getrlimit(RLIMIT_FSIZE, &original) != 0) {
        GTEST_SKIP() << "cannot read RLIMIT_FSIZE on this platform";
    }

    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "chiplet_atomic_save_partial";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::string target = (dir / "design.chiplet").string();

    ChipletFormat format;
    auto assembly = format.load(fixturePath("minimal.chiplet"));
    assembly->set_name("Original Name");
    format.save(*assembly, target);
    const std::string before = readWholeFile(target);
    ASSERT_NE(before.find("Original Name"), std::string::npos);
    ASSERT_GT(before.size(), 64u) << "the document must be larger than the cap below";

    assembly->set_name("Name That Must Never Land");

    // 64 bytes: past mkstemp, short of the document. Restored below whatever
    // happens, along with the signal disposition.
    struct rlimit capped = original;
    capped.rlim_cur = 64;
    if (::setrlimit(RLIMIT_FSIZE, &capped) != 0) {
        std::filesystem::remove_all(dir);
        GTEST_SKIP() << "cannot lower RLIMIT_FSIZE here";
    }
    void (*previousSigxfsz)(int) = ::signal(SIGXFSZ, SIG_IGN);

    bool threw = false;
    try {
        format.save(*assembly, target);
    } catch (const ChipletFormatException&) {
        threw = true;
    }

    ::signal(SIGXFSZ, previousSigxfsz);
    ASSERT_EQ(::setrlimit(RLIMIT_FSIZE, &original), 0)
        << "the file-size limit was not restored; later tests would fail oddly";

    EXPECT_TRUE(threw) << "a partial write reported success";
    EXPECT_EQ(readWholeFile(target), before)
        << "the original was damaged by a write that never completed";

    // The half-written temp file must not survive either. A directory that
    // accumulates .chiplet-save-XXXXXX files after every full disk is its own
    // small defect, and it is the one a user would actually notice.
    size_t leftovers = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().filename().string().rfind(".chiplet-save-", 0) == 0) {
            ++leftovers;
        }
    }
    EXPECT_EQ(leftovers, 0u) << "a temporary file was left in the user's directory";

    std::filesystem::remove_all(dir);
}

TEST(AtomicSave, PreservesTheModeOfTheFileItReplaces)
{
    if (::geteuid() == 0) {
        GTEST_SKIP() << "running as root: umask/mode assertions are unreliable";
    }

    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "chiplet_atomic_save_mode";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::string target = (dir / "design.chiplet").string();

    ChipletFormat format;
    auto assembly = format.load(fixturePath("minimal.chiplet"));
    format.save(*assembly, target);
    ASSERT_EQ(::chmod(target.c_str(), 0640), 0);

    assembly->set_name("Second Save");
    format.save(*assembly, target);

    struct ::stat st{};
    ASSERT_EQ(::stat(target.c_str(), &st), 0);
    EXPECT_EQ(st.st_mode & 07777, 0640u)
        << "the replaced file kept mkstemp's 0600 instead of the mode it had";

    std::filesystem::remove_all(dir);
}

// ofstream follows a symlink; rename(2) replaces it. Saving through a link
// must write the file the link points at, not turn the link into a regular
// file, or a project layout that symlinks its .chiplet quietly loses the link.
TEST(AtomicSave, WritesThroughASymlinkInsteadOfReplacingIt)
{
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "chiplet_atomic_save_symlink";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::filesystem::path real = dir / "real.chiplet";
    const std::filesystem::path link = dir / "link.chiplet";

    ChipletFormat format;
    auto assembly = format.load(fixturePath("minimal.chiplet"));
    format.save(*assembly, real.string());
    std::filesystem::create_symlink(real, link);

    assembly->set_name("Through The Link");
    format.save(*assembly, link.string());

    EXPECT_TRUE(std::filesystem::is_symlink(link))
        << "the save replaced the symlink with a regular file";
    EXPECT_NE(readWholeFile(real.string()).find("Through The Link"),
              std::string::npos)
        << "the save did not reach the file the symlink points at";

    std::filesystem::remove_all(dir);
}

// A successful save must not leave its scratch file next to the design.
TEST(AtomicSave, LeavesNoTemporaryFileBehind)
{
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "chiplet_atomic_save_tmp";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    ChipletFormat format;
    auto assembly = format.load(fixturePath("minimal.chiplet"));
    format.save(*assembly, (dir / "design.chiplet").string());

    std::vector<std::string> left;
    for (const auto& e : std::filesystem::directory_iterator(dir)) {
        left.push_back(e.path().filename().string());
    }
    ASSERT_EQ(left.size(), 1u) << "expected only design.chiplet, found "
                               << left.size() << " entries";
    EXPECT_EQ(left[0], "design.chiplet");

    std::filesystem::remove_all(dir);
}


// --- The writer must never put a raw Unicode separator on disk ---
//
// U+2028 / U+2029 / U+0085 are line breaks in YAML 1.1 and not in YAML 1.2, so
// emitted raw they make PyYAML and yaml-cpp disagree about the STRUCTURE of the
// same bytes. chiplet-system-spec rejects them raw at the text level. The
// studio used to emit them raw, which meant opening a correctly escaped
// document and saving it produced a file that would not load: we downgraded
// good input into broken output, silently, and the round-trip still looked fine
// because our own reader accepted the raw bytes.
namespace {

constexpr const char* kLS = "\xe2\x80\xa8";   // U+2028
constexpr const char* kPS = "\xe2\x80\xa9";   // U+2029
constexpr const char* kNEL = "\xc2\x85";      // U+0085

// An assembly whose every writer path carries a separator: the quoted side
// (qs) and the flow block's plain side (ps), including a flow-block KEY, which
// is the shape that injects a second top-level key into a YAML 1.1 reader.
std::unique_ptr<Assembly> assemblyWithSeparatorsEverywhere()
{
    ChipletFormat format;
    auto assembly = format.load(fixturePath("minimal.chiplet"));
    assembly->set_name(std::string("name") + kLS + "after");
    assembly->set_description(std::string("desc") + kPS + "after");

    FlowDefinition def;
    def.working_directory = std::string("/tmp/wd") + kLS + "x";
    def.environment[std::string("ENV") + kLS + "KEY"] =
        std::string("val") + kNEL + "ue";
    FlowStep step;
    step.id = "step1";
    step.name = std::string("step") + kLS + "name";
    step.tool_path = "/bin/true";
    step.args = {"clean", std::string("dir") + kPS + "ty"};
    def.steps.push_back(step);
    assembly->set_flow_definition(def);
    return assembly;
}

} // namespace

// Of these four, exactly TWO go red against the pre-escaping writer, and it is
// worth knowing which: this one and FlowScalarsCarryingASeparatorAreQuoted.
// Verified by neutering the predicate and re-running, not assumed.
//
// RED against the pre-escaping writer: it emitted all of these raw.
TEST(SeparatorEscaping, SavedDocumentHasNoRawSeparatorBytes)
{
    auto assembly = assemblyWithSeparatorsEverywhere();
    const std::string out =
        (std::filesystem::temp_directory_path() / "sep_escaping.chiplet").string();
    ChipletFormat format;
    format.save(*assembly, out);

    const std::string text = readWholeFile(out);
    EXPECT_EQ(text.find(kLS), std::string::npos) << "raw U+2028 reached the file";
    EXPECT_EQ(text.find(kPS), std::string::npos) << "raw U+2029 reached the file";
    // U+0085 is escaped by yaml-cpp on its own today. Asserted anyway so a
    // library change that stops doing it fails here instead of shipping.
    EXPECT_EQ(text.find(kNEL), std::string::npos) << "raw U+0085 reached the file";

    std::filesystem::remove(out);
}

// RED against the pre-escaping writer for a different reason than the test
// above: there the bytes were raw, here the flow scalar was PLAIN, which is
// the shape that has no escape syntax at all and lets a separator start what
// PyYAML reads as a new top-level key.
TEST(SeparatorEscaping, FlowScalarsCarryingASeparatorAreQuoted)
{
    auto assembly = assemblyWithSeparatorsEverywhere();
    const std::string out =
        (std::filesystem::temp_directory_path() / "sep_flow_quoted.chiplet").string();
    ChipletFormat format;
    format.save(*assembly, out);

    YAML::Node root = YAML::LoadFile(out);
    ASSERT_TRUE(root["flow"]) << "the fixture must actually emit a flow block";
    EXPECT_EQ(root["flow"]["steps"][0]["name"].Tag(), "!")
        << "a flow step name carrying U+2028 was emitted as a plain scalar";
    EXPECT_EQ(root["flow"]["working_directory"].Tag(), "!")
        << "a flow working_directory carrying U+2028 was emitted as a plain scalar";

    bool sawQuotedEnvKey = false;
    for (auto it = root["flow"]["environment"].begin();
         it != root["flow"]["environment"].end(); ++it) {
        if (it->first.Tag() == "!") {
            sawQuotedEnvKey = true;
        }
    }
    EXPECT_TRUE(sawQuotedEnvKey)
        << "the environment KEY carrying U+2028 was emitted as a plain scalar; "
           "that is the shape that injects a second top-level key in YAML 1.1";

    std::filesystem::remove(out);
}

// Guard, not evidence. Measured: this passes against the pre-escaping writer
// too, because that writer emitted the separator raw and OUR reader accepts a
// raw separator, so the value round-tripped perfectly well. That is exactly
// why the defect went unnoticed for so long, and it is why this test cannot be
// cited as proof the bug existed.
//
// What it does buy: the two tests above could both be satisfied by DROPPING
// the character instead of escaping it. This one forbids that, so it stays.
TEST(SeparatorEscaping, SeparatorSurvivesSaveAndLoad)
{
    auto assembly = assemblyWithSeparatorsEverywhere();
    const std::string wantName = assembly->name();
    const std::string wantDesc = assembly->description();
    const std::string wantWd = assembly->flow_definition().working_directory;

    const std::string out =
        (std::filesystem::temp_directory_path() / "sep_roundtrip.chiplet").string();
    ChipletFormat format;
    format.save(*assembly, out);

    auto reloaded = format.load(out);
    EXPECT_EQ(reloaded->name(), wantName) << "U+2028 did not survive the round trip";
    EXPECT_EQ(reloaded->description(), wantDesc) << "U+2029 did not survive";
    ASSERT_TRUE(reloaded->has_flow());
    EXPECT_EQ(reloaded->flow_definition().working_directory, wantWd)
        << "the flow block lost the separator it was carrying";

    std::filesystem::remove(out);
}

// Regression guard, NOT evidence of the defect: this passes against the old
// writer too. It pins the constraint that made the per-value manipulator the
// right shape instead of a global emitter setting, which would have re-encoded
// every accented or CJK character in every document we write.
TEST(SeparatorEscaping, DocumentsWithoutASeparatorKeepTheirRawUtf8)
{
    ChipletFormat format;
    auto assembly = format.load(fixturePath("minimal.chiplet"));
    const std::string accented = "caf\xc3\xa9 \xe6\x97\xa5";  // café + CJK
    assembly->set_name(accented);

    FlowDefinition def;
    FlowStep step;
    step.id = "step1";
    step.tool_path = "/bin/true";
    def.steps.push_back(step);
    assembly->set_flow_definition(def);

    const std::string out =
        (std::filesystem::temp_directory_path() / "sep_clean.chiplet").string();
    format.save(*assembly, out);

    const std::string text = readWholeFile(out);
    EXPECT_NE(text.find(accented), std::string::npos)
        << "a document with no separator had its non-ASCII re-encoded; the "
           "escaping must be per-value, never a global emitter charset";
    EXPECT_NE(text.find("id: step1"), std::string::npos)
        << "a clean flow scalar was promoted to a quoted one, which rewrites "
           "every existing file with a flow block";

    std::filesystem::remove(out);
}


// The coverage question, asked so that a field added in six months answers it
// on its own.
//
// The tests above put separators in fields I chose BY HAND, which is the shape
// that fails quietly: a field added to the writer later appears on none of my
// lists, and nothing goes red. So this one derives its coverage from the
// writer's OWN OUTPUT. It saves a rich document, then for each scalar key that
// document contains, writes a variant with a separator injected into that key's
// values, loads it back, saves it, and requires no raw separator on disk.
//
// Fields the format constrains (format_version, enums, cross-references) refuse
// a separator at load; the sweep DISCOVERS those by trying, rather than by me
// listing them. That is the part that keeps working without maintenance: a new
// free-text field is swept automatically, a new constrained one drops out
// automatically, and neither needs anyone to remember this test exists.
//
// The injection is the ESCAPED spelling, \u2028 in a double-quoted scalar, and
// that is not a detail. The reference reader refuses a raw separator in the
// document text, so a file carrying one is not loadable at all and injecting
// one here would sweep nothing (it did: this test went red on its own >4 guard
// the moment the vendored reader gained that check). The escape is the only
// legal way the character reaches a scalar through a file, and reading it back
// puts the raw code point in memory, which is exactly the state a user reaches
// by pasting one into the GUI. So the path under test is the one that survives:
// a document that is CORRECT on disk, opened and saved, must not come back
// broken. That is the failure this whole change exists to stop, and it is the
// one a corpus scan cannot see, because the corpus only contains files that
// were already written correctly.
// What this sweep bounds, stated before it runs, because the name it used to
// carry ("NoEmissionSiteEscapesTheSweep") claimed more than it can support.
//
// The keys are read back out of ONE fixture's saved output, so the domain is
// whatever with_flow.chiplet happens to emit: a field no fixture exercises is
// not swept and this test says nothing about it. Inline collections are skipped
// too (the `{` and `[` guards below), so a scalar living inside a flow-style
// map or sequence is outside the domain as well. The claim is therefore
// "no FIXTURE-DERIVED, non-inline emission site escapes", which is weaker than
// the old name and is what the evidence actually supports.
TEST(SeparatorEscaping, NoFixtureDerivedEmissionSiteEscapesTheSweep)
{
    ChipletFormat fmt;
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    const std::filesystem::path directory(temp.path().toStdString());
    const std::string first = (directory / "sweep_first.chiplet").string();
    {
        auto assembly = fmt.load(fixturePath("with_flow.chiplet"));
        ASSERT_TRUE(assembly->has_flow()) << "the fixture must exercise the flow block";
        fmt.save(*assembly, first);
    }
    const std::string baseline = readWholeFile(first);
    const std::vector<std::string>& nonString = nonStringFieldNames();

    // Every scalar key our own writer put on disk.
    std::vector<std::string> keys;
    {
        std::istringstream in(baseline);
        std::string line;
        while (std::getline(in, line)) {
            const auto colon = line.find(':');
            if (colon == std::string::npos) continue;
            std::string key = line.substr(0, colon);
            const auto ks = key.find_first_not_of(" -");
            if (ks == std::string::npos) continue;
            key = key.substr(ks);
            if (key.find(' ') != std::string::npos) continue;
            const auto vs = line.find_first_not_of(' ', colon + 1);
            if (vs == std::string::npos) continue;
            if (line[vs] == '{' || line[vs] == '[') continue;
            if (std::find(nonString.begin(), nonString.end(), key) != nonString.end()) continue;
            if (std::find(keys.begin(), keys.end(), key) == keys.end()) keys.push_back(key);
        }
    }
    ASSERT_GT(keys.size(), 6u) << "the fixture emitted almost no scalars to sweep";

    const std::string mutatedPath = (directory / "sweep_mutated.chiplet").string();

    size_t swept = 0, refusedTheMutation = 0;
    for (const std::string& target : keys) {
        std::istringstream in(baseline);
        std::ostringstream mutated;
        std::string line;
        while (std::getline(in, line)) {
            const auto colon = line.find(':');
            bool hit = false;
            if (colon != std::string::npos) {
                std::string key = line.substr(0, colon);
                const auto ks = key.find_first_not_of(" -");
                if (ks != std::string::npos && key.substr(ks) == target) hit = true;
            }
            if (hit) {
                const auto vs = line.find_first_not_of(' ', colon + 1);
                if (vs != std::string::npos && line[vs] != '{' && line[vs] != '[') {
                    std::string value = line.substr(vs);
                    while (!value.empty() &&
                           (value.back() == ' ' || value.back() == '\r')) {
                        value.pop_back();
                    }
                    const bool quoted = value.size() >= 2 &&
                                        value.front() == '"' && value.back() == '"';
                    if (quoted) value = value.substr(1, value.size() - 2);
                    // Double-quoted on purpose even when the source scalar was
                    // plain: a plain scalar has no escape syntax, so this is the
                    // only spelling that carries the character through a file.
                    line = line.substr(0, vs) + "\"" + value + "\\u2028\"";
                }
            }
            mutated << line << "\n";
        }
        { std::ofstream f(mutatedPath, std::ios::binary); f << mutated.str(); }

        std::unique_ptr<Assembly> reloaded;
        try {
            reloaded = fmt.load(mutatedPath);
        } catch (const ChipletFormatException&) {
            // The mutated document did not load. That is all this branch knows:
            // the mutation is the only difference from a baseline that DID
            // load, so the refusal is attributable to it, but not necessarily
            // to a rule about this field. It could equally be the separator
            // breaking the document's structure for our own parser. Counting
            // it as "the format constrains this field" would be a claim the
            // catch cannot support, so it is counted as what it is.
            ++refusedTheMutation;
            continue;
        }
        ASSERT_TRUE(reloaded);
        // Each load has its own destination receipts. A previous variant's
        // output is intentionally unrelated to this model under whole-document
        // capture; give each successful variant a fresh output, not overwrite
        // authority.
        const std::string out = (directory / ("sweep_out_" + std::to_string(swept)
                                              + ".chiplet")).string();
        ASSERT_FALSE(std::filesystem::exists(out));
        fmt.save(*reloaded, out);
        ++swept;
        EXPECT_EQ(readWholeFile(out).find(kLS), std::string::npos)
            << "the emission site for '" << target << "' wrote a raw U+2028. "
               "This key was found by reading our own output, so it is a site "
               "the hand-written tests above do not name.";
    }

    // A sweep that sweeps nothing passes every assertion inside the loop, so
    // the count is the assertion that keeps this test from becoming decorative.
    EXPECT_GT(swept, 4u) << "almost every mutated document failed to load, so this "
                            "swept nothing; " << refusedTheMutation
                         << " did not load at all";

    // QTemporaryDir owns only this test's inputs and all variant outputs.
}

// A flow block the reader could not delimit must not be silently deleted.
//
// The reference reader captures the flow block as the SOURCE SLICE so a host
// that did not author it can re-emit it byte for byte. When the top-level block
// grammar cannot delimit the block (here: `flow :`, one space before the colon)
// the slice is never captured and flow_yaml comes back empty, while has_flow
// stays true. The spec is explicit that reading such a document is required and
// that only WRITING it back is impossible.
//
// The studio must therefore do three things, and this test asserts all three,
// because the first two alone are satisfied by the behaviour that loses the
// data: load it, hold no flow, and REFUSE to save. Without the refusal, open
// followed by save writes the user's build configuration out of existence and
// reports success, which is the same failure shape as a truncating write.
//
// The parser-attested whole-document capture now refuses first, before
// the older flow-only writer guard. Require its specific lexical diagnosis,
// not the older guard's exact spelling of the suggested replacement key.
TEST(FlowBlock, UndelimitableFlowLoadsButRefusesToSave)
{
    QTemporaryDir temp;
    ASSERT_TRUE(temp.isValid());
    const std::filesystem::path directory(temp.path().toStdString());
    const std::string src = (directory / "flow_undelimitable.chiplet").string();
    {
        std::ofstream f(src, std::ios::binary);
        f << "format_version: \"1.0\"\n"
          << "assembly:\n"
          << "  name: \"Undelimitable Flow\"\n"
          << "  units: \"um\"\n"
          << "flow :\n"
          << "  steps:\n"
          << "    - id: build\n"
          << "      name: \"Build\"\n"
          << "      interpreter: shell\n"
          << "      tool_path: /bin/true\n";
    }

    ChipletFormat fmt;
    std::unique_ptr<Assembly> assembly;
    ASSERT_NO_THROW(assembly = fmt.load(src))
        << "flow rule 1: a reader that cannot parse the block must still load "
           "the document";
    ASSERT_TRUE(assembly);
    EXPECT_FALSE(assembly->has_flow())
        << "the block's bytes were never captured, so there is nothing to hold";
    EXPECT_TRUE(assembly->flow_block_uncaptured())
        << "the assembly must carry the reason, or save() cannot tell this "
           "apart from a document that simply has no flow";

    const auto original = readWholeFile(src);
    const auto originalName = assembly->name();
    const std::string out = (directory / "flow_undelimitable_out.chiplet").string();
    ASSERT_FALSE(std::filesystem::exists(out));
    try {
        fmt.save(*assembly, out);
        ADD_FAILURE() << "save() dropped a flow block it never read, which "
                         "deletes the user's build configuration and reports "
                         "success";
    } catch (const ChipletFormatException& e) {
        const std::string msg = e.what();
        // A generic exception (including unrelated-destination refusal) is
        // insufficient: the parser must identify the actual malformed key.
        EXPECT_NE(msg.find("source preservation is unsafe"), std::string::npos) << msg;
        EXPECT_NE(msg.find("lexical_boundary"), std::string::npos) << msg;
        EXPECT_NE(msg.find("flow :"), std::string::npos) << msg;
        EXPECT_NE(msg.find("column zero"), std::string::npos) << msg;
        EXPECT_NE(msg.find("Your edits remain open"), std::string::npos) << msg;
    }
    EXPECT_FALSE(std::filesystem::exists(out))
        << "the refusal must happen before anything is written";

    EXPECT_EQ(readWholeFile(src), original);
    EXPECT_EQ(assembly->name(), originalName);
    EXPECT_TRUE(assembly->flow_block_uncaptured());
}

// The control for the test above. Without it, "refuse to save" is satisfiable
// by refusing every document that has a flow block, and the guard above would
// pass with the feature dead.
//
// The has_flow assertion is also where the flow_yaml change of MEANING is
// pinned: the slice now begins at the `flow:` key line, so a consumer that
// parses it without indexing one level down finds no steps, builds an empty
// FlowDefinition, and quietly decides the document has no flow.
TEST(FlowBlock, DelimitableFlowStillSaves)
{
    ChipletFormat fmt;
    auto assembly = fmt.load(fixturePath("with_flow.chiplet"));
    ASSERT_TRUE(assembly);
    EXPECT_TRUE(assembly->has_flow())
        << "the source slice starts at the `flow:` key line; a false here means "
           "the block was read one level too high";
    EXPECT_FALSE(assembly->flow_block_uncaptured());

    const std::string out =
        (std::filesystem::temp_directory_path() / "flow_delimitable_out.chiplet").string();
    ASSERT_NO_THROW(fmt.save(*assembly, out));
    EXPECT_NE(readWholeFile(out).find("flow:"), std::string::npos);
    std::filesystem::remove(out);
}

// A flow block that was read comes back byte for byte, including the parts this
// program does not model.
//
// The studio has no path that edits a flow block: set_flow_definition has one
// caller, the loader, and flow_definition() is const. So every difference
// between what was read and what is written is loss, and the previous writer
// re-serialised the parsed model, which loses three separate things. Two of
// them are fidelity. The third is not: `DEBUG: "1"` came back as `DEBUG: 1`,
// which a YAML 1.1 reader then resolves to the integer 1 rather than the string
// the author wrote. A value changed type on the way through, which is the same
// defect the document writer was fixed for and which survived in this block.
//
// The assertion is on BYTES rather than on a reload. A reload compares the
// parsed model against itself, so it agrees through exactly the re-serialisation
// under test and cannot see any of this.
TEST(FlowBlock, ASourceFlowBlockSurvivesByteForByte)
{
    const std::string src =
        (std::filesystem::temp_directory_path() / "flow_fidelity.chiplet").string();
    const std::string kFlow =
        "flow:\n"
        "  # a comment the author wrote and expects to keep\n"
        "  working_directory: \".\"\n"
        "  parallelism: 4\n"
        "  environment:\n"
        "    DEBUG: \"1\"\n"
        "  steps:\n"
        "    - id: build\n"
        "      name: \"Build\"\n"
        "      tool: shell\n"
        "      script: /bin/true\n"
        "      retries: 3\n"
        "      args: [\"--fast\"]\n";
    {
        std::ofstream f(src, std::ios::binary);
        f << "format_version: \"1.0\"\n"
          << "assembly:\n"
          << "  name: \"Flow Fidelity\"\n"
          << "  units: \"um\"\n"
          << kFlow;
    }

    ChipletFormat fmt;
    auto assembly = fmt.load(src);
    ASSERT_TRUE(assembly);
    ASSERT_TRUE(assembly->has_flow()) << "the block must still be parsed, not only kept";

    const std::string out =
        (std::filesystem::temp_directory_path() / "flow_fidelity_out.chiplet").string();
    ASSERT_NO_THROW(fmt.save(*assembly, out));

    const std::string written = readWholeFile(out);
    const auto at = written.find("flow:");
    ASSERT_NE(at, std::string::npos) << "the flow block is gone entirely:\n" << written;

    std::string tail = written.substr(at);
    while (!tail.empty() && tail.back() == '\n') tail.pop_back();
    std::string expected = kFlow;
    while (!expected.empty() && expected.back() == '\n') expected.pop_back();
    EXPECT_EQ(tail, expected)
        << "the flow block was rewritten rather than carried. Each of these is a\n"
           "separate loss: the comment, the unmodelled keys (parallelism,\n"
           "retries), and the quoting, which is the one that changes a value's\n"
           "TYPE for the next reader.";

    std::filesystem::remove(src);
    std::filesystem::remove(out);
}

// An assembly whose flow this host authored still writes from the model. Without
// this, "write the source bytes" is satisfiable by writing nothing whenever
// there are no source bytes to write, which would silently drop the flow of any
// assembly built in a script.
TEST(FlowBlock, AnAuthoredFlowIsStillSerializedFromTheModel)
{
    ChipletFormat fmt;
    auto assembly = fmt.load(fixturePath("with_flow.chiplet"));
    ASSERT_TRUE(assembly);
    ASSERT_FALSE(assembly->flow_source_text().empty());

    // Reach the state of an assembly that was never read from a file but was
    // given a flow definition programmatically. Going through the ordinary
    // setter is the point: this asserts the mechanism, not a test-only escape
    // hatch. Plain set_flow_definition is what any future flow editor will
    // call, and it has to drop the bytes it did not produce.
    assembly->set_flow_definition(FlowDefinition(assembly->flow_definition()));
    ASSERT_TRUE(assembly->has_flow());
    ASSERT_TRUE(assembly->flow_source_text().empty())
        << "set_flow_definition left the previous document's bytes attached, so "
           "a later edit would be written over by them";

    const std::string out =
        (std::filesystem::temp_directory_path() / "flow_authored_out.chiplet").string();
    ASSERT_NO_THROW(fmt.save(*assembly, out));

    const std::string written = readWholeFile(out);
    EXPECT_NE(written.find("flow:"), std::string::npos)
        << "an authored flow was dropped:\n" << written;
    EXPECT_NE(written.find("steps:"), std::string::npos) << written;

    ChipletFormat reader;
    auto reloaded = reader.load(out);
    ASSERT_TRUE(reloaded);
    EXPECT_TRUE(reloaded->has_flow());

    std::filesystem::remove(out);
}

// Pin the BEHAVIOUR of the vendored reader that this host depends on, rather
// than its version.
//
// This exists because of a specific escape. Upstream changed flow_yaml from the
// flow block's VALUE to the source slice INCLUDING the `flow:` key line, and
// READER_RELEASE was 1.1.0 at that commit and at its parent. The number did not
// move, so every guard anyone could have written on it would have been green
// through the whole episode: an exact match, a minimum, a compatible range. The
// version is the producer's own summary of its behaviour, and here the summary
// was wrong. The behaviour cannot be wrong about itself.
//
// What this host actually depends on is one sentence, so that is what is
// asserted. It goes red the day the semantics change, whatever the version
// says; it stays quiet through every bump that does not touch it, so it does
// not become maintenance noise; and it would have caught that commit the day it
// landed, which is the only test of a guard that matters.
//
// It asserts on cfio directly, not through ChipletFormat, on purpose: the
// proposition is about the LIBRARY, and routing it through this host's
// consumer would let a compensating change here keep it green.
TEST(VendoredReaderContract, FlowYamlIsTheSourceSliceIncludingTheKeyLine)
{
    namespace cfio = chiplet_format_io;

    const std::string src =
        (std::filesystem::temp_directory_path() / "vendored_contract.chiplet").string();
    const std::string kFlow =
        "flow:\n"
        "  working_directory: \".\"\n"
        "  steps:\n"
        "    - id: build\n"
        "      tool: shell\n"
        "      script: /bin/true\n";
    {
        std::ofstream f(src, std::ios::binary);
        f << "format_version: \"1.0\"\n"
          << "assembly:\n"
          << "  name: \"Vendored Contract\"\n"
          << "  units: \"um\"\n"
          << kFlow;
    }

    const cfio::ChipletDocument doc = cfio::load(src);
    EXPECT_TRUE(doc.has_flow);
    EXPECT_TRUE(doc.flow_source == cfio::FlowSource::Slice);

    ASSERT_FALSE(doc.flow_yaml.empty());
    EXPECT_EQ(doc.flow_yaml.rfind("flow:", 0), 0u)
        << "flow_yaml no longer starts at the `flow:` key line. Every consumer "
           "that indexes one level down to reach the block's contents is now "
           "reading the wrong node, and the ones that do not index find no "
           "steps, build an empty flow and silently decide the document has "
           "none. What came back instead:\n" << doc.flow_yaml;

    // Byte for byte, trailing newline included. The earlier version of this
    // test trimmed trailing newlines from both sides before comparing, which
    // meant the one thing the EOF contract has to say went unasserted: it would
    // have passed whether the reader returned the block with its final newline,
    // without it, or with three. If this ever fails only on the trailing bytes,
    // that is a real change and the fix is to decide the contract again here,
    // not to trim it away.
    EXPECT_EQ(doc.flow_yaml, kFlow)
        << "flow_yaml is no longer the source bytes exactly. It is what this "
           "host writes back, so a re-serialisation here is data loss on every "
           "save. Sizes: got " << doc.flow_yaml.size() << ", want " << kFlow.size();

    std::filesystem::remove(src);
}

// The writer's half of the EOF contract. The reader hands back the flow slice
// exactly, trailing newline included (asserted above). The host now also keeps
// an absent final newline: parser-attested carried bytes are not normalized.
// This is the Studio host preservation contract, not a change to reference
// dumps() or its writer pins. Zero, one and two LFs must remain distinguishable.
//
// Driving the slice directly with 0, 1 and 2 trailing LFs is what separates
// those. Trimming the two-LF case to make it match the old name would be
// silently discarding bytes the reader promised to hand back exactly.
TEST(VendoredReaderContract, SavePreservesCapturedFinalNewlineExactly)
{
    struct Case {
        const char* label;
        std::string slice;      // what flow_source_text holds
        size_t wantTrailing;    // LFs the saved file must end with
    };
    const std::vector<Case> cases = {
        { "no trailing newline",  "flow:\n  steps: []",      0 },
        { "one trailing newline", "flow:\n  steps: []\n",    1 },
        // Preserved, NOT trimmed. If this ever comes back 1, the writer
        // started rewriting bytes the reader is contracted to return verbatim.
        { "two trailing newlines", "flow:\n  steps: []\n\n", 2 },
    };

    for (const auto& c : cases) {
        SCOPED_TRACE(c.label);

        ChipletFormat fmt;
        auto assembly = fmt.load(fixturePath("with_flow.chiplet"));
        ASSERT_TRUE(assembly != nullptr);
        assembly->set_flow_definition_from_source(FlowDefinition(), c.slice);
        ASSERT_EQ(assembly->flow_source_text(), c.slice)
            << "the slice did not survive being set, so this case tests nothing";

        const std::string out =
            (std::filesystem::temp_directory_path() / "eof_contract.chiplet").string();
        fmt.save(*assembly, out);
        const std::string text = readWholeFile(out);
        std::filesystem::remove(out);

        ASSERT_FALSE(text.empty());
        size_t trailing = 0;
        while (trailing < text.size() && text[text.size() - 1 - trailing] == '\n') {
            ++trailing;
        }
        EXPECT_EQ(trailing, c.wantTrailing)
            << "saved file ends with " << trailing << " newline(s), want "
            << c.wantTrailing;
    }
}

// Idempotence, stated separately because it is the claim that matters and the
// loop above only checks one save. Two saves of the same document must be byte
// identical; a writer that appends unconditionally would pass every
// single-save assertion and still grow the file forever.
TEST(VendoredReaderContract, SavingTwiceProducesTheSameBytes)
{
    ChipletFormat fmt;
    auto assembly = fmt.load(fixturePath("with_flow.chiplet"));
    ASSERT_TRUE(assembly != nullptr);

    const std::string out =
        (std::filesystem::temp_directory_path() / "eof_idempotent.chiplet").string();
    fmt.save(*assembly, out);
    const std::string first = readWholeFile(out);

    ChipletFormat fmt2;
    auto again = fmt2.load(out);
    ASSERT_TRUE(again != nullptr);
    fmt2.save(*again, out);
    const std::string second = readWholeFile(out);
    std::filesystem::remove(out);

    EXPECT_EQ(first, second)
        << "a save of a saved document changed it; sizes " << first.size()
        << " then " << second.size();
}

// The other half of the same contract: a block the grammar cannot delimit is
// reported as such and leaves no bytes, rather than being reported as absent.
// The host's save-time refusal keys on exactly this, so if it ever became
// Absent the refusal would stop firing and the silent deletion would come back.
// Reader 1.4.0 widened what makes a document not splittable, and this host
// depends on the verdict: ChipletFormat.cpp:534 turns NotDelimitable into
// set_flow_block_uncaptured(true), which REFUSES the next save. So the set of
// documents this studio will not save grew with the re-vendor, and that is a
// behaviour change worth a test rather than a sentence in a commit message.
//
// Before 1.4.0 only a QUOTED key at column zero did this. Now any line at column
// zero that no top-level key can own does: an explicit key (`? ...`), a bare key
// outside the grammar (`a b:`), a tab-led line. The line need not be anywhere
// near the flow block; it makes the whole file unsplittable, so a flow block
// that is itself perfectly well formed loses its bytes.
TEST(VendoredReaderContract, AnExplicitKeyAtColumnZeroCostsAWellFormedFlowItsBytes)
{
    namespace cfio = chiplet_format_io;

    const std::string src =
        (std::filesystem::temp_directory_path() / "vendored_unattributable.chiplet").string();
    {
        std::ofstream f(src, std::ios::binary);
        f << "format_version: \"1.0\"\n"
          << "assembly:\n"
          << "  name: \"Unattributable\"\n"
          << "  units: \"um\"\n"
          // An explicit key at column zero. Valid YAML, not a key line, and
          // nowhere near the flow block below.
          << "? explicit\n"
          << ": value\n"
          // The flow block itself is exactly what the grammar wants.
          << "flow:\n"
          << "  steps:\n"
          << "    - id: build\n"
          << "      tool: shell\n";
    }

    cfio::LoadOptions opts;
    opts.validate = false;
    const cfio::ChipletDocument doc = cfio::loads(readWholeFile(src), opts);
    std::filesystem::remove(src);

    EXPECT_EQ(doc.flow_source, cfio::FlowSource::NotDelimitable)
        << "a bare `flow:` at column zero still delimited, so the unattributable "
           "line elsewhere in the file did not cost the block its bytes";
    EXPECT_TRUE(doc.flow_yaml.empty())
        << "NotDelimitable must leave no bytes, or a host cannot tell the two "
           "failure modes apart";
}

TEST(VendoredReaderContract, AnUndelimitableBlockIsReportedNotHidden)
{
    namespace cfio = chiplet_format_io;

    const std::string src =
        (std::filesystem::temp_directory_path() / "vendored_contract_nd.chiplet").string();
    {
        std::ofstream f(src, std::ios::binary);
        f << "format_version: \"1.0\"\n"
          << "assembly:\n"
          << "  name: \"Undelimitable\"\n"
          << "  units: \"um\"\n"
          << "flow :\n"
          << "  steps:\n"
          << "    - id: build\n"
          << "      tool: shell\n";
    }

    const cfio::ChipletDocument doc = cfio::load(src);
    EXPECT_TRUE(doc.has_flow)
        << "the document HAS a flow block; reporting it absent is what would "
           "make dropping it look correct";
    EXPECT_TRUE(doc.flow_source == cfio::FlowSource::NotDelimitable);
    EXPECT_TRUE(doc.flow_yaml.empty())
        << "bytes appeared for a block the grammar could not delimit; whatever "
           "they are, they were not captured from the source";

    std::filesystem::remove(src);
}

// ---------------------------------------------------------------------------
// The writer's recorded intent, against the format's own split.
//
// The first group asserts against the vendored library DIRECTLY, per
// src/formats/chiplet_format_io/VENDORED.md: a compensating change in this host
// must not be able to keep them green. The second group asserts on the
// comparator, which is the only place the ORDER clause can be witnessed at all
// (recording intent at the emission site means moving an emission block moves
// its record with it, so no production mutant can make the two disagree about
// order). The third group asserts on the save path.
// ---------------------------------------------------------------------------

TEST(VendoredReaderContract, SplitKeysExcludeThePreamble)
{
    namespace cfio = chiplet_format_io;

    // Keys deliberately in ALPHABETICAL order here, so that a splitter which
    // sorted its output would still pass this one; the ordering claim is the
    // next test's job and mixing them would make both blunt.
    const std::string text =
        "# a comment nobody owns\n"
        "\n"
        "assembly:\n  name: \"P\"\n"
        "format_version: \"1.0\"\n";
    ASSERT_EQ(text[0], '#')
        << "this document has no preamble, so the branch under test never runs "
           "and a pass here would mean nothing. A positive result for preamble "
           "exclusion needs a document whose FIRST BYTE is a comment; without "
           "one, a zero here means nothing was exercised, not that nothing was "
           "wrong. Anyone holding such a zero should read that sentence and "
           "not this test.";

    const cfio::TopLevelKeys split = cfio::split_top_level_keys(text);
    EXPECT_TRUE(split.not_splittable.empty()) << split.not_splittable;
    EXPECT_EQ(split.keys, (std::vector<std::string>{"assembly", "format_version"}))
        << "the lines before the first key belong to no block and must not "
           "appear as one";
}

TEST(VendoredReaderContract, SplitKeysAreInDocumentOrder)
{
    namespace cfio = chiplet_format_io;

    // Deliberately NOT alphabetical: this is the whole content of the test.
    const std::string text =
        "format_version: \"1.0\"\n"
        "assembly:\n  name: \"P\"\n"
        "netlist:\n  nets: []\n"
        "components: []\n";

    const cfio::TopLevelKeys split = cfio::split_top_level_keys(text);
    ASSERT_TRUE(split.not_splittable.empty()) << split.not_splittable;
    EXPECT_EQ(split.keys, (std::vector<std::string>{
                              "format_version", "assembly", "netlist", "components"}))
        << "order is part of what a split means; a sorted or set-like result "
           "would let a reordering writer through";
}

TEST(VendoredReaderContract, NotSplittableLeavesNoKeysToRead)
{
    namespace cfio = chiplet_format_io;

    const std::string bad =
        "format_version: \"1.0\"\n"
        "assembly:\n  name: \"P\"\n"
        "\"quoted\": 1\n";

    const cfio::TopLevelKeys split = cfio::split_top_level_keys(bad);
    EXPECT_NE(split.not_splittable.find("quoted key at column zero"),
              std::string::npos)
        << "got: " << split.not_splittable;
    EXPECT_TRUE(split.keys.empty())
        << "keys came back for a document the splitter disowned; a host that "
           "compares them is comparing against a sequence that mis-attributes "
           "bytes";

    // The positive probe. Without it a splitter that returned {} for every
    // input satisfies both assertions above and this test proves nothing,
    // which is exactly how the const-boundary test went vacuous.
    const std::string good =
        "format_version: \"1.0\"\n"
        "assembly:\n  name: \"P\"\n";
    const cfio::TopLevelKeys ok = cfio::split_top_level_keys(good);
    EXPECT_TRUE(ok.not_splittable.empty()) << ok.not_splittable;
    EXPECT_FALSE(ok.keys.empty())
        << "the same call returns nothing for a well-formed document, so the "
           "emptiness above was not evidence of anything";
}

TEST(VendoredReaderContract, ARepeatedTopLevelKeyThrows)
{
    namespace cfio = chiplet_format_io;

    const std::string repeated =
        "format_version: \"1.0\"\n"
        "assembly:\n  name: \"P\"\n"
        "netlist:\n  nets: []\n"
        "netlist:\n  nets: []\n";
    try {
        (void)cfio::split_top_level_keys(repeated);
        FAIL() << "a repeated top-level key came back as a sequence; PyYAML "
                  "keeps the last value and yaml-cpp the first, so there is no "
                  "one document to hand back";
    } catch (const cfio::ChipletFormatError& e) {
        const std::string what = e.what();
        EXPECT_NE(what.find("repeated top-level key"), std::string::npos) << what;
        EXPECT_NE(what.find("netlist"), std::string::npos) << what;
    }

    // Positive arm: "it throws" is also satisfied by throwing on everything.
    const std::string once =
        "format_version: \"1.0\"\n"
        "assembly:\n  name: \"P\"\n"
        "netlist:\n  nets: []\n";
    EXPECT_NO_THROW((void)cfio::split_top_level_keys(once));
}

TEST(VendoredReaderContract, SplitKeysCanNameAKeyTheParserNeverSees)
{
    namespace cfio = chiplet_format_io;

    // The hazard the library's own header documents, in a document that
    // exhibits it. A flow-mapping value continued at column zero is a key line
    // to the block grammar and part of the preceding scalar to the parser.
    const std::string phantom =
        "format_version: \"1.0\"\n"
        "assembly: {name: \"P\",\n"
        "units: \"um\"}\n";

    const cfio::TopLevelKeys split = cfio::split_top_level_keys(phantom);
    ASSERT_TRUE(split.not_splittable.empty()) << split.not_splittable;
    EXPECT_EQ(split.keys, (std::vector<std::string>{
                              "format_version", "assembly", "units"}))
        << "the grammar is expected to name `units` here";

    const YAML::Node root = YAML::Load(phantom);
    EXPECT_FALSE(root["units"])
        << "the parser sees `units` as a top-level key too, so this document "
           "no longer demonstrates the disagreement it was written for";
    EXPECT_EQ(root["assembly"]["units"].as<std::string>(), "um")
        << "the parser must attribute those bytes to `assembly`";

    // The contrast, so the claim is about THIS shape and not about column-zero
    // lines in general: a block scalar dedents, and both agree.
    const std::string agreeing =
        "format_version: \"1.0\"\n"
        "assembly:\n  name: |\n    x\n"
        "units: \"um\"\n";
    const cfio::TopLevelKeys both = cfio::split_top_level_keys(agreeing);
    ASSERT_TRUE(both.not_splittable.empty()) << both.not_splittable;
    EXPECT_EQ(both.keys.size(), 3u);
    EXPECT_TRUE(YAML::Load(agreeing)["units"])
        << "the parser and the grammar are expected to agree here";
}

// The comparator, on hand-built pairs.
//
// This is the only witness the ORDER clause can have. Intent is recorded at the
// emission site, so moving an emission block moves its record with it and no
// edit to the writer can make the two sequences disagree about order alone.
// A property with no possible witness should not be claimed; this is how it
// gets one.
TEST(TopLevelKeyIntent, TheSequenceIsCompared)
{
    const std::string doc =
        "format_version: \"1.0\"\n"
        "assembly:\n  name: \"P\"\n"
        "netlist:\n  nets: []\n";
    const std::vector<std::string> real{"format_version", "assembly", "netlist"};

    EXPECT_TRUE(chiplet::check_top_level_key_sequence(real, doc).empty());

    const std::vector<std::string> permuted{"assembly", "format_version", "netlist"};
    EXPECT_FALSE(chiplet::check_top_level_key_sequence(permuted, doc).empty())
        << "same keys, different order, accepted: the comparison is set-like "
           "and a writer that reorders its blocks passes";

    EXPECT_FALSE(chiplet::check_top_level_key_sequence(
                     {"format_version", "assembly"}, doc).empty())
        << "a key the writer never intended was accepted";
    EXPECT_FALSE(chiplet::check_top_level_key_sequence(
                     {"format_version", "assembly", "netlist", "flow"}, doc).empty())
        << "an intended key the document lacks was accepted";
}

TEST(TopLevelKeyIntent, AnEmptyIntentIsRefusedRatherThanAgreedWith)
{
    // Two documents, because the interesting one is the second: an empty split
    // and an empty intent are equal, and equality is exactly the wrong answer.
    EXPECT_FALSE(chiplet::check_top_level_key_sequence(
                     {}, "format_version: \"1.0\"\n").empty());
    EXPECT_FALSE(chiplet::check_top_level_key_sequence({}, "# only a comment\n").empty())
        << "nothing to compare was treated as agreement";
}

TEST(TopLevelKeyIntent, ANonSplittableDocumentIsRefusedAsSuch)
{
    const std::string bad =
        "format_version: \"1.0\"\n"
        "assembly:\n  name: \"P\"\n"
        "\"quoted\": 1\n";
    const std::string why = chiplet::check_top_level_key_sequence(
        {"format_version", "assembly"}, bad);
    ASSERT_FALSE(why.empty());
    EXPECT_NE(why.find("do not split"), std::string::npos)
        << "reported as a key mismatch rather than as a document that cannot "
           "be split; `keys` was read when the library says it must not be. "
           "got: " << why;
}

TEST(TopLevelKeyIntent, ARepeatedKeyIsRefusedNotSwallowed)
{
    const std::string repeated =
        "format_version: \"1.0\"\n"
        "assembly:\n  name: \"P\"\n"
        "netlist:\n  nets: []\n"
        "netlist:\n  nets: []\n";
    const std::string why = chiplet::check_top_level_key_sequence(
        {"format_version", "assembly", "netlist"}, repeated);
    EXPECT_FALSE(why.empty())
        << "the splitter throws here; an uncaught throw would leave save() with "
           "no verdict, and a swallowed one would let the document through";
    EXPECT_NE(why.find("repeated top-level key"), std::string::npos) << why;
}

// The save path.
//
// Reaching a mismatch from the outside is narrower than it looks. Every
// top-level key save() emits is a literal, and every value it emits through
// qs() is double-quoted, so no value can put a key-shaped line at column zero.
// The bytes in the output that no emitter built are the appended flow slice,
// which is where these drive from.
TEST(SaveTimeKeySequenceCheck, AKeyTheWriterNeverIntendedIsRefused)
{
    ChipletFormat fmt;
    auto assembly = fmt.load(fixturePath("with_flow.chiplet"));
    ASSERT_TRUE(assembly);

    // A second top-level block smuggled in behind the flow bytes.
    const std::string slice =
        "flow:\n  steps: []\ninterposer:\n  adapter: \"intm4tm2\"\n";
    assembly->set_flow_definition_from_source(
        FlowDefinition(assembly->flow_definition()), slice);
    ASSERT_EQ(assembly->flow_source_text(), slice)
        << "the slice did not survive being set, so this case tests nothing";

    const std::string out =
        (std::filesystem::temp_directory_path() / "keyseq_refused.chiplet").string();
    const std::string sidecar = out + ".rejected";
    std::filesystem::remove(sidecar);
    const std::string before = "# the previous good version\n";
    { std::ofstream f(out); f << before; }

    try {
        fmt.save(*assembly, out);
        FAIL() << "a document carrying a top-level block this writer never "
                  "recorded was written";
    } catch (const ChipletFormatException& e) {
        const std::string message = e.what();
        EXPECT_NE(message.find("interposer"), std::string::npos)
            << "the refusal must name the block that caused it. got: " << message;
    }

    std::ifstream check(out, std::ios::binary);
    const std::string after((std::istreambuf_iterator<char>(check)),
                            std::istreambuf_iterator<char>());
    EXPECT_EQ(after, before) << "the target was modified by a refused save";
    std::filesystem::remove(out);
    std::filesystem::remove(sidecar);
}

TEST(SaveTimeKeySequenceCheck, FlowBytesThatAreNotAFlowBlockAreRefusedBeforeAnythingIsWritten)
{
    ChipletFormat fmt;
    auto assembly = fmt.load(fixturePath("with_flow.chiplet"));
    ASSERT_TRUE(assembly);

    // Not a flow block at all: the value without its key line, which is the
    // pre-slice spelling the reader stopped producing.
    const std::string slice = "  steps: []\n";
    assembly->set_flow_definition_from_source(
        FlowDefinition(assembly->flow_definition()), slice);
    ASSERT_EQ(assembly->flow_source_text(), slice);

    const std::string out =
        (std::filesystem::temp_directory_path() / "keyseq_notflow.chiplet").string();
    std::filesystem::remove(out);
    std::filesystem::remove(out + ".rejected");

    EXPECT_THROW(fmt.save(*assembly, out), ChipletFormatException);
    EXPECT_FALSE(std::filesystem::exists(out))
        << "the target was created for a document that was never written";
    EXPECT_FALSE(std::filesystem::exists(out + ".rejected"))
        << "nothing had been serialized yet, so there was nothing to rescue; a "
           "sidecar here would be a copy of bytes this host did not produce";
}

namespace {
// The whole top-level sequence this writer emits, in the order it emits it. It
// is spelled out here rather than derived, because a list computed from the
// same code under test agrees with it by construction.
const std::vector<std::string> kAllTopLevelKeysNoFlow{
    "format_version", "assembly", "technologies", "interconnect",
    "connection_stacks", "components", "interfaces", "netlist"};

std::string readWhole(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

std::vector<std::string> savedTopLevelKeys(Assembly& assembly, const std::string& out)
{
    ChipletFormat fmt;
    fmt.save(assembly, out);
    const chiplet_format_io::TopLevelKeys split =
        chiplet_format_io::split_top_level_keys(readWhole(out));
    EXPECT_TRUE(split.not_splittable.empty()) << split.not_splittable;
    std::filesystem::remove(out);
    return split.keys;
}
}  // namespace

TEST(SaveTimeKeySequenceCheck, EveryTopLevelKeyThisWriterEmitsIsRecorded)
{
    // Coverage, and the assertion has to be EXACT to be that. A `>= 6` here
    // would pass while leaving emission sites unexercised, which is the claim
    // this test is making on behalf of all of them.
    ChipletFormat fmt;
    auto assembly = fmt.load(fixturePath("cross_reader_emitted_paths.chiplet"));
    ASSERT_TRUE(assembly);

    const std::string out =
        (std::filesystem::temp_directory_path() / "keyseq_all_keys.chiplet").string();
    EXPECT_EQ(savedTopLevelKeys(*assembly, out), kAllTopLevelKeysNoFlow)
        << "this fixture is the one that reaches every non-flow emission site; "
           "if the writer's sequence changed, change it here deliberately "
           "rather than loosening the assertion";
}

TEST(SaveTimeKeySequenceCheck, TheFlowKeyIsRecordedOnTheSOURCEBYTESPath)
{
    // Path one of two: bytes the reader captured, appended after the emitter's
    // document. The emitter never writes this key, so the record is the only
    // thing that puts it in the intent.
    ChipletFormat fmt;
    auto assembly = fmt.load(fixturePath("with_flow.chiplet"));
    ASSERT_TRUE(assembly);
    ASSERT_FALSE(assembly->flow_source_text().empty())
        << "this fixture no longer carries captured flow bytes, so it exercises "
           "the other path and this test proves nothing about this one";

    const std::string out =
        (std::filesystem::temp_directory_path() / "keyseq_flow_bytes.chiplet").string();
    const std::vector<std::string> keys = savedTopLevelKeys(*assembly, out);
    ASSERT_FALSE(keys.empty());
    EXPECT_EQ(keys.back(), "flow")
        << "the appended block must be the last top-level key";
}

TEST(SaveTimeKeySequenceCheck, TheFlowKeyIsRecordedOnTheAUTHOREDPath)
{
    // Path two of two: a definition held in memory, which FlowConfig writes
    // through the emitter, emitting the `flow` key itself. save() records the
    // key without emitting it, so this is the arm that would go red if that
    // other translation unit stopped writing it.
    ChipletFormat fmt;
    auto assembly = fmt.load(fixturePath("minimal.chiplet"));
    ASSERT_TRUE(assembly);
    ASSERT_FALSE(assembly->has_flow())
        << "the fixture already has a flow, so the authored path is not what "
           "runs here";

    FlowStep step;
    step.id = "build";
    step.declared_tool = "shell";
    FlowDefinition def;
    def.steps.push_back(step);
    assembly->set_flow_definition(std::move(def));
    ASSERT_TRUE(assembly->has_flow());
    ASSERT_TRUE(assembly->flow_source_text().empty())
        << "setting a definition must clear captured bytes, or this is the "
           "source-bytes path again";

    const std::string out =
        (std::filesystem::temp_directory_path() / "keyseq_flow_authored.chiplet").string();
    const std::vector<std::string> keys = savedTopLevelKeys(*assembly, out);
    ASSERT_FALSE(keys.empty());
    EXPECT_EQ(keys.back(), "flow");
}

TEST(SaveTimeKeySequenceCheck, DocumentsNobodyEditedStillSave)
{
    // Without this control, "refuse on any difference" is satisfied by refusing
    // every document, and the two refusal tests above pass with the feature
    // dead.
    const char* fixtures[] = {
        "minimal.chiplet", "with_components.chiplet", "with_technologies.chiplet",
        "with_flow.chiplet", "with_io_pads.chiplet",
    };
    for (const char* name : fixtures) {
        ChipletFormat fmt;
        auto assembly = fmt.load(fixturePath(name));
        ASSERT_TRUE(assembly) << name;
        const std::string out =
            (std::filesystem::temp_directory_path() /
             (std::string("keyseq_ok_") + name)).string();
        std::filesystem::remove(out + ".rejected");
        EXPECT_NO_THROW(fmt.save(*assembly, out)) << name;
        EXPECT_FALSE(std::filesystem::exists(out + ".rejected")) << name;
        std::filesystem::remove(out);
    }
}

// What the user is told when an unattended auto-save fails.
//
// The previous behaviour was a five second status-bar message. It is worth
// being precise about why that is a defect rather than a rough edge: the
// message is gone before a user looking at the 3D view can notice it, and once
// it is gone nothing on screen says anything is wrong, while the condition it
// reported is still true. The user goes on believing their work is being kept.
// That is the failure this asserts against, so the assertions are about the
// notice's LIFETIME, not about whether a notice happens.
//
// The policy lives in its own display-free type precisely so it can be asserted
// here. Inside MainWindow the only witness would be a test that needs a running
// GUI, and those self-skip on this gate, which would leave the fix with no
// witness at all on the machine that runs it.
TEST(AutoSaveNoticePolicy, TheMarkerLastsAsLongAsTheCondition)
{
    AutoSaveState state;
    EXPECT_FALSE(state.failing());

    const AutoSaveNotice first = state.on_failure();
    EXPECT_TRUE(first.announce) << "the first failure must interrupt";
    EXPECT_TRUE(first.warn);

    // The part a transient message cannot do: still failing, still visible.
    for (int i = 0; i < 5; ++i) {
        const AutoSaveNotice again = state.on_failure();
        EXPECT_FALSE(again.announce)
            << "attempt " << i + 2 << " interrupted again; a modal every five "
               "minutes is not a stronger warning, it is one people dismiss "
               "without reading";
        EXPECT_TRUE(again.warn)
            << "attempt " << i + 2 << " cleared the marker while auto-save was "
               "still failing, which is the original defect: nothing on screen "
               "says the work is unprotected";
    }
    EXPECT_TRUE(state.failing());
}

// The control. Without it, "keep the marker up while failing" is satisfied by a
// marker that is always up, which would train the user to ignore it and would
// be a worse outcome than the bug.
TEST(AutoSaveNoticePolicy, ASuccessClearsTheMarkerAndRearmsTheAnnouncement)
{
    AutoSaveState state;
    ASSERT_TRUE(state.on_failure().announce);
    ASSERT_FALSE(state.on_failure().announce);

    const AutoSaveNotice ok = state.on_success();
    EXPECT_FALSE(ok.warn) << "the marker outlived the condition it reports";
    EXPECT_FALSE(ok.announce) << "a success is not an event to interrupt over";
    EXPECT_FALSE(state.failing());

    // A later failure is a NEW event, and the recovery in between is what makes
    // it one, so it announces again rather than being swallowed as a repeat.
    EXPECT_TRUE(state.on_failure().announce);
}

TEST(AutoSaveNoticePolicy, ASuccessOnAHealthyStateSaysNothing)
{
    AutoSaveState state;
    const AutoSaveNotice ok = state.on_success();
    EXPECT_FALSE(ok.warn);
    EXPECT_FALSE(ok.announce);
    EXPECT_FALSE(state.failing());
}

// The writer builds YAML from this program's model; the reader is a
// vendored copy of the format's reference implementation. Nothing keeps them in
// step. The reader gained validation rule 8 in the re-vendor at 7caeaa9 and the
// emitter was not told, which is the general shape: the reader grows rules, the
// writer does not hear about it, and the gap is a document this program writes
// and the format rejects.
//
// So save() reads its own output back through that reader before writing, and
// refuses if it is rejected. These tests pin what makes that worth having, and
// the negative control below is not optional: a check that refused everything
// would satisfy the positive case on its own.

// A document this program can produce that the reader refuses: an interface of
// type micro_bump meeting a wire_bond pad on a shared port layer. Built through
// the ordinary core API, which is the point. No GUI control reaches this today,
// and that is exactly why the guard is cheap to add now rather than later.
std::unique_ptr<Assembly> assemblyThatViolatesRule8()
{
    ChipletFormat fmt;
    auto assembly = fmt.load(fixturePath("with_io_pads.chiplet"));
    if (!assembly) return nullptr;

    auto iface = std::make_unique<Interface>("IF_BAD", InterfaceType::MicroBump);
    InterfaceEndpoint from;
    from.component = "interposer";
    from.surface = "top";
    from.portLayer = "TopMetal2";   // the layer the wire_bond pads sit on
    iface->set_from(from);
    InterfaceEndpoint to;
    to.component = "U1";
    to.surface = "bottom";
    to.portLayer = "TopMetal2";
    iface->set_to(to);
    assembly->add_interface(std::move(iface));
    return assembly;
}

TEST(SaveTimeReaderCheck, ARejectedDocumentIsNotWrittenAndIsNotLost)
{
    auto assembly = assemblyThatViolatesRule8();
    ASSERT_TRUE(assembly);

    const std::string out =
        (std::filesystem::temp_directory_path() / "rule8_refused.chiplet").string();
    const std::string sidecar = out + ".rejected";
    std::filesystem::remove(sidecar);

    // Put known bytes at the target so "unchanged" is a claim with a witness
    // rather than "the file happens not to exist".
    const std::string before = "# the previous good version\n";
    { std::ofstream f(out); f << before; }

    ChipletFormat fmt;
    std::string message;
    try {
        fmt.save(*assembly, out);
        FAIL() << "the document was written; the reader refuses it, so this "
                  "program just produced a file the format rejects";
    } catch (const ChipletFormatException& e) {
        message = e.what();
    }

    // 1. Nothing was written. This is the half that matters most: a refusal
    //    after the write would have destroyed the previous version and told the
    //    user about it afterwards.
    EXPECT_EQ(readWholeFile(out), before)
        << "the target was modified by a save that then refused";

    // 2. Nothing was lost either. The refusal is loss-free by construction, not
    //    because this program happens to have no editor: the emitted text lands
    //    beside the target under a name no *.chiplet glob matches, so a user
    //    with an hour of unsaved work always has it on disk somewhere, and no
    //    sweep in the ecosystem picks a refused document up as input.
    ASSERT_TRUE(std::filesystem::exists(sidecar))
        << "the rejected text was dropped; a user whose only copy is in RAM "
           "loses it";
    const std::string kept = readWholeFile(sidecar);
    EXPECT_NE(kept.find("IF_BAD"), std::string::npos)
        << "the sidecar is not the document that was refused";

    // 3. The check IS the reader, not a local re-implementation of its rules.
    //    Asserting the reader's own sentence is what discriminates those two: a
    //    hand-written rule-8 check in this file could satisfy everything above
    //    and would go stale on the next re-vendor, which is the failure this
    //    whole mechanism exists to survive.
    std::string readerSays;
    try {
        chiplet_format_io::LoadOptions opts;
        opts.validate = true;
        opts.allow_intermediate = true;
        (void)chiplet_format_io::loads(kept, opts);
        FAIL() << "the reader accepts the text save() refused";
    } catch (const chiplet_format_io::ChipletFormatError& e) {
        readerSays = e.what();
    }
    EXPECT_NE(message.find(readerSays), std::string::npos)
        << "the refusal does not quote the reader that produced it:\n"
        << message << "\n---\n" << readerSays;

    // 4. And it does not overclaim. This check is necessary for reopening, not
    //    sufficient: a ${VAR} document passes it and can still fail on open,
    //    because path resolution lives in this host and never in the library.
    EXPECT_EQ(message.find("cannot be reopened"), std::string::npos)
        << "the message promises more than the check tested:\n" << message;

    std::filesystem::remove(out);
    std::filesystem::remove(sidecar);
}

// The negative control. Everything above is satisfied by a check that refuses
// every document, so "this refuses only what the reader refuses" needs the other
// side asserted too. with_flow is named explicitly because the appended source
// slice is the one surface where save() hands the reader text no emitter built.
// The rescue write must not be redirected by a symlink sitting where the
// sidecar goes. write_file_atomically resolves a symlinked target on purpose,
// because a normal save through a link must replace what the link points at,
// the way an ofstream would. Applied to `<path>.rejected` that same rule aims
// the rescue at whatever the link names, which in the worst case is the very
// document the refusal message promises is untouched.
TEST(SaveTimeReaderCheck, TheRescueWriteIsNotRedirectedBySymlink)
{
    namespace fs = std::filesystem;
    auto assembly = assemblyThatViolatesRule8();
    ASSERT_TRUE(assembly != nullptr);

    const fs::path dir = fs::temp_directory_path() / "sidecar_link_probe";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path target = dir / "design.chiplet";
    const fs::path sidecar = dir / "design.chiplet.rejected";

    const std::string original = "original bytes, must survive\n";
    { std::ofstream f(target); f << original; }

    // The sidecar name is a symlink pointing back at the document.
    std::error_code ec;
    fs::create_symlink(target, sidecar, ec);
    ASSERT_FALSE(ec) << "could not create the symlink the test is about";
    ASSERT_TRUE(fs::is_symlink(sidecar)) << "precondition: sidecar is a link";

    ChipletFormat fmt;
    EXPECT_THROW(fmt.save(*assembly, target.string()), ChipletFormatException);

    // The promise the refusal message makes.
    EXPECT_EQ(readWholeFile(target.string()), original)
        << "the rescue write followed the symlink and destroyed the document "
           "the refusal message says is untouched";

    // ...and the rescue still landed, at the name it claims.
    EXPECT_TRUE(fs::exists(sidecar)) << "no rescue file was written";
    EXPECT_FALSE(fs::is_symlink(sidecar))
        << "the sidecar is still a link, so the rescue text went somewhere else";

    fs::remove_all(dir);
}

TEST(SaveTimeReaderCheck, LegitimateDocumentsStillSave)
{
    for (const char* fixture : {"with_io_pads.chiplet", "with_flow.chiplet",
                                "pipeline_smoke.chiplet"}) {
        ChipletFormat fmt;
        auto assembly = fmt.load(fixturePath(fixture));
        ASSERT_TRUE(assembly) << fixture;

        const std::string out =
            (std::filesystem::temp_directory_path() /
             (std::string("selfcheck_ok_") + fixture)).string();
        EXPECT_NO_THROW(fmt.save(*assembly, out))
            << "the self-check refused a document nobody edited: " << fixture;
        EXPECT_FALSE(std::filesystem::exists(out + ".rejected")) << fixture;
        std::filesystem::remove(out);
    }
}

// What a refused save does with the text it was about to write.
//
// The default is Preserve and has to stay Preserve. A refusal writes
// <path>.rejected so that an hour of work living only in RAM does not leave
// with it. Suppress exists for one shape: a save whose entire content was just
// derived from a file the user still has. An import of a loose GDS is the case;
// nothing there is rescued by a sidecar, because nothing would be lost, and
// what the sidecar does instead is leave debris at a destination the user chose
// from a document they never authored, named after a file that does not exist.
//
// Both arms live in one test on purpose. "No sidecar appeared" is also true
// when the refusal never fired, when the assembly turned out to be acceptable,
// and when serialization never ran at all, so a lone Suppress test stays green
// for three reasons that have nothing to do with the flag. The pair is the
// discriminator: one document, one route, one field different, opposite
// outcomes, and both arms pinned to the SAME refusal by the reader's own
// sentence rather than by the fact that something was thrown.
TEST(RejectedOutputPolicy, SuppressDropsTheSidecarAndChangesNothingElse)
{
    namespace fs = std::filesystem;

    // The refusal both arms drive from, and the reader sentence that identifies
    // it. Taken from the reader here rather than spelled out, so a re-vendor
    // that rewords it cannot leave this test asserting a string nobody emits.
    auto probe = assemblyThatViolatesRule8();
    ASSERT_TRUE(probe != nullptr);
    std::string readerSays;
    {
        ChipletFormat fmt;
        const std::string scratch =
            (fs::temp_directory_path() / "save_policy_probe.chiplet").string();
        fs::remove(scratch);
        fs::remove(scratch + ".rejected");
        try {
            (void)fmt.save_snapshot(*probe, scratch, SaveRequest{});
            FAIL() << "the reader accepts the document this test is built on; "
                      "every assertion below would be vacuous";
        } catch (const ChipletFormatException&) {
            // Deliberately NOT e.what(). That is the host's whole wrapped
            // refusal and it names the path it refused, so it can never be a
            // substring of another arm's message; asserting it was an
            // instrument error, not evidence about suppression. Carving a
            // sentence back out of that message would only trade it for a
            // coupling to the host's message format.
            //
            // The reader's own diagnostic is available directly, from the
            // rejected bytes the Preserve default just wrote, run back through
            // chiplet_format_io. Same pattern as SaveTimeReaderCheck below, and
            // it has the property that matters here: the needle comes from the
            // reader, so a re-vendor that rewords the finding updates this test
            // automatically, and a host that stops quoting the reader fails it.
        }
        ASSERT_TRUE(fs::exists(scratch + ".rejected"))
            << "the default policy wrote no rejected text, so there are no "
               "bytes to take the reader's own diagnostic from";
        const std::string rejected = readWholeFile(scratch + ".rejected");
        try {
            chiplet_format_io::LoadOptions opts;
            opts.validate = true;
            opts.allow_intermediate = true;
            (void)chiplet_format_io::loads(rejected, opts);
            FAIL() << "the reader accepts the text save_snapshot refused; every "
                      "assertion below would be vacuous";
        } catch (const chiplet_format_io::ChipletFormatError& e) {
            readerSays = e.what();
        }
        fs::remove(scratch);
        fs::remove(scratch + ".rejected");
    }
    // An empty needle would make every find() below return 0, which is not
    // npos, so the arms would PASS on a message format that stopped carrying a
    // reader sentence at all. A test that succeeds when its input disappears is
    // the shape these tests exist to remove.
    ASSERT_FALSE(readerSays.empty())
        << "the reader's diagnostic came back empty; every assertion below "
           "would be satisfied by any message at all";
    // The rule-8 complaint, not a generic refusal: if save() started refusing
    // for some unrelated reason the arms below would still agree with each
    // other and prove nothing about the reader check.
    ASSERT_NE(readerSays.find("TopMetal2"), std::string::npos)
        << "the probe refusal is not the reader's rule-8 finding: " << readerSays;

    ASSERT_EQ(SaveRequest{}.rejected_output, RejectedOutput::Preserve)
        << "the default changed; every caller that never heard of this field "
           "just started discarding the text of its own refusals";

    struct Arm {
        const char* name;
        RejectedOutput policy;
        bool expectSidecar;
    };
    const Arm arms[] = {
        {"save_policy_preserve.chiplet", RejectedOutput::Preserve, true},
        {"save_policy_suppress.chiplet", RejectedOutput::Suppress, false},
    };

    for (const Arm& arm : arms) {
        auto assembly = assemblyThatViolatesRule8();
        ASSERT_TRUE(assembly != nullptr) << arm.name;

        const std::string out = (fs::temp_directory_path() / arm.name).string();
        const std::string sidecar = out + ".rejected";
        fs::remove(out);
        fs::remove(sidecar);

        SaveRequest request;
        request.rejected_output = arm.policy;

        ChipletFormat fmt;
        std::string message;
        try {
            (void)fmt.save_snapshot(*assembly, out, request);
            FAIL() << "the document was written: " << arm.name;
        } catch (const ChipletFormatException& e) {
            message = e.what();
        }

        // 1. Suppress is not a bypass. It changes what happens to the rejected
        //    text and nothing about whether the save is refused, so both arms
        //    have to be the same refusal, quoting the same reader.
        EXPECT_NE(message.find(readerSays), std::string::npos)
            << arm.name << ": not the refusal this test is about:\n" << message;

        // 2. Neither arm creates the destination.
        EXPECT_FALSE(fs::exists(out))
            << arm.name << ": a refused save created its target";

        // 3. The field's whole visible effect.
        EXPECT_EQ(fs::exists(sidecar), arm.expectSidecar)
            << arm.name << ": rejected-text file "
            << (arm.expectSidecar ? "missing" : "written anyway");
        if (arm.expectSidecar) {
            EXPECT_NE(readWholeFile(sidecar).find("IF_BAD"), std::string::npos)
                << arm.name << ": the sidecar is not the document that was "
                               "refused";
        }

        // 4. And it says which of the two happened. Telling someone their text
        //    "could not be written" when the caller asked for it not to be
        //    written sends them looking for a disk problem that is not there.
        if (arm.expectSidecar) {
            EXPECT_NE(message.find("was written to"), std::string::npos)
                << arm.name << ": " << message;
        } else {
            EXPECT_EQ(message.find("could not be written"), std::string::npos)
                << arm.name << ": the message reports a failure where the "
                               "caller made a choice:\n" << message;
            EXPECT_NE(message.find("by request"), std::string::npos)
                << arm.name << ": the message does not say the omission was "
                               "asked for:\n" << message;
        }

        fs::remove(out);
        fs::remove(sidecar);
    }
}

// What a caller is allowed to do about a failed save.
//
// The kind exists for one decision and the decision has exactly three answers:
// write nothing anywhere, ask for a different name, try a different
// destination. Getting it wrong is not cosmetic. A document the reader refused,
// reported as a destination problem, is answered by writing that same document
// into a second directory; a name that is merely taken, reported the same way,
// is answered by writing into a private directory while the user is never asked
// for the different name that is the actual remedy, and it is answered
// silently, because the check that should have caught it is the check that just
// did.
//
// Each test below asserts a kind AND the outcome that kind is supposed to
// produce, because a kind nothing acts on is a label.

TEST(SaveFailureKind, AnOccupiedDestinationSaysOccupiedAndAFreshOneIsWritten)
{
    namespace fs = std::filesystem;
    ChipletFormat fmt;
    auto assembly = fmt.load(fixturePath("minimal.chiplet"));
    ASSERT_TRUE(assembly != nullptr);

    const fs::path dir =
        fs::temp_directory_path() / "save_policy_kind_occupied";
    fs::remove_all(dir);
    fs::create_directories(dir);

    // The positive arm comes first, so a save that refused everything could not
    // satisfy the rest of this test.
    const fs::path fresh = dir / "fresh.chiplet";
    EXPECT_NO_THROW((void)fmt.save_snapshot(*assembly, fresh.string(), SaveRequest{}));
    EXPECT_TRUE(fs::exists(fresh)) << "a fresh destination was not written";

    // Three shapes of occupied. The dangling link is the one a plain exists()
    // check calls free and then writes through.
    const std::string mine = "not mine to replace\n";
    const fs::path regular = dir / "regular.chiplet";
    { std::ofstream f(regular); f << mine; }
    const fs::path dangling = dir / "dangling.chiplet";
    std::error_code ec;
    fs::create_symlink(dir / "nothing-is-here", dangling, ec);
    ASSERT_FALSE(ec) << "could not create the dangling link this test is about";
    const fs::path directory = dir / "directory.chiplet";
    fs::create_directories(directory);

    for (const fs::path& taken : {regular, dangling, directory}) {
        try {
            (void)fmt.save_snapshot(*assembly, taken.string(), SaveRequest{});
            FAIL() << "wrote over an occupied destination: " << taken;
        } catch (const ChipletFormatException& e) {
            EXPECT_EQ(e.kind(), FailureKind::DestinationOccupied)
                << taken << " reported as something a different DIRECTORY would "
                            "fix, when what it needs is a different NAME: "
                << e.what();
        }
    }

    // The kind is only half the claim; nothing may have been touched either.
    EXPECT_EQ(readWholeFile(regular.string()), mine);
    EXPECT_TRUE(fs::is_symlink(dangling)) << "the link was replaced";
    EXPECT_TRUE(fs::is_directory(directory)) << "the directory was replaced";

    fs::remove_all(dir);
}

TEST(SaveFailureKind, ADirectoryThatCannotTakeTheFileIsIo)
{
    namespace fs = std::filesystem;
    if (::geteuid() == 0) {
        GTEST_SKIP() << "running as root: directory permissions are not enforced";
    }
    ChipletFormat fmt;
    auto assembly = fmt.load(fixturePath("minimal.chiplet"));
    ASSERT_TRUE(assembly != nullptr);

    const fs::path dir = fs::temp_directory_path() / "save_policy_kind_io";
    fs::remove_all(dir);
    fs::create_directories(dir);
    ASSERT_EQ(::chmod(dir.c_str(), 0555), 0);

    // Assert the failure this test is named after exists here, or the assertion
    // below would be about a save that simply worked.
    const int probe = ::open((dir / "probe").c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
    if (probe >= 0) ::close(probe);
    ASSERT_LT(probe, 0) << "this user can write into a 0555 directory, so the "
                           "failure under test was never created";

    const fs::path target = dir / "design.chiplet";
    try {
        (void)fmt.save_snapshot(*assembly, target.string(), SaveRequest{});
        FAIL() << "the save reported success in a directory it cannot write";
    } catch (const ChipletFormatException& e) {
        EXPECT_EQ(e.kind(), FailureKind::DestinationIo)
            << "a destination that cannot take the file is the one case where "
               "trying a different destination is the right answer: "
            << e.what();
    }

    ASSERT_EQ(::chmod(dir.c_str(), 0755), 0);
    fs::remove_all(dir);
}

TEST(SaveFailureKind, ADocumentTheReaderRefusesIsNeitherOccupiedNorIo)
{
    namespace fs = std::filesystem;
    auto assembly = assemblyThatViolatesRule8();
    ASSERT_TRUE(assembly != nullptr);

    const fs::path dir = fs::temp_directory_path() / "save_policy_kind_refusal";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path target = dir / "refused.chiplet";

    ChipletFormat fmt;
    try {
        (void)fmt.save_snapshot(*assembly, target.string(), SaveRequest{});
        FAIL() << "the reader refuses this document, so the save should have";
    } catch (const ChipletFormatException& e) {
        // The whole reason the default is the safe one. Either other kind here
        // means "this destination was the problem", and the only sensible thing
        // to do with that answer is write the same document somewhere else,
        // which is how a document the format rejects reaches disk anyway.
        EXPECT_EQ(e.kind(), FailureKind::Other)
            << "a refused document reported as a destination problem invites "
               "the caller to write it somewhere else: "
            << e.what();
    }
    EXPECT_FALSE(fs::exists(target)) << "the refused document was written";

    fs::remove_all(dir);
}

// A concurrent creator never loses its file, whichever moment it arrives at.
//
// This is NOT the final-publication test and an earlier version of it claimed
// to be. Counting that the racer created the target says only that it did so at
// some point in the iteration: for an early delay that lands before
// admit_destination, so the save dies at the admission check and the exclusive
// create is never reached, and both of the anti-vacuity counters below are
// satisfiable by a run in which the link EEXIST branch executed exactly zero
// times. The deterministic test for that interval is
// ATargetCreatedInsideTheExclusiveCreateIsNotOverwritten; this one is
// supplemental and proves the weaker property in its name across a range of
// real interleavings.
//
// Two outcomes are correct and any third is the defect. Both must occur, or the
// sweep exercised one case and says so rather than passing, and the two
// refusal routes are counted apart by their messages so the run reports which
// interleavings it actually produced.
TEST(SaveFailureKind, AConcurrentCreatorNeverLosesItsFile)
{
    namespace fs = std::filesystem;
    ChipletFormat fmt;
    auto assembly = fmt.load(fixturePath("minimal.chiplet"));
    ASSERT_TRUE(assembly != nullptr);

    const fs::path dir = fs::temp_directory_path() / "save_policy_kind_race";
    fs::remove_all(dir);
    fs::create_directories(dir);

    // How long an unraced save takes HERE, so the sweep covers it rather than a
    // number that was true on the machine this was written on.
    const fs::path timed = dir / "timed.chiplet";
    const auto t0 = std::chrono::steady_clock::now();
    ASSERT_NO_THROW((void)fmt.save_snapshot(*assembly, timed.string(), SaveRequest{}));
    const long saveMicros = static_cast<long>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - t0).count());
    fs::remove(timed);

    const std::string racerBytes = "written by someone else\n";
    const int steps = 48;
    const long span = std::max<long>(saveMicros * 2, 2000);
    int weWon = 0;
    int racerWon = 0;
    int viaAdmission = 0;
    int viaExclusiveCreate = 0;

    for (int i = 0; i < steps; ++i) {
        const fs::path target = dir / ("race_" + std::to_string(i) + ".chiplet");
        const long delay = (span * i) / (steps - 1);

        std::atomic<int> racerFd{-2};
        std::thread racer([&] {
            std::this_thread::sleep_for(std::chrono::microseconds(delay));
            const int fd = ::open(target.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
            if (fd >= 0) {
                const ssize_t n = ::write(fd, racerBytes.data(), racerBytes.size());
                (void)n;
                ::close(fd);
            }
            racerFd.store(fd);
        });

        bool saved = true;
        std::string message;
        FailureKind kind = FailureKind::Other;
        try {
            (void)fmt.save_snapshot(*assembly, target.string(), SaveRequest{});
        } catch (const ChipletFormatException& e) {
            saved = false;
            message = e.what();
            kind = e.kind();
        }
        racer.join();

        if (racerFd.load() < 0) {
            ++weWon;
            EXPECT_TRUE(saved)
                << "nothing else ever created " << target
                << " yet the save refused: " << message;
            if (saved) {
                EXPECT_NE(readWholeFile(target.string()).find("format_version"),
                          std::string::npos)
                    << "the destination does not hold the document that was saved";
            }
        } else {
            ++racerWon;
            EXPECT_FALSE(saved)
                << "another writer created " << target
                << " and the save replaced it";
            EXPECT_EQ(kind, FailureKind::DestinationOccupied)
                << "a name taken by someone else is not a reason to write this "
                   "document into a different directory: " << message;
            EXPECT_EQ(readWholeFile(target.string()), racerBytes)
                << "the other writer's file was overwritten";
            // Same kind, two routes, and the messages are the only thing that
            // tells them apart. Recorded rather than asserted: which route a
            // given interleaving takes is exactly the luck this test does not
            // control, and requiring the link route here would make a correct
            // run red on a fast machine.
            if (message.find("Refusing to overwrite an unrelated destination")
                != std::string::npos) {
                ++viaAdmission;
            } else if (message.find("Could not create a new destination without "
                                    "overwriting") != std::string::npos) {
                ++viaExclusiveCreate;
            }
        }
    }

    // Anti-vacuity. Without both, the sweep either never raced or never let the
    // save through, and every assertion above was about a single case.
    EXPECT_GT(racerWon, 0) << "no iteration was raced; the sweep proved nothing";
    EXPECT_GT(weWon, 0)
        << "the save never won an iteration, so this only re-tested an already "
           "occupied destination. Measured save: " << saveMicros
        << "us, sweep span: " << span << "us";
    EXPECT_EQ(viaAdmission + viaExclusiveCreate, racerWon)
        << "a refusal came from neither known route, so the two are no longer "
           "the only ways an occupied destination is reported";
    std::cerr << "[      INFO ] refusals via admission: " << viaAdmission
              << ", via exclusive create: " << viaExclusiveCreate << "\n";

    fs::remove_all(dir);
}

// A name that is taken stays taken even when nothing can resolve it.
//
// This is an ordering test wearing a symlink. Establishing a destination's
// identity follows symlinks, and a link pointing at itself resolves to ELOOP
// rather than to a path, so asking the resolver before asking what is at the
// name reports a plainly OCCUPIED destination as a filesystem problem. A caller
// with a fallback then answers it by writing the document somewhere else while
// the name the user chose sits there occupied, and reports success. Occupancy
// has to be decided from the leaf's own status, which follows nothing.
TEST(SaveFailureKind, ACyclicSymlinkIsOccupiedAndNotAResolutionFailure)
{
    namespace fs = std::filesystem;
    ChipletFormat fmt;
    auto assembly = fmt.load(fixturePath("minimal.chiplet"));
    ASSERT_TRUE(assembly != nullptr);

    const fs::path dir = fs::temp_directory_path() / "save_policy_cycle";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path self = dir / "self.chiplet";

    std::error_code ec;
    fs::create_symlink("self.chiplet", self, ec);
    ASSERT_FALSE(ec) << "could not create the self-referential link";

    // The premise, asserted rather than assumed: if resolution DID succeed here
    // the ordering this test is named after would never be exercised and it
    // would pass for the wrong reason.
    std::error_code resolveEc;
    (void)fs::weakly_canonical(self, resolveEc);
    ASSERT_TRUE(static_cast<bool>(resolveEc))
        << "this platform resolves a self-referential symlink, so the ordering "
           "under test is not reachable from here";

    try {
        (void)fmt.save_snapshot(*assembly, self.string(), SaveRequest{});
        FAIL() << "wrote to a name occupied by a self-referential link";
    } catch (const ChipletFormatException& e) {
        EXPECT_EQ(e.kind(), FailureKind::DestinationOccupied)
            << "a taken name was reported as something a different DIRECTORY "
               "would fix, so a caller with a fallback would have written the "
               "document elsewhere and called it success: "
            << e.what();
    }

    EXPECT_TRUE(fs::is_symlink(self)) << "the link was replaced";
    std::vector<std::string> left;
    for (const auto& entry : fs::directory_iterator(dir)) {
        left.push_back(entry.path().filename().string());
    }
    EXPECT_EQ(left, (std::vector<std::string>{"self.chiplet"}))
        << "the refusal left something behind";

    fs::remove_all(dir);
}

// The final publication interval, deterministically.
//
// The interval is between the last admission check and the exclusive create,
// and it is a few instructions wide with nothing in it a test can hook. So the
// seam at the top of this file defines ::link for this binary, arms it for one
// call on one path, and creates the target from inside the syscall itself: by
// construction after every check the writer makes and before the create it
// makes them for. What the writer then meets is a real EEXIST from a real file
// that arrived at exactly the moment nothing else can reach.
//
// Both shapes go through it. The dangling symlink is the one a plain exists()
// check calls free and writes straight through, and link does not follow the
// final component, so it is EEXIST there too.
TEST(SaveFailureKind, ATargetCreatedInsideTheExclusiveCreateIsNotOverwritten)
{
    namespace fs = std::filesystem;
    ChipletFormat fmt;
    auto assembly = fmt.load(fixturePath("minimal.chiplet"));
    ASSERT_TRUE(assembly != nullptr);

    // The instrument has to work before anything measured with it means
    // something. A null next-link would make every save below fail for a reason
    // that has nothing to do with the property under test.
    ASSERT_NE(::dlsym(RTLD_NEXT, "link"), nullptr)
        << "the real link could not be resolved, so this test would be "
           "measuring its own interposer failing";

    const fs::path dir = fs::temp_directory_path() / "save_policy_link_seam";
    fs::remove_all(dir);
    fs::create_directories(dir);

    struct Shape { const char* name; bool dangling; };
    for (const Shape& shape : {Shape{"regular.chiplet", false},
                               Shape{"dangling.chiplet", true}}) {
        const fs::path target = dir / shape.name;
        std::string message;
        FailureKind kind = FailureKind::Other;
        bool threw = false;
        int fired = 0;
        {
            const SeamArmed armed(target.string(), shape.dangling);
            try {
                (void)fmt.save_snapshot(*assembly, target.string(), SaveRequest{});
            } catch (const ChipletFormatException& e) {
                threw = true;
                message = e.what();
                kind = e.kind();
            }
            fired = armed.firedSince();
        }
        // Disarmed on the way out of that scope however this went, so nothing
        // later in this binary inherits an armed trigger.
        ASSERT_FALSE(chipletLinkSeam().armed.load()) << shape.name;

        ASSERT_EQ(fired, 1)
            << shape.name << ": the seam never ran, so the publication was "
               "never reached and nothing below is about it";

        ASSERT_TRUE(threw)
            << shape.name << ": a target that appeared inside the exclusive "
               "create was overwritten, which is the whole failure this "
               "publication exists to prevent";
        EXPECT_EQ(kind, FailureKind::DestinationOccupied)
            << shape.name << ": " << message;

        // The route, not only the kind. The admission check cannot have
        // produced this one: it ran before the seam did, when the name was
        // still free.
        EXPECT_NE(message.find("Could not create a new destination without "
                               "overwriting"), std::string::npos)
            << shape.name << ": not the exclusive create's own refusal: "
            << message;
        EXPECT_EQ(message.find("Refusing to overwrite an unrelated destination"),
                  std::string::npos)
            << shape.name << ": this came from the admission check, so the "
                             "interval was not exercised: " << message;

        // And what arrived in the interval is exactly as it was left.
        if (shape.dangling) {
            EXPECT_TRUE(fs::is_symlink(target)) << "the link was replaced";
            EXPECT_FALSE(fs::exists(target))
                << "the dangling link acquired a target, so it was written "
                   "through";
        } else {
            EXPECT_EQ(readWholeFile(target.string()),
                      std::string(chipletSeamBytes()))
                << "the file that arrived in the interval was overwritten";
        }
    }

    // No half-written temporary left behind by either refusal.
    std::vector<std::string> left;
    for (const auto& entry : fs::directory_iterator(dir)) {
        left.push_back(entry.path().filename().string());
    }
    std::sort(left.begin(), left.end());
    EXPECT_EQ(left, (std::vector<std::string>{"dangling.chiplet", "regular.chiplet"}))
        << "a refused publication left something in the directory";

    fs::remove_all(dir);
}

} // namespace
} // namespace chiplet
