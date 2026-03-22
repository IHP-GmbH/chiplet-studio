/**
 * test_interfaces.cpp - Unit tests for Interface parsing in ChipletFormat
 */

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "formats/ChipletFormat.h"
#include "core/Interface.h"

namespace chiplet {
namespace {

std::string fixturePath(const std::string& filename)
{
    return std::string(FIXTURES_DIR) + "/" + filename;
}

// Load the fixture with two interfaces and verify count, ids, and types
TEST(InterfaceParsing, ParseInterfaces)
{
    ChipletFormat format;
    auto assembly = format.load(fixturePath("with_interfaces.chiplet"));

    ASSERT_NE(assembly, nullptr);
    EXPECT_EQ(assembly->interfaces().size(), 2u);

    auto iface0 = assembly->interface("die0_to_interposer");
    ASSERT_NE(iface0, nullptr);
    EXPECT_EQ(iface0->id(), "die0_to_interposer");
    EXPECT_EQ(iface0->type(), InterfaceType::MicroBump);

    auto iface1 = assembly->interface("interposer_to_substrate");
    ASSERT_NE(iface1, nullptr);
    EXPECT_EQ(iface1->id(), "interposer_to_substrate");
    EXPECT_EQ(iface1->type(), InterfaceType::CopperPillar);
}

// Verify endpoint details (from/to component, surface, portLayer)
TEST(InterfaceParsing, InterfaceEndpointDetails)
{
    ChipletFormat format;
    auto assembly = format.load(fixturePath("with_interfaces.chiplet"));

    auto iface = assembly->interface("die0_to_interposer");
    ASSERT_NE(iface, nullptr);

    EXPECT_EQ(iface->from().component, "die_0");
    EXPECT_EQ(iface->from().surface, "bottom");
    EXPECT_EQ(iface->from().portLayer, "TopMetal2");

    EXPECT_EQ(iface->to().component, "interposer");
    EXPECT_EQ(iface->to().surface, "top");
    EXPECT_EQ(iface->to().portLayer, "TopMetal2");

    auto iface2 = assembly->interface("interposer_to_substrate");
    ASSERT_NE(iface2, nullptr);

    EXPECT_EQ(iface2->from().component, "interposer");
    EXPECT_EQ(iface2->from().surface, "bottom");
    EXPECT_EQ(iface2->from().portLayer, "Metal1");

    EXPECT_EQ(iface2->to().component, "substrate");
    EXPECT_EQ(iface2->to().surface, "top");
    EXPECT_EQ(iface2->to().portLayer, "Metal1");
}

// Verify physical parameters
TEST(InterfaceParsing, PhysicalParameters)
{
    ChipletFormat format;
    auto assembly = format.load(fixturePath("with_interfaces.chiplet"));

    auto iface = assembly->interface("die0_to_interposer");
    ASSERT_NE(iface, nullptr);
    EXPECT_DOUBLE_EQ(iface->physical().pitch, 55.0);
    EXPECT_DOUBLE_EQ(iface->physical().diameter, 25.0);
    EXPECT_DOUBLE_EQ(iface->physical().height, 30.0);

    auto iface2 = assembly->interface("interposer_to_substrate");
    ASSERT_NE(iface2, nullptr);
    EXPECT_DOUBLE_EQ(iface2->physical().pitch, 130.0);
    EXPECT_DOUBLE_EQ(iface2->physical().diameter, 50.0);
    EXPECT_DOUBLE_EQ(iface2->physical().height, 45.0);
}

// Round-trip: load, save, reload, compare all fields
TEST(InterfaceParsing, RoundTripInterfaces)
{
    ChipletFormat format;
    auto assembly = format.load(fixturePath("with_interfaces.chiplet"));
    ASSERT_NE(assembly, nullptr);

    std::string tempPath = "test_interfaces_roundtrip.chiplet";
    EXPECT_NO_THROW(format.save(*assembly, tempPath));

    ChipletFormat format2;
    auto assembly2 = format2.load(tempPath);
    ASSERT_NE(assembly2, nullptr);

    EXPECT_EQ(assembly2->interfaces().size(), 2u);

    auto iface = assembly2->interface("die0_to_interposer");
    ASSERT_NE(iface, nullptr);
    EXPECT_EQ(iface->type(), InterfaceType::MicroBump);
    EXPECT_EQ(iface->from().component, "die_0");
    EXPECT_EQ(iface->from().surface, "bottom");
    EXPECT_EQ(iface->from().portLayer, "TopMetal2");
    EXPECT_EQ(iface->to().component, "interposer");
    EXPECT_EQ(iface->to().surface, "top");
    EXPECT_EQ(iface->to().portLayer, "TopMetal2");
    EXPECT_DOUBLE_EQ(iface->physical().pitch, 55.0);
    EXPECT_DOUBLE_EQ(iface->physical().diameter, 25.0);
    EXPECT_DOUBLE_EQ(iface->physical().height, 30.0);

    auto iface2 = assembly2->interface("interposer_to_substrate");
    ASSERT_NE(iface2, nullptr);
    EXPECT_EQ(iface2->type(), InterfaceType::CopperPillar);
    EXPECT_DOUBLE_EQ(iface2->physical().pitch, 130.0);

    std::filesystem::remove(tempPath);
}

// Unknown interface type should throw ChipletFormatException
TEST(InterfaceParsing, InvalidInterfaceType)
{
    std::string tempPath = "test_invalid_iface_type.chiplet";
    {
        std::ofstream f(tempPath);
        f << "format_version: \"1.0\"\n"
          << "assembly:\n"
          << "  name: \"Bad Interface Type\"\n"
          << "  units: \"um\"\n"
          << "interfaces:\n"
          << "  - id: bad_iface\n"
          << "    type: quantum_tunnel\n";
    }

    ChipletFormat format;
    EXPECT_THROW(format.load(tempPath), ChipletFormatException);

    std::filesystem::remove(tempPath);
}

// Missing interface id should throw ChipletFormatException
TEST(InterfaceParsing, MissingInterfaceId)
{
    std::string tempPath = "test_missing_iface_id.chiplet";
    {
        std::ofstream f(tempPath);
        f << "format_version: \"1.0\"\n"
          << "assembly:\n"
          << "  name: \"Missing Interface Id\"\n"
          << "  units: \"um\"\n"
          << "interfaces:\n"
          << "  - type: micro_bump\n";
    }

    ChipletFormat format;
    EXPECT_THROW(format.load(tempPath), ChipletFormatException);

    std::filesystem::remove(tempPath);
}

// Files without interfaces section should still load (backward compat)
TEST(InterfaceParsing, BackwardCompatNoInterfaces)
{
    ChipletFormat format;
    auto assembly = format.load(fixturePath("minimal.chiplet"));

    ASSERT_NE(assembly, nullptr);
    EXPECT_TRUE(assembly->interfaces().empty());
}

// Type conversion helper functions
TEST(InterfaceParsing, TypeConversion)
{
    EXPECT_EQ(interface_type_from_string("micro_bump"), InterfaceType::MicroBump);
    EXPECT_EQ(interface_type_from_string("copper_pillar"), InterfaceType::CopperPillar);
    EXPECT_EQ(interface_type_from_string("tsv"), InterfaceType::TSV);
    EXPECT_EQ(interface_type_from_string("wire_bond"), InterfaceType::WireBond);

    EXPECT_EQ(interface_type_to_string(InterfaceType::MicroBump), "micro_bump");
    EXPECT_EQ(interface_type_to_string(InterfaceType::CopperPillar), "copper_pillar");
    EXPECT_EQ(interface_type_to_string(InterfaceType::TSV), "tsv");
    EXPECT_EQ(interface_type_to_string(InterfaceType::WireBond), "wire_bond");

    EXPECT_THROW(interface_type_from_string("invalid"), ChipletFormatException);
}

} // namespace
} // namespace chiplet
