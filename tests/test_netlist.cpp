/**
 * test_netlist.cpp - Unit tests for Netlist integration in Assembly and ChipletFormat
 */

#include <gtest/gtest.h>
#include <filesystem>
#include "formats/ChipletFormat.h"
#include "core/Netlist.h"

namespace chiplet {
namespace {

std::string fixturePath(const std::string& filename)
{
    return std::string(FIXTURES_DIR) + "/" + filename;
}

// --- Standalone Netlist data structure tests ---

TEST(NetlistTests, NetConstruction)
{
    Net net("VDD", NetClass::Power);
    EXPECT_EQ(net.name(), "VDD");
    EXPECT_EQ(net.net_class(), NetClass::Power);
    EXPECT_EQ(net.connection_count(), 0u);

    net.add_connection("die_0", "vdd", "TopMetal2");
    EXPECT_EQ(net.connection_count(), 1u);
    EXPECT_EQ(net.connections()[0].component, "die_0");
    EXPECT_EQ(net.connections()[0].pin, "vdd");
    EXPECT_EQ(net.connections()[0].layer, "TopMetal2");
}

TEST(NetlistTests, NetlistAddQuery)
{
    Netlist netlist;
    EXPECT_TRUE(netlist.empty());
    EXPECT_EQ(netlist.net_count(), 0u);

    Net vdd("VDD", NetClass::Power);
    vdd.add_connection("interposer", "vdd_pad");
    vdd.add_connection("die_0", "vdd");
    netlist.add_net(std::move(vdd));

    Net gnd("GND", NetClass::Ground);
    gnd.add_connection("interposer", "gnd_pad");
    gnd.add_connection("die_0", "vss");
    netlist.add_net(std::move(gnd));

    Net sig("SIG0", NetClass::Signal);
    sig.add_connection("die_0", "data_out");
    netlist.add_net(std::move(sig));

    EXPECT_FALSE(netlist.empty());
    EXPECT_EQ(netlist.net_count(), 3u);

    // Query by name
    const Net* found = netlist.net("VDD");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->net_class(), NetClass::Power);

    EXPECT_EQ(netlist.net("NONEXISTENT"), nullptr);

    // Query by class
    auto power_nets = netlist.nets_by_class(NetClass::Power);
    EXPECT_EQ(power_nets.size(), 1u);
    EXPECT_EQ(power_nets[0]->name(), "VDD");

    auto ground_nets = netlist.nets_by_class(NetClass::Ground);
    EXPECT_EQ(ground_nets.size(), 1u);

    // Query by component
    auto die_nets = netlist.nets_for_component("die_0");
    EXPECT_EQ(die_nets.size(), 3u);

    auto interposer_nets = netlist.nets_for_component("interposer");
    EXPECT_EQ(interposer_nets.size(), 2u);
}

TEST(NetlistTests, NetClassConversion)
{
    EXPECT_EQ(net_class_to_string(NetClass::Signal), "signal");
    EXPECT_EQ(net_class_to_string(NetClass::Power), "power");
    EXPECT_EQ(net_class_to_string(NetClass::Ground), "ground");
    EXPECT_EQ(net_class_to_string(NetClass::DiffPair), "diff_pair");
    EXPECT_EQ(net_class_to_string(NetClass::NC), "nc");
    EXPECT_EQ(net_class_to_string(NetClass::Interface), "interface");

    EXPECT_EQ(string_to_net_class("signal"), NetClass::Signal);
    EXPECT_EQ(string_to_net_class("power"), NetClass::Power);
    EXPECT_EQ(string_to_net_class("ground"), NetClass::Ground);
    EXPECT_EQ(string_to_net_class("diff_pair"), NetClass::DiffPair);
    EXPECT_EQ(string_to_net_class("nc"), NetClass::NC);
    EXPECT_EQ(string_to_net_class("interface"), NetClass::Interface);

    // Unknown defaults to Signal
    EXPECT_EQ(string_to_net_class("bogus"), NetClass::Signal);
}

// --- ChipletFormat integration tests ---

TEST(NetlistTests, ParseNetlist)
{
    ChipletFormat format;
    auto assembly = format.load(fixturePath("with_netlist.chiplet"));

    ASSERT_NE(assembly, nullptr);
    const Netlist& nl = assembly->netlist();
    EXPECT_FALSE(nl.empty());
    EXPECT_EQ(nl.net_count(), 3u);

    // VDD
    const Net* vdd = nl.net("VDD");
    ASSERT_NE(vdd, nullptr);
    EXPECT_EQ(vdd->net_class(), NetClass::Power);
    EXPECT_EQ(vdd->connection_count(), 2u);
    EXPECT_EQ(vdd->connections()[0].component, "interposer");
    EXPECT_EQ(vdd->connections()[0].pin, "vdd_pad");
    EXPECT_EQ(vdd->connections()[0].layer, "TopMetal2");
    EXPECT_EQ(vdd->connections()[1].component, "die_0");
    EXPECT_EQ(vdd->connections()[1].pin, "vdd");

    // GND
    const Net* gnd = nl.net("GND");
    ASSERT_NE(gnd, nullptr);
    EXPECT_EQ(gnd->net_class(), NetClass::Ground);
    EXPECT_EQ(gnd->connection_count(), 2u);

    // SIG0
    const Net* sig = nl.net("SIG0");
    ASSERT_NE(sig, nullptr);
    EXPECT_EQ(sig->net_class(), NetClass::Signal);
    EXPECT_EQ(sig->connection_count(), 2u);
    EXPECT_EQ(sig->connections()[0].pin, "sig0_pad");
    EXPECT_EQ(sig->connections()[1].pin, "data_out");
}

TEST(NetlistTests, RoundTripNetlist)
{
    ChipletFormat format;
    auto assembly = format.load(fixturePath("with_netlist.chiplet"));
    ASSERT_NE(assembly, nullptr);

    std::string tempPath = "test_netlist_roundtrip.chiplet";
    EXPECT_NO_THROW(format.save(*assembly, tempPath));

    ChipletFormat format2;
    auto assembly2 = format2.load(tempPath);
    ASSERT_NE(assembly2, nullptr);

    const Netlist& nl = assembly2->netlist();
    EXPECT_EQ(nl.net_count(), 3u);

    // Verify all nets survived round-trip
    const Net* vdd = nl.net("VDD");
    ASSERT_NE(vdd, nullptr);
    EXPECT_EQ(vdd->net_class(), NetClass::Power);
    EXPECT_EQ(vdd->connection_count(), 2u);
    EXPECT_EQ(vdd->connections()[0].component, "interposer");
    EXPECT_EQ(vdd->connections()[0].pin, "vdd_pad");
    EXPECT_EQ(vdd->connections()[0].layer, "TopMetal2");

    const Net* gnd = nl.net("GND");
    ASSERT_NE(gnd, nullptr);
    EXPECT_EQ(gnd->net_class(), NetClass::Ground);

    const Net* sig = nl.net("SIG0");
    ASSERT_NE(sig, nullptr);
    EXPECT_EQ(sig->net_class(), NetClass::Signal);

    EXPECT_EQ(nl.external_netlist_path(), "test_netlist.csv");

    std::filesystem::remove(tempPath);
}

TEST(NetlistTests, ParseExternalNetlistPath)
{
    ChipletFormat format;
    auto assembly = format.load(fixturePath("with_netlist.chiplet"));
    ASSERT_NE(assembly, nullptr);

    EXPECT_EQ(assembly->netlist().external_netlist_path(), "test_netlist.csv");
}

TEST(NetlistTests, BackwardCompatNoNetlist)
{
    ChipletFormat format;
    auto assembly = format.load(fixturePath("minimal.chiplet"));

    ASSERT_NE(assembly, nullptr);
    EXPECT_TRUE(assembly->netlist().empty());
    EXPECT_EQ(assembly->netlist().net_count(), 0u);
}
// NOTE: the former RealWorldChipletDemo integration test was removed here.
// It loaded chiplet_demo.chiplet from the kicad_interposer_hyperlynx_to_gds
// repo (a sibling of the old kicad_designs/ layout); that repo and its B0
// output were retired, so the fixture is permanently gone and the test only
// ever GTEST_SKIP'd. Its assertions (5 nets, VDD/GND classes, the
// chiplet_demo_netlist.csv ref) are specific to that removed file and cannot
// be met by any bundled demo. The synthetic NetlistTests above cover the
// parser; a new real-file check should resolve via $WIREBOND_DEMO_CHIPLET.

} // namespace
} // namespace chiplet
