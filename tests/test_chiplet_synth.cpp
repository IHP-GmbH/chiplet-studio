/**
 * test_chiplet_synth.cpp - Unit tests for the single-GDS import synthesizer
 * and the optional technology `stackup` field it relies on.
 *
 * Hermetic: synthesizes YAML, writes it to a temp file, and loads it through
 * the real ChipletFormat. No KLayout / GDS reading is involved.
 */

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <string>

#include "formats/ChipletSynth.h"
#include "formats/ChipletFormat.h"
#include "core/Assembly.h"
#include "core/Component.h"
#include "core/Technology.h"

namespace chiplet {
namespace {

std::string writeTemp(const std::string& name, const std::string& content)
{
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / name;
    std::ofstream f(path);
    f << content;
    f.close();
    return path.string();
}

// A supported-PDK import: technology id alone, no custom files. The synthesized
// YAML must load and carry exactly the single die we described.
TEST(ChipletSynth, SupportedPdkLoads)
{
    SingleGdsImportSpec spec;
    spec.gdsPath = "/tmp/design.gds";  // need not exist; read only at render
    spec.topCell = "MY_TOP";
    spec.widthUm = 1234.5;
    spec.heightUm = 678.25;
    spec.thicknessUm = 200.0;
    spec.techId = "sg13g2";

    const std::string yaml = synthesizeSingleGdsChiplet(spec);
    const std::string path = writeTemp("chiplet_synth_supported.chiplet", yaml);

    ChipletFormat format;
    auto assembly = format.load(path);
    ASSERT_NE(assembly, nullptr);

    ASSERT_EQ(assembly->components().size(), 1u);
    auto* die = assembly->component("imported_die");
    ASSERT_NE(die, nullptr);
    EXPECT_EQ(die->type(), ComponentType::Die);
    EXPECT_EQ(die->technology(), "sg13g2");
    EXPECT_EQ(die->top_cell(), "MY_TOP");
    EXPECT_EQ(die->layout_path(), "/tmp/design.gds");
    EXPECT_DOUBLE_EQ(die->dimensions().width, 1234.5);
    EXPECT_DOUBLE_EQ(die->dimensions().height, 678.25);
    EXPECT_DOUBLE_EQ(die->dimensions().thickness, 200.0);

    auto* tech = assembly->technology("sg13g2");
    ASSERT_NE(tech, nullptr);
    EXPECT_TRUE(tech->layer_properties_source().empty());
    EXPECT_TRUE(tech->stackup_source().empty());

    std::filesystem::remove(path);
}

// A custom / unsupported PDK: user-supplied .lyp + stackup must parse into the
// technology and survive a save -> reload round-trip (proves Edits 1-7).
TEST(ChipletSynth, CustomLypAndStackupRoundTrip)
{
    SingleGdsImportSpec spec;
    spec.gdsPath = "/tmp/closed.gds";
    spec.topCell = "TOP";
    spec.widthUm = 500.0;
    spec.heightUm = 500.0;
    spec.techId = "";  // custom
    spec.customLyp = "/tmp/custom.lyp";
    spec.customStackup = "/tmp/custom.stackup.yaml";

    const std::string yaml = synthesizeSingleGdsChiplet(spec);
    const std::string path = writeTemp("chiplet_synth_custom.chiplet", yaml);

    ChipletFormat format;
    auto assembly = format.load(path);
    ASSERT_NE(assembly, nullptr);

    auto* tech = assembly->technology(kCustomImportTechId);
    ASSERT_NE(tech, nullptr);
    EXPECT_EQ(tech->layer_properties_source(), "/tmp/custom.lyp");
    EXPECT_EQ(tech->stackup_source(), "/tmp/custom.stackup.yaml");
    // Absolute inputs resolve to themselves.
    EXPECT_EQ(tech->stackup_path(), "/tmp/custom.stackup.yaml");

    // Round-trip: save and reload; the stackup field must survive.
    const std::filesystem::path outPath =
        std::filesystem::temp_directory_path() / "chiplet_synth_custom_rt.chiplet";
    format.save(*assembly, outPath.string());

    ChipletFormat format2;
    auto reloaded = format2.load(outPath.string());
    ASSERT_NE(reloaded, nullptr);
    auto* tech2 = reloaded->technology(kCustomImportTechId);
    ASSERT_NE(tech2, nullptr);
    EXPECT_EQ(tech2->stackup_source(), "/tmp/custom.stackup.yaml");
    EXPECT_EQ(tech2->layer_properties_source(), "/tmp/custom.lyp");

    std::filesystem::remove(path);
    std::filesystem::remove(outPath);
}

// Optional fields are emit-when-present: a supported-PDK import declares no
// stackup or layer_properties (this is what keeps the format backward compatible
// and existing golden files byte-identical).
TEST(ChipletSynth, OptionalFieldsOmittedWhenEmpty)
{
    SingleGdsImportSpec spec;
    spec.gdsPath = "/tmp/a.gds";
    spec.topCell = "A";
    spec.techId = "sky130";

    const std::string yaml = synthesizeSingleGdsChiplet(spec);
    EXPECT_EQ(yaml.find("stackup"), std::string::npos);
    EXPECT_EQ(yaml.find("layer_properties"), std::string::npos);
}

// And present when supplied.
TEST(ChipletSynth, CustomStackupEmitted)
{
    SingleGdsImportSpec spec;
    spec.gdsPath = "/tmp/a.gds";
    spec.techId = "";
    spec.customStackup = "/tmp/s.yaml";

    const std::string yaml = synthesizeSingleGdsChiplet(spec);
    EXPECT_NE(yaml.find("stackup:"), std::string::npos);
}

// A non-positive bbox must not yield a zero-sized die (the mesh builder would
// fall back to symbolic geometry); the synthesizer clamps to sane defaults.
TEST(ChipletSynth, DefaultsForInvalidBbox)
{
    SingleGdsImportSpec spec;
    spec.gdsPath = "/tmp/a.gds";
    spec.topCell = "A";
    spec.widthUm = 0.0;
    spec.heightUm = 0.0;
    spec.thicknessUm = 0.0;
    spec.techId = "gf180";

    const std::string yaml = synthesizeSingleGdsChiplet(spec);
    const std::string path = writeTemp("chiplet_synth_defaults.chiplet", yaml);

    ChipletFormat format;
    auto assembly = format.load(path);
    ASSERT_NE(assembly, nullptr);
    auto* die = assembly->component("imported_die");
    ASSERT_NE(die, nullptr);
    EXPECT_GT(die->dimensions().width, 0.0);
    EXPECT_GT(die->dimensions().height, 0.0);
    EXPECT_GT(die->dimensions().thickness, 0.0);

    std::filesystem::remove(path);
}

}  // namespace
}  // namespace chiplet
