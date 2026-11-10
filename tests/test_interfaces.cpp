/**
 * test_interfaces.cpp - Unit tests for Interface parsing in ChipletFormat
 */

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "formats/ChipletFormat.h"
#include "core/Interface.h"
#include <chiplet_format_io/chiplet_format_io.hpp>
#include <algorithm>
#include <string>
#include <vector>

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
    EXPECT_EQ(iface0->type().mapped(), InterfaceType::MicroBump);

    auto iface1 = assembly->interface("interposer_to_substrate");
    ASSERT_NE(iface1, nullptr);
    EXPECT_EQ(iface1->id(), "interposer_to_substrate");
    EXPECT_EQ(iface1->type().mapped(), InterfaceType::CopperPillar);
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
    EXPECT_EQ(iface->type().mapped(), InterfaceType::MicroBump);
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
    EXPECT_EQ(iface2->type().mapped(), InterfaceType::CopperPillar);
    EXPECT_DOUBLE_EQ(iface2->physical().pitch, 130.0);

    std::filesystem::remove(tempPath);
}

// An interface type this studio does not map must LOAD, be reported, and
// survive a save byte for byte.
//
// This replaces a test that asserted the opposite. That one expected a throw,
// and it was green for a reason it did not state: the vendored reference reader
// refused an unknown type before this reader's own converter ever saw the
// value, so the assertion could not tell the two throwers apart and would have
// stayed green if either were removed alone. Both are gone now, upstream by
// policy and here by this commit.
//
// quantum_tunnel is the right fixture for THIS proposition. It is outside the
// format's closed vocabulary, so the schema refuses it for a producer, and the
// reference reader carries it and warns anyway; chiplet-system-spec pins that
// divergence deliberately. So it exercises the case that recurs: a document
// whose type neither the studio nor the vocabulary knows.
TEST(InterfaceParsing, UnmappedInterfaceTypeLoadsAndSurvivesASave)
{
    const std::string tempPath = "test_unmapped_iface_type.chiplet";
    {
        std::ofstream f(tempPath);
        f << "format_version: \"1.0\"\n"
          << "assembly:\n"
          << "  name: \"Unmapped Interface Type\"\n"
          << "  units: \"um\"\n"
          << "interfaces:\n"
          << "  - id: bad_iface\n"
          << "    type: quantum_tunnel\n";
    }

    ChipletFormat format;
    std::unique_ptr<Assembly> assembly;
    ASSERT_NO_THROW(assembly = format.load(tempPath))
        << "a type this studio cannot act on is a fact about this studio, not a "
           "defect in the document";
    ASSERT_TRUE(assembly);

    auto* iface = assembly->interface("bad_iface");
    ASSERT_NE(iface, nullptr) << "the ELEMENT must survive, not just the document";
    EXPECT_FALSE(iface->type().mapped().has_value());
    EXPECT_EQ(iface->type().declared(), "quantum_tunnel");

    // The reader's note reached this host. Content, not merely "a warning
    // happened": a count with no text is satisfied by warning about anything.
    const auto& notes = format.warnings();
    ASSERT_FALSE(notes.empty()) << "the unmapped value was carried silently";
    EXPECT_NE(std::find_if(notes.begin(), notes.end(),
                           [](const std::string& n) {
                               return n.find("quantum_tunnel") != std::string::npos;
                           }),
              notes.end())
        << "a note was produced but it does not name the value";

    // The bytes, not a reload. A reload comparing equal is satisfied by a lossy
    // pair of converters that agree with each other.
    const std::string outPath = "test_unmapped_iface_type_out.chiplet";
    ASSERT_NO_THROW(format.save(*assembly, outPath));
    std::string written;
    {
        std::ifstream in(outPath, std::ios::binary);
        written.assign(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
    }
    EXPECT_NE(written.find("quantum_tunnel"), std::string::npos)
        << "the save rewrote a type it did not recognise, which is data loss "
           "with no appearance of loss:\n" << written;

    std::filesystem::remove(tempPath);
    std::filesystem::remove(outPath);
}

// The other half, and a different proposition: a type the FORMAT knows and this
// studio now maps. solder_bump is schema-valid, so the reference reader emits
// no note for it; if the studio left it unmapped it would be more ignorant than
// the reader it vendors and would have to invent an alarm about a perfectly
// good document.
TEST(InterfaceParsing, SolderBumpIsMappedAndWarnsAboutNothing)
{
    const std::string tempPath = "test_solder_bump.chiplet";
    {
        std::ofstream f(tempPath);
        f << "format_version: \"1.0\"\n"
          << "assembly:\n"
          << "  name: \"Solder Bump\"\n"
          << "  units: \"um\"\n"
          << "interfaces:\n"
          << "  - id: sb\n"
          << "    type: solder_bump\n";
    }

    ChipletFormat format;
    std::unique_ptr<Assembly> assembly;
    ASSERT_NO_THROW(assembly = format.load(tempPath));
    ASSERT_TRUE(assembly);

    auto* iface = assembly->interface("sb");
    ASSERT_NE(iface, nullptr);
    EXPECT_EQ(iface->type().mapped(), InterfaceType::SolderBump);
    EXPECT_EQ(iface->type().declared(), "solder_bump");

    // The negative that stops "warn unconditionally" from passing this suite.
    for (const std::string& note : format.warnings()) {
        EXPECT_EQ(note.find("solder_bump"), std::string::npos)
            << "warned about a value the format and this studio both know: " << note;
    }

    std::filesystem::remove(tempPath);
}

// The studio's mapping must cover exactly the vocabulary the vendored reader
// exports, in both directions.
//
// Without this the two lists drift in silence: the studio under-maps and every
// document using the new member is carried as unmapped, or it over-maps and
// writes a type no reader accepts. Neither shows up in any other test, because
// each list on its own is still internally consistent.
//
// Polarity matters here. The check is against kKnownInterfaceTypes, so a member
// ADDED upstream turns this red on the next re-vendor, which is the moment
// somebody should decide what the studio does about it. A hand-written list of
// expected names would instead stay green through exactly that event.
TEST(InterfaceParsing, MappingCoversTheVendoredVocabularyExactly)
{
    namespace cfio = chiplet_format_io;

    for (const char* known : cfio::kKnownInterfaceTypes) {
        EXPECT_TRUE(InterfaceTypeValue(std::string(known)).mapped().has_value())
            << "the reference reader accepts '" << known << "' and this studio "
               "does not map it; a document using it loads but nothing here can "
               "act on it";
    }

    const InterfaceType all[] = {
        InterfaceType::MicroBump, InterfaceType::CopperPillar,
        InterfaceType::TSV, InterfaceType::WireBond, InterfaceType::SolderBump,
    };
    for (InterfaceType t : all) {
        const std::string name = interface_type_name(t);
        EXPECT_TRUE(cfio::is_known_interface_type(name))
            << "this studio maps to '" << name << "', which is outside the "
               "format's vocabulary; a save would write a type no reader takes";
        // The round trip through the table, which is what makes declared() an
        // honest record for a programmatically built interface.
        EXPECT_EQ(InterfaceTypeValue(t).mapped(), t);
        EXPECT_EQ(InterfaceTypeValue(t).declared(), name);
    }
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

// The mapping, read in both directions, including the case the old version of
// this test asserted the wrong way round: an unrecognised value is not an
// error, it is the absence of a mapping.
TEST(InterfaceParsing, TypeConversion)
{
    EXPECT_EQ(InterfaceTypeValue("micro_bump").mapped(), InterfaceType::MicroBump);
    EXPECT_EQ(InterfaceTypeValue("copper_pillar").mapped(), InterfaceType::CopperPillar);
    EXPECT_EQ(InterfaceTypeValue("tsv").mapped(), InterfaceType::TSV);
    EXPECT_EQ(InterfaceTypeValue("wire_bond").mapped(), InterfaceType::WireBond);
    EXPECT_EQ(InterfaceTypeValue("solder_bump").mapped(), InterfaceType::SolderBump);

    EXPECT_STREQ(interface_type_name(InterfaceType::MicroBump), "micro_bump");
    EXPECT_STREQ(interface_type_name(InterfaceType::CopperPillar), "copper_pillar");
    EXPECT_STREQ(interface_type_name(InterfaceType::TSV), "tsv");
    EXPECT_STREQ(interface_type_name(InterfaceType::WireBond), "wire_bond");
    EXPECT_STREQ(interface_type_name(InterfaceType::SolderBump), "solder_bump");

    EXPECT_FALSE(InterfaceTypeValue("invalid").mapped().has_value());
    EXPECT_EQ(InterfaceTypeValue("invalid").declared(), "invalid");
}

} // namespace
} // namespace chiplet
