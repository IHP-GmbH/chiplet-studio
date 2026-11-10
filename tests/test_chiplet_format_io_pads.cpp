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

#include <QByteArray>
#include <QCryptographicHash>

namespace chiplet {
namespace {

// sha256 of tests/fixtures/pipeline_smoke.chiplet, as its PROVENANCE.md names it.
const char kPipelineSmokeSha[] =
    "26fbd6060f44fe99d8ce8c5170e6c1315517582fde127050a86eb19a44b82a7a";

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
    // Smoke: parse a .chiplet produced by the plugin's real export, ending in
    // hyp_to_gds.py --update-chiplet-file (see pipeline_smoke.PROVENANCE.md).
    // Asserts schema compatibility end-to-end (hyp_to_gds -> ChipletFormat::load).
    // The hash comes first: an expectation "fixed" by editing the copy is the
    // failure this guards against.
    const std::string path = fixturePath("pipeline_smoke.chiplet");
    {
        std::ifstream in(path, std::ios::binary);
        ASSERT_TRUE(in) << "fixture missing: " << path;
        std::ostringstream text;
        text << in.rdbuf();
        const std::string bytes = text.str();
        const QByteArray digest = QCryptographicHash::hash(
            QByteArray(bytes.data(), static_cast<int>(bytes.size())),
            QCryptographicHash::Sha256).toHex();
        ASSERT_EQ(digest.toStdString(), kPipelineSmokeSha)
            << "tests/fixtures/pipeline_smoke.chiplet is not the export its "
               "PROVENANCE.md names. Re-export it; do not edit it here.";
    }

    ChipletFormat format;
    auto assembly = format.load(path);
    ASSERT_NE(assembly, nullptr);

    auto* interposer = assembly->component(ComponentID("interposer"));
    ASSERT_NE(interposer, nullptr);
    // The writer declares the plane, and the dies sit on it as written.
    ASSERT_TRUE(interposer->attachment_surface_z());
    EXPECT_DOUBLE_EQ(*interposer->attachment_surface_z(), 10.8103);
    const Component* u1 = assembly->component(ComponentID("U1"));
    const Component* u2 = assembly->component(ComponentID("U2"));
    ASSERT_NE(u1, nullptr);
    ASSERT_NE(u2, nullptr);
    EXPECT_DOUBLE_EQ(u1->position().z, 54.8103);
    EXPECT_DOUBLE_EQ(u2->position().z, 34.8103);

    const auto& pads = interposer->io_pads();
    ASSERT_EQ(pads.size(), 35u);

    // Three pads pinned by position, as the export wrote them.
    std::map<std::string, std::pair<double, double>> pinned = {
        {"J24", {2113.915000000008, 672.7449999999953}},
        {"J25", {500.5350000000035, 2252.220000000001}},
        {"J10", {3053.545000000013, 3420.7550000000047}},
    };
    std::set<std::string> ids;
    std::map<std::string, int> nets;
    for (const auto& p : pads) {
        EXPECT_EQ(p.io_class(), IOClass::WireBond) << p.id();
        EXPECT_EQ(p.layer(), "TopMetal2") << p.id();
        EXPECT_DOUBLE_EQ(p.size().x, 100.0) << p.id();
        EXPECT_DOUBLE_EQ(p.size().y, 100.0) << p.id();
        ids.insert(p.id());
        ++nets[p.net()];
        if (auto it = pinned.find(p.id()); it != pinned.end()) {
            EXPECT_DOUBLE_EQ(p.position().x, it->second.first) << p.id();
            EXPECT_DOUBLE_EQ(p.position().y, it->second.second) << p.id();
            pinned.erase(it);
        }
    }
    EXPECT_TRUE(pinned.empty()) << "a pinned pad is missing";

    // J2..J43 without J4..J8, J11 and J38.
    std::set<std::string> expectedIds;
    for (int n = 2; n <= 43; ++n) {
        if ((n >= 4 && n <= 8) || n == 11 || n == 38) continue;
        expectedIds.insert("J" + std::to_string(n));
    }
    EXPECT_EQ(ids, expectedIds);

    const std::map<std::string, int> expectedNets = {
        {"gnd", 14}, {"vcc", 2},
        {"div_out", 1}, {"div_vcc", 1}, {"fd_vbb", 1}, {"fd_vcc", 1},
        {"fq_vcc", 1}, {"pa_vbb1", 1}, {"pa_vbb2", 1}, {"pa_vbb3", 1},
        {"pa_vbb4", 1}, {"pa_vbb5", 1}, {"pa_vcc1", 1}, {"pa_vcc2", 1},
        {"pa_vcc3", 1}, {"pa_vcc4", 1}, {"pa_vcc5", 1}, {"ref", 1},
        {"sout", 1}, {"vco_vcm", 1}, {"vco_vth", 1},
    };
    EXPECT_EQ(nets, expectedNets);
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
