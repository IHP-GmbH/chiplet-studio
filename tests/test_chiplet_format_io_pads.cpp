/**
 * test_chiplet_format_io_pads.cpp - Unit tests for I/O pad schema parsing
 */

#include <gtest/gtest.h>
#include <filesystem>
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

}  // namespace
}  // namespace chiplet
