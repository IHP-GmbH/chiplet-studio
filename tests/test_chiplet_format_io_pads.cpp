/**
 * test_chiplet_format_io_pads.cpp - Unit tests for I/O pad schema parsing
 */

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include "formats/ChipletFormat.h"
#include "core/IOPad.h"

namespace chiplet {
namespace {

std::string fixturePath(const std::string& filename)
{
    return std::string(FIXTURES_DIR) + "/" + filename;
}

TEST(ChipletFormatIOPads, LoadAssemblyIoTechnology)
{
    ChipletFormat format;
    auto assembly = format.load(fixturePath("with_io_pads.chiplet"));
    ASSERT_NE(assembly, nullptr);
    EXPECT_EQ(assembly->io_technology(), "wire_bond");
}

TEST(ChipletFormatIOPads, LoadInterposerIoPads)
{
    ChipletFormat format;
    auto assembly = format.load(fixturePath("with_io_pads.chiplet"));
    ASSERT_NE(assembly, nullptr);

    auto* interposer = assembly->component(ComponentID("interposer"));
    ASSERT_NE(interposer, nullptr);

    const auto& pads = interposer->io_pads();
    ASSERT_EQ(pads.size(), 3u);

    EXPECT_EQ(pads[0].id(), "J1");
    EXPECT_EQ(pads[0].io_class(), IOClass::WireBond);
    EXPECT_EQ(pads[0].net(), "VDD_EXT");
    EXPECT_DOUBLE_EQ(pads[0].position().x, -1500.0);
    EXPECT_DOUBLE_EQ(pads[0].position().y, 1200.0);
    EXPECT_DOUBLE_EQ(pads[0].size().x, 100.0);
    EXPECT_DOUBLE_EQ(pads[0].size().y, 100.0);
    EXPECT_EQ(pads[0].layer(), "TopMetal2");

    EXPECT_EQ(pads[2].id(), "J3");
    EXPECT_DOUBLE_EQ(pads[2].size().x, 150.0);
}

TEST(ChipletFormatIOPads, LoadExternalNetsFlag)
{
    ChipletFormat format;
    auto assembly = format.load(fixturePath("with_io_pads.chiplet"));
    ASSERT_NE(assembly, nullptr);

    auto external = assembly->netlist().nets_external();
    EXPECT_EQ(external.size(), 3u);

    auto* internalNet = assembly->netlist().net("INTERNAL_NET");
    ASSERT_NE(internalNet, nullptr);
    EXPECT_FALSE(internalNet->external());

    auto* vdd = assembly->netlist().net("VDD_EXT");
    ASSERT_NE(vdd, nullptr);
    EXPECT_TRUE(vdd->external());
}

TEST(ChipletFormatIOPads, RoundTripPreservesIoPadsAndExternalFlag)
{
    ChipletFormat format;
    auto assembly = format.load(fixturePath("with_io_pads.chiplet"));
    ASSERT_NE(assembly, nullptr);

    auto tmpDir = std::filesystem::temp_directory_path() / "chiplet_io_pads_rt";
    std::filesystem::create_directories(tmpDir);
    auto outPath = (tmpDir / "out.chiplet").string();
    format.save(*assembly, outPath);

    auto reloaded = format.load(outPath);
    ASSERT_NE(reloaded, nullptr);
    EXPECT_EQ(reloaded->io_technology(), "wire_bond");

    auto* interposer = reloaded->component(ComponentID("interposer"));
    ASSERT_NE(interposer, nullptr);
    EXPECT_EQ(interposer->io_pads().size(), 3u);
    EXPECT_EQ(interposer->io_pads()[1].net(), "GND_EXT");

    EXPECT_EQ(reloaded->netlist().nets_external().size(), 3u);

    std::filesystem::remove(outPath);
}

TEST(ChipletFormatIOPads, IoClassStringRoundTrip)
{
    EXPECT_EQ(io_class_to_string(IOClass::WireBond),    "wire_bond");
    EXPECT_EQ(io_class_to_string(IOClass::FlippedBump), "flipped_bump");
    EXPECT_EQ(io_class_to_string(IOClass::TSVBump),     "tsv_bump");
    EXPECT_EQ(string_to_io_class("wire_bond"),    IOClass::WireBond);
    EXPECT_EQ(string_to_io_class("flipped_bump"), IOClass::FlippedBump);
    EXPECT_EQ(string_to_io_class("tsv_bump"),     IOClass::TSVBump);
    EXPECT_THROW(string_to_io_class("garbage"), std::runtime_error);
}

TEST(ChipletFormatIOPads, ComponentApiAddAndClear)
{
    Component c("interposer", ComponentType::Interposer);
    EXPECT_EQ(c.io_pad_count(), 0u);

    IOPad pad("J1", IOClass::WireBond);
    pad.set_net("VDD");
    pad.set_position({1.0, 2.0});
    pad.set_size({100.0, 100.0});
    c.add_io_pad(pad);
    EXPECT_EQ(c.io_pad_count(), 1u);
    EXPECT_EQ(c.io_pads()[0].net(), "VDD");

    c.clear_io_pads();
    EXPECT_EQ(c.io_pad_count(), 0u);
}

TEST(ChipletFormatIOPads, LoadRealHypToGdsOutput)
{
    // Smoke: parse a .chiplet produced by the actual hyp_to_gds.py pipeline.
    // Asserts schema compatibility end-to-end (hyp_to_gds -> ChipletFormat::load).
    ChipletFormat format;
    auto assembly = format.load(fixturePath("pipeline_smoke.chiplet"));
    ASSERT_NE(assembly, nullptr);

    auto* interposer = assembly->component(ComponentID("interposer"));
    ASSERT_NE(interposer, nullptr);

    const auto& pads = interposer->io_pads();
    ASSERT_EQ(pads.size(), 4u);

    std::map<std::string, std::pair<double, double>> expected = {
        {"J1", {500.0, 1000.0}},
        {"J2", {1500.0, 1000.0}},
        {"J3", {500.0, 2000.0}},
        {"J4", {1500.0, 2000.0}},
    };
    std::set<std::string> expected_nets = {"VDD_EXT", "GND_EXT", "OUT0", "OUT1"};
    std::set<std::string> seen_nets;

    for (const auto& p : pads) {
        EXPECT_EQ(p.io_class(), IOClass::WireBond);
        EXPECT_EQ(p.layer(), "TopMetal2");
        EXPECT_DOUBLE_EQ(p.size().x, 100.0);
        EXPECT_DOUBLE_EQ(p.size().y, 100.0);
        ASSERT_TRUE(expected.count(p.id())) << "unexpected pad id: " << p.id();
        EXPECT_DOUBLE_EQ(p.position().x, expected[p.id()].first);
        EXPECT_DOUBLE_EQ(p.position().y, expected[p.id()].second);
        seen_nets.insert(p.net());
    }
    EXPECT_EQ(seen_nets, expected_nets);
}

// `io_class` and `layer` are OPTIONAL in the schema (io_pad.required
// is exactly id and position), and this program used to invent both: IOPad
// defaulted them to wire_bond and TopMetal2, the loader assigned them only when
// declared, and the writer emitted them unconditionally. Load and save a pad
// that declared neither and it came back declaring both.
//
// That is not cosmetic. Validation rule 8 is vacuous for a pad with no
// io_class, and it skips any pad whose layer is not the interface's port layer,
// so stamping the two turns the rule from vacuous to binding on a document
// whose author never said any of it. A reader that enforces rule 8 then refuses
// a file this program wrote from one it had just accepted, and no control in
// this program can repair it.
//
// The distinction the test has to make is between "kept" and "unchanged". A
// writer that echoed the whole pad back would pass a check that only looks at
// the pad it declared something for, so both halves are asserted on the same
// document: the pad that declared nothing must still declare nothing, and the
// pad that declared exactly one field must still declare exactly that one, with
// no partner field appearing beside it.
TEST(ChipletFormatIOPads, OnlyDeclaredPadFieldsAreWrittenBack)
{
    const std::string src =
        (std::filesystem::temp_directory_path() / "pad_declaredness.chiplet").string();
    {
        std::ofstream f(src);
        f << R"(format_version: "1.0"

assembly:
  name: "Pad declaredness"
  units: "um"

technologies:
  intm4tm2:
    description: "Interposer"
    dbu: 0.001

components:
  - id: interposer
    type: interposer
    technology: intm4tm2
    dimensions: {width: 4000, height: 3000, thickness: 10.8103}
    position: {x: 0, y: 0, z: 0}
    io_pads:
      - id: BARE
        position: {x: 10.0, y: 20.0}
        size: {x: 100.0, y: 100.0}
      - id: CLASS_ONLY
        io_class: flipped_bump
        position: {x: 30.0, y: 40.0}
        size: {x: 100.0, y: 100.0}
      - id: LAYER_ONLY
        position: {x: 50.0, y: 60.0}
        size: {x: 100.0, y: 100.0}
        layer: TopMetal1
)";
    }

    ChipletFormat fmt;
    auto assembly = fmt.load(src);
    ASSERT_TRUE(assembly);

    auto* interposer = assembly->component(ComponentID("interposer"));
    ASSERT_NE(interposer, nullptr);
    const auto& pads = interposer->io_pads();
    ASSERT_EQ(pads.size(), 3u);

    // The model carries absence, which is what makes writing it back possible.
    EXPECT_FALSE(pads[0].io_class().has_value());
    EXPECT_FALSE(pads[0].layer().has_value());
    EXPECT_EQ(pads[1].io_class(), IOClass::FlippedBump);
    EXPECT_FALSE(pads[1].layer().has_value());
    EXPECT_FALSE(pads[2].io_class().has_value());
    EXPECT_EQ(pads[2].layer(), "TopMetal1");

    const std::string out =
        (std::filesystem::temp_directory_path() / "pad_declaredness_out.chiplet").string();
    ASSERT_NO_THROW(fmt.save(*assembly, out));

    std::ifstream in(out);
    std::stringstream buf;
    buf << in.rdbuf();
    const std::string written = buf.str();

    // Per pad, count what the emitted document declares for it. Slicing by pad
    // id rather than searching the whole file is what lets one pad's declared
    // field prove the other pad's is absent, instead of a single global find
    // that any one occurrence satisfies.
    auto padBlock = [&written](const std::string& id) {
        const auto at = written.find("id: \"" + id + "\"");
        EXPECT_NE(at, std::string::npos) << "pad " << id << " is gone:\n" << written;
        if (at == std::string::npos) return std::string();
        const auto next = written.find("- id:", at);
        return written.substr(at, next == std::string::npos ? std::string::npos : next - at);
    };

    const std::string bare = padBlock("BARE");
    EXPECT_EQ(bare.find("io_class"), std::string::npos)
        << "a pad that declared no io_class was written with one:\n" << bare;
    EXPECT_EQ(bare.find("layer"), std::string::npos)
        << "a pad that declared no layer was written with one:\n" << bare;

    const std::string classOnly = padBlock("CLASS_ONLY");
    EXPECT_NE(classOnly.find("io_class: \"flipped_bump\""), std::string::npos) << classOnly;
    EXPECT_EQ(classOnly.find("layer"), std::string::npos)
        << "a layer appeared beside a declared io_class:\n" << classOnly;

    const std::string layerOnly = padBlock("LAYER_ONLY");
    EXPECT_NE(layerOnly.find("layer: \"TopMetal1\""), std::string::npos) << layerOnly;
    EXPECT_EQ(layerOnly.find("io_class"), std::string::npos)
        << "an io_class appeared beside a declared layer:\n" << layerOnly;

    // And the document still reloads, so "emit less" did not drop a field the
    // reader requires.
    ChipletFormat reader;
    auto reloaded = reader.load(out);
    ASSERT_TRUE(reloaded);

    std::filesystem::remove(src);
    std::filesystem::remove(out);
}

}  // namespace
}  // namespace chiplet
