/**
 * test_chiplet_synth.cpp - Unit tests for the single-GDS import path and the
 * optional technology `stackup` field it relies on.
 *
 * Hermetic: builds the assembly the importer builds, writes it with the real
 * ChipletFormat, and loads it back. No KLayout / GDS reading is involved.
 *
 * The tests go through save() rather than around it on purpose. The import path
 * used to build YAML as a string with its own quoter; now there is exactly one
 * serializer, and a test that asserted on a string this file produced would be
 * asserting on the thing that was deleted.
 */

#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <QComboBox>
#include <QLabel>

#include "formats/ChipletSynth.h"
#include "formats/ChipletFormat.h"
#include "core/Assembly.h"
#include "core/Component.h"
#include "core/Technology.h"
#include "ui/ImportGdsDialog.h"

namespace chiplet {
namespace {

// One directory per test. These tests WRITE, and a leftover file from a previous
// run changes what a write means.
std::filesystem::path caseDir(const std::string& name)
{
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / ("chiplet_synth_" + name);
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

std::string readWhole(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

// Build the assembly the importer builds and write it the way the importer
// writes it. Returns the refusal message, or "" when the save went through.
std::string saveImport(const SingleGdsImportSpec& spec,
                       const std::filesystem::path& path)
{
    // The BUILD is inside the try, not before it. buildSingleGdsAssembly
    // refuses two kinds of spec, and with the build outside, a refusal would
    // leave this helper as an uncaught exception: the hostile-input tests would
    // abort the binary instead of reporting which case refused, and the control
    // that keeps the guard from over-reaching would become a crash. Same reason
    // the entry point builds inside its own try.
    try {
        const auto assembly = buildSingleGdsAssembly(spec);
        ChipletFormat format;
        format.save(*assembly, path.string());
    } catch (const std::exception& e) {
        return e.what();
    }
    return "";
}

// A supported-PDK import: technology id alone, no custom files. The document
// must load and carry exactly the single die we described.
TEST(ChipletSynth, SupportedPdkLoads)
{
    const auto dir = caseDir("supported");
    SingleGdsImportSpec spec;
    spec.gdsPath = "/tmp/design.gds";  // need not exist; read only at render
    spec.topCell = "MY_TOP";
    spec.widthUm = 1234.5;
    spec.heightUm = 678.25;
    spec.thicknessUm = 200.0;
    spec.techId = "sg13g2";

    const auto path = dir / "import.chiplet";
    ASSERT_EQ(saveImport(spec, path), "");

    ChipletFormat format;
    auto assembly = format.load(path.string());
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

    std::filesystem::remove_all(dir);
}

// A GDS imported with the interposer technology is an Interposer, and with any
// other technology a Die, a custom PDK included. Read back from the written
// document, so the type checked is the one the file carries. The intm4tm2 row
// is the one an import that stamps Die regardless gets wrong.
TEST(ChipletSynth, TheTechnologyDecidesWhetherAnImportIsADieOrAnInterposer)
{
    const auto dir = caseDir("imported_type");
    const std::vector<std::pair<std::string, ComponentType>> rows = {
        {"intm4tm2", ComponentType::Interposer},
        {"sg13g2", ComponentType::Die},
        {"sg13cmos5l", ComponentType::Die},
        {"sky130", ComponentType::Die},
        {"gf180mcu", ComponentType::Die},
        {"", ComponentType::Die},
    };
    for (const auto& [techId, want] : rows) {
        SingleGdsImportSpec spec;
        spec.gdsPath = "/tmp/design.gds";
        spec.widthUm = 100.0;
        spec.heightUm = 50.0;
        spec.techId = techId;
        const auto path = dir / ((techId.empty() ? std::string("custom") : techId) + ".chiplet");
        ASSERT_EQ(saveImport(spec, path), "") << "technology '" << techId << "'";

        ChipletFormat format;
        auto assembly = format.load(path.string());
        ASSERT_NE(assembly, nullptr) << "technology '" << techId << "'";
        const Component* comp = assembly->component("imported_die");
        ASSERT_NE(comp, nullptr) << "technology '" << techId << "'";
        EXPECT_EQ(comp->type(), want) << "technology '" << techId << "'";
    }
    std::filesystem::remove_all(dir);
}

// The dialog's read-only "Imports as" line names the type the import writes,
// for every technology the dialog offers. The walk starts on the first entry, a
// die process, and reaches the interposer later, so a line that does not follow
// the selection reads Die where the import writes an Interposer. Both types must
// occur, or the comparison is one answer checked against itself.
TEST(ImportGdsDialog, TheImportsAsLineNamesTheTypeTheImportWrites)
{
    ImportGdsDialog dialog;
    const auto combos = dialog.findChildren<QComboBox*>();
    ASSERT_EQ(combos.size(), 1);
    QComboBox* technology = combos.front();
    const QLabel* line = dialog.findChild<QLabel*>(QStringLiteral("importsAs"));
    ASSERT_NE(line, nullptr);

    const std::map<ComponentType, QString> names = {
        {ComponentType::Die, QStringLiteral("Die")},
        {ComponentType::Interposer, QStringLiteral("Interposer")},
    };
    std::set<ComponentType> written;
    for (int i = 0; i < technology->count(); ++i) {
        technology->setCurrentIndex(i);
        SingleGdsImportSpec spec;
        spec.gdsPath = "/tmp/design.gds";
        spec.techId = dialog.techId().toStdString();
        const auto assembly = buildSingleGdsAssembly(spec);
        const ComponentType type = assembly->component("imported_die")->type();
        written.insert(type);
        ASSERT_EQ(names.count(type), 1u) << "technology '" << spec.techId << "'";
        EXPECT_EQ(line->text(), names.at(type))
            << "technology '" << spec.techId << "': the line says "
            << line->text().toStdString() << ", the import writes "
            << names.at(type).toStdString();
    }
    EXPECT_EQ(written, (std::set<ComponentType>{ComponentType::Die, ComponentType::Interposer}));
}

// The document must DECLARE the anchor, and the value is bbox_center
// even though the component type is `die`.
//
// The general convention (dies anchor on gds_origin) describes a die placed in
// an assembly, where the GDS origin is the design reference its position is
// measured from. This path is not that: it wraps one loose GDS for viewing, the
// position is the model's zero, and the dimensions ARE the GDS bounding box, so
// a bbox-sized box has to be centred on the bbox. Stamping gds_origin here would
// move every imported layout drawn in the first quadrant by half its own size,
// and it was the first fix proposed for this row.
//
// Asserting on the written TEXT as well as the parsed value is deliberate: the
// reader defaults an absent anchor to bbox_center, so a value check alone passes
// just as happily when the file says nothing at all, which is the defect this
// test exists to catch.
TEST(ChipletSynth, DeclaresBboxCenterAnchorExplicitly)
{
    const auto dir = caseDir("anchor");
    SingleGdsImportSpec spec;
    spec.gdsPath = "/tmp/design.gds";
    spec.widthUm = 100.0;
    spec.heightUm = 50.0;
    spec.thicknessUm = 10.0;

    const auto path = dir / "import.chiplet";
    ASSERT_EQ(saveImport(spec, path), "");

    const std::string text = readWhole(path);
    EXPECT_NE(text.find("anchor:"), std::string::npos)
        << "the written document does not declare an anchor; consumers would "
           "fall back to their own default. Written:\n" << text;
    EXPECT_NE(text.find("bbox_center"), std::string::npos) << text;

    ChipletFormat format;
    auto assembly = format.load(path.string());
    ASSERT_NE(assembly, nullptr);
    auto* die = assembly->component("imported_die");
    ASSERT_NE(die, nullptr);
    EXPECT_EQ(die->anchor(), Anchor::BboxCenter);
    EXPECT_TRUE(die->anchor_declared())
        << "anchor must be read from the file, not from the reader default";
    std::filesystem::remove_all(dir);
}

// A custom / unsupported PDK: user-supplied .lyp + stackup must parse into the
// technology and survive a further save -> reload round-trip.
TEST(ChipletSynth, CustomLypAndStackupRoundTrip)
{
    const auto dir = caseDir("custom");
    SingleGdsImportSpec spec;
    spec.gdsPath = "/tmp/closed.gds";
    spec.topCell = "TOP";
    spec.widthUm = 500.0;
    spec.heightUm = 500.0;
    spec.techId = "";  // custom
    spec.customLyp = "/tmp/custom.lyp";
    spec.customStackup = "/tmp/custom.stackup.yaml";

    const auto path = dir / "import.chiplet";
    ASSERT_EQ(saveImport(spec, path), "");

    ChipletFormat format;
    auto assembly = format.load(path.string());
    ASSERT_NE(assembly, nullptr);

    auto* tech = assembly->technology(kCustomImportTechId);
    ASSERT_NE(tech, nullptr);
    EXPECT_EQ(tech->layer_properties_source(), "/tmp/custom.lyp");
    EXPECT_EQ(tech->stackup_source(), "/tmp/custom.stackup.yaml");
    // Absolute inputs resolve to themselves.
    EXPECT_EQ(tech->stackup_path(), "/tmp/custom.stackup.yaml");

    const auto rtPath = dir / "roundtrip.chiplet";
    format.save(*assembly, rtPath.string());

    ChipletFormat format2;
    auto reloaded = format2.load(rtPath.string());
    ASSERT_NE(reloaded, nullptr);
    auto* tech2 = reloaded->technology(kCustomImportTechId);
    ASSERT_NE(tech2, nullptr);
    EXPECT_EQ(tech2->stackup_source(), "/tmp/custom.stackup.yaml");
    EXPECT_EQ(tech2->layer_properties_source(), "/tmp/custom.lyp");

    std::filesystem::remove_all(dir);
}

// Optional fields are emit-when-present: a supported-PDK import declares no
// stackup or layer_properties.
TEST(ChipletSynth, OptionalFieldsOmittedWhenEmpty)
{
    const auto dir = caseDir("optional");
    SingleGdsImportSpec spec;
    spec.gdsPath = "/tmp/a.gds";
    spec.topCell = "A";
    spec.techId = "sky130";

    const auto path = dir / "import.chiplet";
    ASSERT_EQ(saveImport(spec, path), "");
    const std::string text = readWhole(path);
    EXPECT_EQ(text.find("stackup"), std::string::npos) << text;
    EXPECT_EQ(text.find("layer_properties"), std::string::npos) << text;
    std::filesystem::remove_all(dir);
}

// And present when supplied.
TEST(ChipletSynth, CustomStackupEmitted)
{
    const auto dir = caseDir("stackup");
    SingleGdsImportSpec spec;
    spec.gdsPath = "/tmp/a.gds";
    spec.techId = "";
    spec.customStackup = "/tmp/s.yaml";

    const auto path = dir / "import.chiplet";
    ASSERT_EQ(saveImport(spec, path), "");
    EXPECT_NE(readWhole(path).find("stackup:"), std::string::npos);
    std::filesystem::remove_all(dir);
}

// A non-positive bbox must not yield a zero-sized die (the mesh builder would
// fall back to symbolic geometry); the builder clamps to sane defaults.
TEST(ChipletSynth, DefaultsForInvalidBbox)
{
    const auto dir = caseDir("defaults");
    SingleGdsImportSpec spec;
    spec.gdsPath = "/tmp/a.gds";
    spec.topCell = "A";
    spec.widthUm = 0.0;
    spec.heightUm = 0.0;
    spec.thicknessUm = 0.0;
    spec.techId = "gf180mcu";

    const auto path = dir / "import.chiplet";
    ASSERT_EQ(saveImport(spec, path), "");

    ChipletFormat format;
    auto assembly = format.load(path.string());
    ASSERT_NE(assembly, nullptr);
    auto* die = assembly->component("imported_die");
    ASSERT_NE(die, nullptr);
    EXPECT_GT(die->dimensions().width, 0.0);
    EXPECT_GT(die->dimensions().height, 0.0);
    EXPECT_GT(die->dimensions().thickness, 0.0);

    std::filesystem::remove_all(dir);
}

// --- What the import path may put on disk -----------------------------------
//
// The contract asserted below is deliberately a disjunction, because both arms
// are correct outcomes and only a third one is a defect:
//
//   either the document is written and the hostile bytes come back from the
//   reader exactly as they went in, with no raw occurrence of them in the file;
//   or the save refuses and NOTHING is left at the destination.
//
// The defect this replaces was neither: the old string builder escaped only a
// backslash and a double quote, so a newline in the GDS path went into the file
// raw inside a quoted scalar. PyYAML read the path back with the newline folded
// to a space, and the reference reader reported the document NOT SPLITTABLE.
// This was the only path in the program that could write such a file, and it
// wrote it beside the GDS where every *.chiplet glob finds it.
//
// What this cannot witness: the second reader. Both arms here run through the
// yaml-cpp side, so the raw-occurrence assertion is what stands in for PyYAML,
// and it is why the check is on the BYTES and not only on the parsed value.
struct HostilePath {
    const char* name;
    std::string bytes;

    // Which protection makes these bytes harmless, because there are two and
    // asking for the wrong one fails a writer that is doing the right thing.
    //
    // A character that cannot appear literally inside a double-quoted YAML
    // scalar has to be ESCAPED, so its bytes must not survive into the file.
    // `#` is not such a character: it introduces a comment only OUTSIDE a
    // quoted scalar, so quoting alone neutralises it and the bytes are supposed
    // to appear, between the quotes. The first version of this test asserted
    // the escaping form for every case and failed on `#` against correct
    // output.
    bool mustBeEscaped;
};

std::vector<HostilePath> hostilePaths()
{
    return {
        {"newline",        std::string("/tmp/we\nird.gds"), true},
        {"carriage_return", std::string("/tmp/we\rird.gds"), true},
        {"crlf",           std::string("/tmp/we\r\nird.gds"), true},
        {"tab",            std::string("/tmp/we\tird.gds"), true},
        {"nul_adjacent",   std::string("/tmp/we\x01ird.gds"), true},
        // Written by an author as six characters, not as an escape. A quoter
        // that escapes the backslash keeps them six; one that does not turns
        // them into a newline for the next reader.
        {"literal_backslash_n", std::string("/tmp/we\\nird.gds"), true},
        {"double_quote",   std::string("/tmp/we\"ird.gds"), true},
        // Quoted, not escaped, and that is correct. See mustBeEscaped above.
        {"hash",           std::string("/tmp/we#ird.gds"), false},
        // Line breaks in YAML 1.1 and ordinary characters in YAML 1.2, which is
        // the difference between this ecosystem's two readers.
        {"u2028",          std::string("/tmp/we\xE2\x80\xA8ird.gds"), true},
        {"u2029",          std::string("/tmp/we\xE2\x80\xA9ird.gds"), true},
        {"u0085",          std::string("/tmp/we\xC2\x85ird.gds"), true},
    };
}

TEST(ChipletSynthHostileInput, LayoutPathSurvivesOrTheSaveRefuses)
{
    // Bound once, so the size assertion and the sweep are the same vector.
    // Calling the helper twice would check one invocation and iterate
    // another, which is a weaker claim than it looks.
    const std::vector<HostilePath> hostiles = hostilePaths();
    ASSERT_EQ(hostiles.size(), 11u)
        << "the hostile-path corpus is not the one this test was written against. "
           "A sweep passes trivially over a corpus that has quietly shrunk, so the "
           "count is asserted before anything is swept.";

    for (const auto& hostile : hostiles) {
        const auto dir = caseDir(std::string("hostile_") + hostile.name);
        const auto path = dir / "import.chiplet";

        SingleGdsImportSpec spec;
        spec.gdsPath = hostile.bytes;
        spec.topCell = "TOP";
        spec.techId = "sg13g2";

        const std::string refusal = saveImport(spec, path);
        if (!refusal.empty()) {
            EXPECT_FALSE(std::filesystem::exists(path))
                << hostile.name << ": the save refused but left a file behind. "
                << refusal;
            std::filesystem::remove_all(dir);
            continue;
        }

        const std::string text = readWhole(path);
        if (hostile.mustBeEscaped) {
            EXPECT_EQ(text.find(hostile.bytes), std::string::npos)
                << hostile.name << ": the raw bytes appear verbatim in the "
                   "file, so whatever reads it next decides what they mean. "
                   "Written:\n" << text;
        } else {
            // The protection is asserted rather than the check being skipped:
            // the bytes have to be inside the quoted layout scalar, which is
            // what makes them harmless, and not loose anywhere else.
            EXPECT_NE(text.find("layout: \"" + hostile.bytes + "\""),
                      std::string::npos)
                << hostile.name << ": the bytes are in the file but not inside "
                   "the quoted scalar that makes them inert. Written:\n"
                << text;
        }

        ChipletFormat format;
        auto assembly = format.load(path.string());
        ASSERT_NE(assembly, nullptr) << hostile.name;
        auto* die = assembly->component("imported_die");
        ASSERT_NE(die, nullptr) << hostile.name;
        EXPECT_EQ(die->layout_path(), hostile.bytes)
            << hostile.name << ": the path came back changed.";

        std::filesystem::remove_all(dir);
    }
}

// The same contract for every other free-form string the spec carries. The
// component id and the technology id were emitted as BARE scalars by the old
// builder, which is the `id: 0755` class of defect: YAML 1.1 reads that as the
// integer 493 and YAML 1.2 as the string "0755", so the two readers of this
// format disagree about the same file.
TEST(ChipletSynthHostileInput, IdentifiersAreNotRetypedByTheReader)
{
    struct Case { const char* name; std::string id; std::string tech; };
    const std::vector<Case> cases = {
        {"octal_looking_id",   "0755",        "sg13g2"},
        {"yaml11_true_id",     "yes",         "sg13g2"},
        {"yaml11_false_id",    "no",          "sg13g2"},
        {"numeric_id",         "012",         "sg13g2"},
        {"colon_in_tech",      "imported_die", "we:ird"},
        {"hash_in_tech",       "imported_die", "we#ird"},
        {"newline_in_tech",    "imported_die", std::string("we\nird")},
        {"yaml11_bool_tech",   "imported_die", "on"},
    };

    ASSERT_EQ(cases.size(), 8u)
        << "the identifier corpus is not the one this test was written against; "
           "a loop over a shrunken corpus passes by not looking.";

    for (const auto& c : cases) {
        const auto dir = caseDir(std::string("ident_") + c.name);
        const auto path = dir / "import.chiplet";

        SingleGdsImportSpec spec;
        spec.gdsPath = "/tmp/a.gds";
        spec.componentId = c.id;
        spec.techId = c.tech;

        const std::string refusal = saveImport(spec, path);
        if (!refusal.empty()) {
            EXPECT_FALSE(std::filesystem::exists(path))
                << c.name << ": refused but left a file behind. " << refusal;
            std::filesystem::remove_all(dir);
            continue;
        }

        ChipletFormat format;
        auto assembly = format.load(path.string());
        ASSERT_NE(assembly, nullptr) << c.name;
        auto* die = assembly->component(c.id);
        ASSERT_NE(die, nullptr)
            << c.name << ": the component id did not come back as written.";
        EXPECT_EQ(die->technology(), c.tech)
            << c.name << ": the technology id did not come back as written.";
        ASSERT_NE(assembly->technology(c.tech), nullptr)
            << c.name << ": the technology key did not come back as written.";
        std::filesystem::remove_all(dir);
    }
}

// A refusal must not damage a document that is already there. This is the
// property the old path could not have: it wrote first and found out afterwards.
TEST(ChipletSynthHostileInput, ARefusalLeavesAnExistingFileIntact)
{
    const auto dir = caseDir("refusal_intact");
    const auto path = dir / "import.chiplet";
    const std::string existing = "# not a valid chiplet, and not ours to destroy\n";
    { std::ofstream out(path, std::ios::binary); out << existing; }

    // Every hostile input in turn, against the same occupied destination.
    // Bound once: the assertion and the sweep are the same vector.
    const std::vector<HostilePath> hostiles = hostilePaths();
    ASSERT_EQ(hostiles.size(), 11u)
        << "the hostile-path corpus is not the one this test was written against. "
           "A sweep passes trivially over a corpus that has quietly shrunk, so the "
           "count is asserted before anything is swept.";

    for (const auto& hostile : hostiles) {
        SingleGdsImportSpec spec;
        spec.gdsPath = hostile.bytes;
        spec.techId = "sg13g2";
        const std::string refusal = saveImport(spec, path);
        if (!refusal.empty()) {
            EXPECT_EQ(readWhole(path), existing)
                << hostile.name << ": a refused save changed the file it refused "
                                   "to write. " << refusal;
            // Put it back for the next case if a later arm overwrote it.
            std::ofstream out(path, std::ios::binary);
            out << existing;
        }
    }
    std::filesystem::remove_all(dir);
}


// --- The write policy, without a GUI -----------------------------------------
//
// writeSingleGdsImport carries the whole policy on purpose, so these four
// properties are asserted against the code that implements them rather than
// against a screenshot of a dialog.

// The import's Refused route, asserted where it is actually reachable.
//
// This is a DESTINATION-ADMISSION refusal and not a semantic-reader one, and
// the distinction is the whole point of the test. The spec below is valid and
// the document it builds is one the reader accepts; what is refused is the
// path. reject_invalid_destination_path rejects an empty or NUL-bearing
// destination, admit_destination calls it first, and save_snapshot runs
// admit_destination before write_file_atomically, so the refusal lands before
// any filesystem mutation. That is why nothing needs cleaning up afterwards and
// why "the directory is unchanged" is a claim about the route rather than about
// luck.
//
// The first version of this test looked for a refusal in the hostile-path
// corpus and its own META guard caught that it never found one. Chasing that, I
// drove eighteen further SPEC shapes through the real producer, including
// several no product caller could build, and every one was written, which is
// true and was the wrong conclusion to draw from. A spec is not the only input
// to writeSingleGdsImport. The destination is the other one, and it reaches the
// Other arm of the FailureKind switch directly. The bound I could honestly
// claim from spec probing alone was never about the arm.
//
// The semantic-reader refusal keeps its own control elsewhere:
// RejectedOutputPolicy.SuppressDropsTheSidecarAndChangesNothingElse drives a
// rule-8 document through both rejected-output policies and asserts that
// neither creates the destination and only Preserve leaves the sidecar. That is
// the deterministic reader/Suppress pair and it is not duplicated here.
TEST(ChipletImportWrite, AnInadmissibleDestinationIsRefusedBeforeAnythingIsWritten)
{
    const auto dir = caseDir("dest_admission");

    // Valid on purpose. If the spec were the problem this test would be about
    // the document again, which is the confusion it exists to end.
    SingleGdsImportSpec spec;
    spec.gdsPath = "/tmp/design.gds";
    spec.topCell = "TOP";
    spec.techId = "sg13g2";

    struct Case { const char* name; std::string destination; };
    const std::vector<Case> cases = {
        {"empty", std::string()},
        {"nul_inside", (dir / "a").string() + std::string(1, '\0') + "b.chiplet"},
        {"nul_at_end", (dir / "ok.chiplet").string() + std::string(1, '\0')},
    };

    ASSERT_EQ(cases.size(), 3u)
        << "the destination corpus is not the one this test was written against; "
           "a loop over a shrunken corpus passes by not looking.";

    for (const Case& c : cases) {
        int asked = 0;
        const auto result = writeSingleGdsImport(
            spec, c.destination, [&asked](const std::string&) {
                ++asked;
                return std::optional<std::string>{};
            });

        EXPECT_EQ(result.outcome, ImportWriteResult::Outcome::Refused)
            << c.name << ": an inadmissible destination did not reach the "
               "Refused route. " << result.message;

        // Not Cancelled, and this is the discriminating half. An inadmissible
        // path is not an occupied one, so asking the caller for a different
        // name would be answering a question nobody asked, and Cancelled would
        // look identical to a user who declined.
        EXPECT_EQ(asked, 0)
            << c.name << ": the caller was asked for another name for a path "
               "that was never occupied";

        // No claimed output and no fallback. WrittenToFallback also populates
        // path, so an empty path is what separates "refused" from "written
        // somewhere you did not ask for".
        EXPECT_TRUE(result.path.empty())
            << c.name << ": a refusal claimed the path " << result.path;
        EXPECT_FALSE(result.message.empty())
            << c.name << ": a refusal with no reason in it";
    }

    // And the directory the caller owns is exactly as it was found: no
    // document, no rejected-text sidecar, no temporary left over from a write
    // that got part way.
    std::vector<std::string> left;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        left.push_back(entry.path().filename().string());
    }
    std::sort(left.begin(), left.end());
    EXPECT_TRUE(left.empty())
        << "a refused import left " << ::testing::PrintToString(left)
        << " in the caller's directory";

    std::filesystem::remove_all(dir);
}

// Two things a spec can say that no document can carry, refused where
// the spec becomes a model rather than at the entry point's door.
//
// Where the guard lives is the decision these tests pin, so it is worth stating.
// buildSingleGdsAssembly is public and has callers other than
// writeSingleGdsImport, this file's own saveImport among them, and every
// ChipletSynthHostileInput case goes through that one. A check at the entry
// point would be invisible to all of them, which is the shape where a guard
// looks present and the harness walks past it. It throws with
// FailureKind::Other, stated at the throw and not defaulted, and the entry
// point builds inside its try, so the refusal arrives as Refused through the
// switch that already exists rather than as a second place deciding outcomes.
//
// So the two loops below assert BOTH sides, and neither replaces the other.
// The builder is called directly, because that is where the guard lives and
// what its other callers depend on; pinning only the entry point would repeat
// the same mistake from the other side, leaving the boundary itself unpinned.
// Then the same spec goes through writeSingleGdsImport, because a refusal
// classified as anything but Other still refuses at the builder and is then
// written to the fallback by the caller, which is the failure the kind exists
// to prevent and which the builder alone cannot show.
//
// Neither of these is a representability failure, and that is why the writer is
// not their author. The format states `.inf` and both readers read it back as
// infinity; a NUL is escaped, round-trips byte for byte and is decoded to the
// same NUL. Both survive every check the format has. What is wrong with them is
// downstream: no mesh builder can use an infinite size, and ::open stops at a
// NUL and reads a different file.

TEST(ChipletImportWrite, ANonFiniteDimensionIsRefusedAndNothingIsWritten)
{
    const double kNan = std::numeric_limits<double>::quiet_NaN();
    const double kInf = std::numeric_limits<double>::infinity();

    struct Case {
        const char* name;
        const char* field;   // the spec field the refusal must name
        double width;
        double height;
        double thickness;
    };
    // Every field, every non-finite value, one at a time, because a guard
    // written for one field passes a test that only tries that field.
    //
    // -inf is not redundant with +inf and it is the case a plausible wrong fix
    // misses. `-inf > 0.0` is false, so -inf already takes the default arm
    // below; a guard spelled as a tightening of the `> 0.0` test, or placed
    // after the substitution, would silently turn an impossible measurement
    // into 1000.0 and pass every +inf case.
    const std::vector<Case> cases = {
        {"width_nan",      "width",  kNan, 800.0, 200.0},
        {"width_pos_inf",  "width",  kInf, 800.0, 200.0},
        {"width_neg_inf",  "width", -kInf, 800.0, 200.0},
        {"height_nan",     "height", 600.0,  kNan, 200.0},
        {"height_pos_inf", "height", 600.0,  kInf, 200.0},
        {"height_neg_inf", "height", 600.0, -kInf, 200.0},
        {"thickness_nan",     "thickness", 600.0, 800.0,  kNan},
        {"thickness_pos_inf", "thickness", 600.0, 800.0,  kInf},
        {"thickness_neg_inf", "thickness", 600.0, 800.0, -kInf},
    };

    ASSERT_EQ(cases.size(), 9u)
        << "the non-finite dimension corpus is not the one this test was written against; "
           "a loop over a shrunken corpus passes by not looking.";

    for (const Case& c : cases) {
        const auto dir = caseDir(std::string("nonfinite_") + c.name);

        SingleGdsImportSpec spec;
        spec.gdsPath = "/tmp/design.gds";
        spec.topCell = "TOP";
        spec.techId = "sg13g2";
        spec.widthUm = c.width;
        spec.heightUm = c.height;
        spec.thicknessUm = c.thickness;

        // The boundary where the guard actually lives, asserted directly.
        //
        // The guard was put in the public builder rather than at the entry
        // point precisely because the entry point does not see every caller.
        // Asserting it only through writeSingleGdsImport would repeat that
        // mistake from the other side: it would pin the one caller that wraps
        // the builder and leave the builder's own contract unpinned, which is
        // what the other callers, this file's saveImport included, actually
        // depend on.
        //
        // The KIND is the load-bearing half. Other is the arm that answers
        // Refused; classified as DestinationIo instead, the same refusal would
        // route to writeToPrivateDirectory and the document would be written
        // after all, somewhere nobody asked for.
        try {
            (void)buildSingleGdsAssembly(spec);
            ADD_FAILURE() << c.name << ": the builder accepted a dimension no "
                             "document can carry";
        } catch (const ChipletFormatException& e) {
            EXPECT_EQ(e.kind(), FailureKind::Other)
                << c.name << ": a spec refusal classified as something other "
                   "than a document problem";
            EXPECT_NE(std::string(e.what()).find(c.field), std::string::npos)
                << c.name << ": the refusal does not say which field was wrong: "
                << e.what();
        } catch (const std::exception& e) {
            ADD_FAILURE() << c.name << ": refused with the wrong exception "
                             "type, so no caller can classify it: " << e.what();
        }

        int asked = 0;
        const auto result = writeSingleGdsImport(
            spec, (dir / "import.chiplet").string(),
            [&asked](const std::string&) {
                ++asked;
                return std::optional<std::string>{};
            });

        EXPECT_EQ(result.outcome, ImportWriteResult::Outcome::Refused)
            << c.name << ": a non-finite dimension was not refused. "
            << result.message;
        EXPECT_EQ(asked, 0)
            << c.name << ": the caller was asked for another name for a spec "
               "no name can fix";
        // WrittenToFallback also populates path, so this is what separates a
        // refusal from a document written somewhere nobody asked for.
        EXPECT_TRUE(result.path.empty())
            << c.name << ": a refusal claimed the path " << result.path;
        EXPECT_FALSE(result.message.empty())
            << c.name << ": a refusal with no reason in it";
        // Names the field that was wrong, so a guard that fires on the right
        // case for the wrong reason is not mistaken for a working one.
        EXPECT_NE(result.message.find(c.field), std::string::npos)
            << c.name << ": the refusal does not say which field was wrong: "
            << result.message;

        std::vector<std::string> left;
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            left.push_back(entry.path().filename().string());
        }
        EXPECT_TRUE(left.empty())
            << c.name << ": a refused import left "
            << ::testing::PrintToString(left) << " in the caller's directory";

        std::filesystem::remove_all(dir);
    }
}

// The boundary the guard must NOT cross is pinned elsewhere and deliberately
// not repeated here: ChipletSynth.DefaultsForInvalidBbox drives 0.0 through the
// same function and requires the defaults, because zero means the importer had
// nothing to measure. If this guard ever grows into "reject anything not
// positive", that test goes red, which is the polarity worth having.

TEST(ChipletImportWrite, ANulInAFilesystemFieldIsRefused)
{
    // Only the fields that name a file. topCell, techId, componentId and
    // assemblyName carry a NUL into the document unharmed as far as this rule
    // is concerned; refusing them would be a different rule with a different
    // reason, and inventing it here would make the message wrong.
    struct Case {
        const char* name;
        std::string SingleGdsImportSpec::* field;
    };
    const std::vector<Case> cases = {
        {"gdsPath",       &SingleGdsImportSpec::gdsPath},
        {"customLyp",     &SingleGdsImportSpec::customLyp},
        {"customStackup", &SingleGdsImportSpec::customStackup},
    };

    ASSERT_EQ(cases.size(), 3u)
        << "the NUL-bearing field corpus is not the one this test was written against; "
           "a loop over a shrunken corpus passes by not looking.";

    for (const Case& c : cases) {
        const auto dir = caseDir(std::string("nul_") + c.name);

        SingleGdsImportSpec spec;
        spec.gdsPath = "/tmp/design.gds";
        spec.topCell = "TOP";
        // Custom technology, so customLyp and customStackup are fields this
        // import would actually emit rather than dead inputs.
        spec.techId = "";
        spec.customLyp = "/tmp/layers.lyp";
        spec.customStackup = "/tmp/stackup.yaml";
        // Mid-string, not trailing. A trailing NUL is the case a length-blind
        // C-string check would catch by accident; one in the middle is the case
        // that opens a real, different file.
        spec.*(c.field) = std::string("/tmp/de") + '\0' + "sign.gds";

        // The builder's own boundary, for the same reason as the non-finite
        // loop above: the guard lives here, so this is where it is pinned, and
        // the kind decides whether a refusal can end up in the fallback.
        try {
            (void)buildSingleGdsAssembly(spec);
            ADD_FAILURE() << c.name << ": the builder accepted a path that "
                             "cannot name a file";
        } catch (const ChipletFormatException& e) {
            EXPECT_EQ(e.kind(), FailureKind::Other)
                << c.name << ": a spec refusal classified as something other "
                   "than a document problem";
            EXPECT_NE(std::string(e.what()).find(c.name), std::string::npos)
                << c.name << ": the refusal does not say which field was wrong: "
                << e.what();
        } catch (const std::exception& e) {
            ADD_FAILURE() << c.name << ": refused with the wrong exception "
                             "type, so no caller can classify it: " << e.what();
        }

        int asked = 0;
        const auto result = writeSingleGdsImport(
            spec, (dir / "import.chiplet").string(),
            [&asked](const std::string&) {
                ++asked;
                return std::optional<std::string>{};
            });

        EXPECT_EQ(result.outcome, ImportWriteResult::Outcome::Refused)
            << c.name << ": a NUL in a filesystem field was not refused. "
            << result.message;
        EXPECT_EQ(asked, 0) << c.name << ": asked for another name";
        EXPECT_TRUE(result.path.empty())
            << c.name << ": a refusal claimed the path " << result.path;
        EXPECT_NE(result.message.find(c.name), std::string::npos)
            << c.name << ": the refusal does not say which field was wrong: "
            << result.message;

        std::vector<std::string> left;
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            left.push_back(entry.path().filename().string());
        }
        EXPECT_TRUE(left.empty())
            << c.name << ": a refused import left "
            << ::testing::PrintToString(left) << " in the caller's directory";

        std::filesystem::remove_all(dir);
    }
}

// The one that discriminates WHERE the guard runs, and the only test here that
// a guard placed later would still fail.
//
// The destination is a directory this user cannot write. With a valid spec that
// is AnUnwritableDestinationFallsBackAndSaysWhereItWent: the document is good,
// the disk is not, and the import writes to a private directory and says where
// it went. With a refused spec it must not, because there is no good document
// to rescue, and a refusal answered by a fallback puts the thing that was
// refused on disk anyway, in a place the user did not choose and will not think
// to look.
//
// Every other assertion in this file would also pass for a guard that ran after
// admission. This one would not: reaching admission at all on this destination
// produces DestinationIo, and DestinationIo routes to writeToPrivateDirectory.
TEST(ChipletImportWrite, ARefusedSpecIsNotRescuedByTheFallback)
{
    namespace fs = std::filesystem;
    if (::geteuid() == 0) {
        GTEST_SKIP() << "running as root: directory permissions are not enforced";
    }
    const auto dir = caseDir("refused_no_fallback");
    const auto sealed = dir / "read_only_pdk";
    fs::create_directories(sealed);
    fs::permissions(sealed, fs::perms::owner_read | fs::perms::owner_exec);
    ASSERT_NE(::access(sealed.c_str(), W_OK), 0)
        << "the read-only directory is still writable by this user, so the "
           "failure under test was never created";

    SingleGdsImportSpec spec;
    spec.gdsPath = "/tmp/design.gds";
    spec.topCell = "TOP";
    spec.techId = "sg13g2";
    spec.widthUm = std::numeric_limits<double>::quiet_NaN();

    const auto preferred = sealed / "import.chiplet";
    const auto result = writeSingleGdsImport(
        spec, preferred.string(),
        [](const std::string&) { return std::optional<std::string>{}; });

    // Restored before the assertions so a failure still leaves a removable tree.
    fs::permissions(sealed, fs::perms::owner_all);

    EXPECT_EQ(result.outcome, ImportWriteResult::Outcome::Refused)
        << "a refused spec aimed at an unwritable destination came back as "
           "something other than a refusal: " << result.message;
    EXPECT_TRUE(result.path.empty())
        << "a refused spec was written to " << result.path;
    EXPECT_FALSE(fs::exists(preferred));

    fs::remove_all(dir);
}

// The corpus half, kept as an observation rather than deleted.
//
// Nothing in the hostile corpus is refused today: every one is written, with
// the bytes either escaped or quoted, which is correct, because these strings
// can all be safely quoted and inventing one that "must" be refused would be
// asserting a rule the format does not have. That is a fact about this writer
// and not a property anybody promised, so it is asserted rather than assumed.
//
// Fresh destination per case, deliberately. Reusing one path would make every
// case after the first hit an occupied destination and cancel, and a cancellation
// is occupancy, never a serialization refusal; a sweep that reused a path could
// be read as evidence of a semantic refusal that never happened.
TEST(ChipletImportWrite, NoHostileLayoutPathIsRefusedByTheImport)
{
    // Bound once: the assertion and the sweep are the same vector.
    const std::vector<HostilePath> hostiles = hostilePaths();
    ASSERT_EQ(hostiles.size(), 11u)
        << "the hostile-path corpus is not the one this test was written against. "
           "A sweep passes trivially over a corpus that has quietly shrunk, so the "
           "count is asserted before anything is swept.";

    for (const auto& hostile : hostiles) {
        const auto dir = caseDir(std::string("not_refused_") + hostile.name);
        SingleGdsImportSpec spec;
        spec.gdsPath = hostile.bytes;
        spec.techId = "sg13g2";

        const auto result = writeSingleGdsImport(
            spec, (dir / "import.chiplet").string(),
            [](const std::string&) { return std::optional<std::string>{}; });

        EXPECT_EQ(result.outcome, ImportWriteResult::Outcome::Written)
            << hostile.name << ": this path is no longer written. If it is now "
               "REFUSED, the import has a reachable refusal for the first time "
               "and ARefusalLeavesTheCallersDirectoryEmpty should assert the "
               "directory property through this entry point. " << result.message;
        std::filesystem::remove_all(dir);
    }
}

// The ordinary case, end to end through the writer under test.
TEST(ChipletImportWrite, AFreshDestinationIsWrittenAndReadsBack)
{
    const auto dir = caseDir("fresh");
    const auto path = dir / "import.chiplet";

    SingleGdsImportSpec spec;
    spec.gdsPath = "/tmp/design.gds";
    spec.topCell = "TOP";
    spec.widthUm = 800.0;
    spec.heightUm = 600.0;
    spec.techId = "sg13g2";

    const auto result = writeSingleGdsImport(
        spec, path.string(),
        [](const std::string&) { return std::optional<std::string>{}; });
    ASSERT_EQ(result.outcome, ImportWriteResult::Outcome::Written) << result.message;
    EXPECT_EQ(result.path, path.string());

    ChipletFormat format;
    auto assembly = format.load(path.string());
    ASSERT_NE(assembly, nullptr);
    auto* die = assembly->component("imported_die");
    ASSERT_NE(die, nullptr);
    EXPECT_EQ(die->layout_path(), "/tmp/design.gds");
    EXPECT_EQ(die->top_cell(), "TOP");
    std::filesystem::remove_all(dir);
}

// A destination that exists in ANY form is not written to, and that includes a
// dangling symlink, which is the case an exists() check calls free and then
// writes straight through.
TEST(ChipletImportWrite, AnOccupiedDestinationIsNeverReplaced)
{
    struct Occupant { const char* name; bool dangling; };
    for (const Occupant& occupant : {Occupant{"regular", false},
                                     Occupant{"live_link", false},
                                     Occupant{"dangling_link", true}}) {
        const auto dir = caseDir(std::string("occupied_") + occupant.name);
        const auto path = dir / "import.chiplet";
        const std::string sentinel = "# not ours to destroy\n";
        const auto elsewhere = dir / "elsewhere.chiplet";

        if (std::string(occupant.name) == "regular") {
            std::ofstream out(path, std::ios::binary); out << sentinel;
        } else {
            if (!occupant.dangling) {
                std::ofstream out(elsewhere, std::ios::binary); out << sentinel;
            }
            std::filesystem::create_symlink(elsewhere, path);
        }

        SingleGdsImportSpec spec;
        spec.gdsPath = "/tmp/design.gds";
        spec.techId = "sg13g2";

        int asked = 0;
        const auto result = writeSingleGdsImport(
            spec, path.string(), [&asked](const std::string&) {
                ++asked;
                return std::optional<std::string>{};  // cancel
            });

        EXPECT_EQ(asked, 1) << occupant.name
            << ": the caller was not asked for another name.";
        EXPECT_EQ(result.outcome, ImportWriteResult::Outcome::Cancelled)
            << occupant.name;
        if (!occupant.dangling) {
            EXPECT_EQ(readWhole(std::string(occupant.name) == "regular"
                                    ? path : elsewhere), sentinel)
                << occupant.name << ": the occupant was modified.";
        } else {
            EXPECT_TRUE(std::filesystem::is_symlink(path)) << occupant.name;
            EXPECT_FALSE(std::filesystem::exists(elsewhere))
                << occupant.name << ": the dangling link was written through.";
        }
        std::filesystem::remove_all(dir);
    }
}

// The realistic race: the destination is free when first checked and taken by
// the time the caller comes back from the name dialog, which is the interval
// that lasts as long as a human takes to answer. The loop must re-check rather
// than trust the answer it already has.
//
// The interval between the LAST check and the write itself is closed too: the
// import publishes through save_snapshot with no expected receipt, which makes
// the write an exclusive create. That race is tested at the save_snapshot
// level (SaveFailureKind.ATargetCreatedInsideTheExclusiveCreateIsNotOverwritten),
// not through this path.
TEST(ChipletImportWrite, ADestinationTakenWhileChoosingIsRefusedAgain)
{
    const auto dir = caseDir("race");
    const auto first = dir / "import.chiplet";
    const auto second = dir / "second.chiplet";
    const auto third = dir / "third.chiplet";
    const std::string sentinel = "# taken\n";
    { std::ofstream out(first, std::ios::binary); out << sentinel; }

    SingleGdsImportSpec spec;
    spec.gdsPath = "/tmp/design.gds";
    spec.techId = "sg13g2";

    std::vector<std::string> offered;
    const auto result = writeSingleGdsImport(
        spec, first.string(),
        [&](const std::string& occupied) -> std::optional<std::string> {
            offered.push_back(occupied);
            if (offered.size() == 1) {
                // Someone else creates the name we are about to return.
                std::ofstream out(second, std::ios::binary); out << sentinel;
                return second.string();
            }
            return third.string();
        });

    ASSERT_EQ(offered.size(), 2u)
        << "the second destination was accepted without being re-checked.";
    EXPECT_EQ(offered[1], second.string());
    ASSERT_EQ(result.outcome, ImportWriteResult::Outcome::Written) << result.message;
    EXPECT_EQ(result.path, third.string());
    EXPECT_EQ(readWhole(first), sentinel);
    EXPECT_EQ(readWhole(second), sentinel);
    std::filesystem::remove_all(dir);
}

// Cancelling leaves the directory exactly as it was, working files included.
TEST(ChipletImportWrite, CancellingWritesNothingAnywhere)
{
    const auto dir = caseDir("cancel");
    const auto path = dir / "import.chiplet";
    const std::string sentinel = "# untouched\n";
    { std::ofstream out(path, std::ios::binary); out << sentinel; }

    SingleGdsImportSpec spec;
    spec.gdsPath = "/tmp/design.gds";
    spec.techId = "sg13g2";

    const auto result = writeSingleGdsImport(
        spec, path.string(),
        [](const std::string&) { return std::optional<std::string>{}; });
    EXPECT_EQ(result.outcome, ImportWriteResult::Outcome::Cancelled);
    EXPECT_TRUE(result.path.empty());
    EXPECT_EQ(readWhole(path), sentinel);

    std::vector<std::string> left;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        left.push_back(entry.path().filename().string());
    }
    EXPECT_EQ(left, std::vector<std::string>{"import.chiplet"});
    std::filesystem::remove_all(dir);
}


// A filesystem problem and a document problem are different answers and must not
// wear each other's message. Telling someone their GDS cannot be described as a
// .chiplet, when the document is fine and the disk is not, sends them to debug
// the wrong thing.
//
// A real write failure at the destination the caller named, answered by the
// fallback rather than by a refusal, with the returned path asserted rather
// than assumed. The read-only PDK checkout is the case this exists for.
//
// The path is the whole contract of WrittenToFallback: the caller opens what
// comes back, so a result that says "written" and hands back a path that holds
// nothing is worse than a failure.
TEST(ChipletImportWrite, AnUnwritableDestinationFallsBackAndSaysWhereItWent)
{
    namespace fs = std::filesystem;
    if (::geteuid() == 0) {
        GTEST_SKIP() << "running as root: directory permissions are not enforced";
    }
    const auto dir = caseDir("fallback");
    const auto sealed = dir / "read_only_pdk";
    fs::create_directories(sealed);
    fs::permissions(sealed, fs::perms::owner_read | fs::perms::owner_exec);

    // Asserted, not assumed: run as a user who can write anywhere and the setup
    // above is a no-op and everything below would be about a save that worked.
    ASSERT_NE(::access(sealed.c_str(), W_OK), 0)
        << "the read-only directory is still writable by this user, so the "
           "failure under test was never created";

    SingleGdsImportSpec spec;
    spec.gdsPath = "/tmp/design.gds";
    spec.topCell = "TOP";
    spec.techId = "sg13g2";
    const auto preferred = sealed / "import.chiplet";
    const auto result = writeSingleGdsImport(
        spec, preferred.string(),
        [](const std::string&) { return std::optional<std::string>{}; });

    // Restored before the assertions so a failure still leaves a removable tree.
    fs::permissions(sealed, fs::perms::owner_all);

    ASSERT_EQ(result.outcome, ImportWriteResult::Outcome::WrittenToFallback)
        << "a destination this program could not write was reported as: "
        << result.message;
    EXPECT_FALSE(fs::exists(preferred))
        << "the read-only destination was written after all";

    ASSERT_FALSE(result.path.empty()) << "nothing was said about where it went";
    EXPECT_NE(result.path, preferred.string());
    ChipletFormat format;
    auto assembly = format.load(result.path);
    ASSERT_NE(assembly, nullptr)
        << "the returned path does not hold a readable document: " << result.path;
    auto* die = assembly->component("imported_die");
    ASSERT_NE(die, nullptr);
    EXPECT_EQ(die->layout_path(), "/tmp/design.gds");
    EXPECT_EQ(die->top_cell(), "TOP");

    // And it names the destination that failed, so the message is about a disk
    // and not about the document.
    EXPECT_NE(result.message.find(preferred.string()), std::string::npos)
        << "the fallback does not say which destination failed: " << result.message;
    EXPECT_EQ(result.message.find("Refusing to save"), std::string::npos)
        << "a filesystem problem was worded as a refusal: " << result.message;

    fs::remove_all(fs::path(result.path).parent_path());
    fs::remove_all(dir);
}

// And when the fallback cannot be written either, nothing is claimed. A result
// carrying a path nobody can open is the failure mode worth guarding here.
TEST(ChipletImportWrite, WhenTheFallbackFailsTooNoPathIsClaimed)
{
    namespace fs = std::filesystem;
    if (::geteuid() == 0) {
        GTEST_SKIP() << "running as root: directory permissions are not enforced";
    }
    const auto dir = caseDir("fallback_fails");
    const auto sealed = dir / "read_only_pdk";
    const auto noTemp = dir / "read_only_tmp";
    fs::create_directories(sealed);
    fs::create_directories(noTemp);
    fs::permissions(sealed, fs::perms::owner_read | fs::perms::owner_exec);
    fs::permissions(noTemp, fs::perms::owner_read | fs::perms::owner_exec);
    ASSERT_NE(::access(sealed.c_str(), W_OK), 0) << "destination is writable";
    ASSERT_NE(::access(noTemp.c_str(), W_OK), 0) << "temp directory is writable";

    const char* previous = ::getenv("TMPDIR");
    const std::string saved = previous ? previous : "";
    ::setenv("TMPDIR", noTemp.c_str(), 1);

    SingleGdsImportSpec spec;
    spec.gdsPath = "/tmp/design.gds";
    spec.techId = "sg13g2";
    const auto preferred = sealed / "import.chiplet";
    const auto result = writeSingleGdsImport(
        spec, preferred.string(),
        [](const std::string&) { return std::optional<std::string>{}; });

    if (previous) { ::setenv("TMPDIR", saved.c_str(), 1); } else { ::unsetenv("TMPDIR"); }
    fs::permissions(sealed, fs::perms::owner_all);
    fs::permissions(noTemp, fs::perms::owner_all);

    EXPECT_EQ(result.outcome, ImportWriteResult::Outcome::Failed)
        << "reported as: " << result.message;
    EXPECT_TRUE(result.path.empty())
        << "a path was handed back for a document that was never written: "
        << result.path;
    EXPECT_FALSE(fs::exists(preferred));
    EXPECT_TRUE(fs::is_empty(noTemp)) << "the fallback left something behind";
    fs::remove_all(dir);
}

// And the same thing seen from the import: a name it cannot resolve is a name
// to ask about, not a reason to write the document into a private directory.
// With no alternative offered that is Cancelled, and the fallback is what would
// have happened if occupancy were decided after resolution instead of before.
TEST(ChipletImportWrite, ANameThatCannotBeResolvedIsStillTaken)
{
    namespace fs = std::filesystem;
    const auto dir = caseDir("cycle");
    const auto self = dir / "self.chiplet";
    std::error_code ec;
    fs::create_symlink("self.chiplet", self, ec);
    ASSERT_FALSE(ec) << "could not create the self-referential link";

    std::error_code resolveEc;
    (void)fs::weakly_canonical(self, resolveEc);
    ASSERT_TRUE(static_cast<bool>(resolveEc))
        << "this platform resolves a self-referential symlink, so the ordering "
           "under test is not reachable from here";

    SingleGdsImportSpec spec;
    spec.gdsPath = "/tmp/design.gds";
    spec.techId = "sg13g2";
    const auto result = writeSingleGdsImport(
        spec, self.string(),
        [](const std::string&) { return std::optional<std::string>{}; });

    EXPECT_EQ(result.outcome, ImportWriteResult::Outcome::Cancelled)
        << "an occupied name was answered by writing somewhere else: "
        << result.message << " -> " << result.path;
    EXPECT_TRUE(result.path.empty()) << result.path;
    EXPECT_TRUE(fs::is_symlink(self)) << "the link was replaced";
    fs::remove_all(dir);
}

// The three outcomes of asking whether a destination is taken, asserted
// separately because two of them were once the same answer.
//
// std::filesystem::symlink_status reports an absent path by SETTING its
// error_code to ENOENT and returning a KNOWN status of not_found. A check that
// branches on the error_code therefore calls every free destination taken, which
// is not an edge case: it is the ordinary import into a directory that does not
// already contain the file. AFreshDestinationIsWrittenAndReadsBack above would
// have caught it, and did not, because it had never been run.
TEST(ChipletImportWrite, AbsentIsFreeUnknownIsTakenAndDanglingIsTaken)
{
    const auto dir = caseDir("occupancy");
    SingleGdsImportSpec spec;
    spec.gdsPath = "/tmp/design.gds";
    spec.techId = "sg13g2";
    const auto never = [](const std::string&) { return std::optional<std::string>{}; };

    // 1. Absent, in a directory that exists: free.
    const auto absent = dir / "absent.chiplet";
    auto result = writeSingleGdsImport(spec, absent.string(), never);
    EXPECT_EQ(result.outcome, ImportWriteResult::Outcome::Written)
        << "an absent destination was not treated as free: " << result.message;

    // 2. Dangling symlink: taken, and not written through.
    const auto dangling = dir / "dangling.chiplet";
    const auto nowhere = dir / "nowhere";
    std::filesystem::create_symlink(nowhere, dangling);
    result = writeSingleGdsImport(spec, dangling.string(), never);
    EXPECT_EQ(result.outcome, ImportWriteResult::Outcome::Cancelled)
        << "a dangling symlink was treated as free.";
    EXPECT_FALSE(std::filesystem::exists(nowhere))
        << "the dangling link was written through to its target.";

    // 3. Unsearchable parent: still never written through, but the answer
    //    changed and the change is deliberate. This used to be "cannot tell, so
    //    treat it as taken", which asks the caller for a different NAME; a name
    //    is not the problem when the directory cannot be searched, and the same
    //    dialog in the same directory would fail identically. It is now a
    //    destination failure, so the document goes to the private directory and
    //    the caller is told where. The property that mattered, that nothing is
    //    written through a destination this program cannot see, is unchanged.
    const auto sealed = dir / "sealed";
    std::filesystem::create_directories(sealed);
    std::filesystem::permissions(sealed, std::filesystem::perms::none);
    const bool sealedForReal = ::access((sealed / "x").c_str(), F_OK) != 0;
    std::string sealedFallback;
    if (sealedForReal) {
        result = writeSingleGdsImport(spec, (sealed / "x.chiplet").string(), never);
        EXPECT_EQ(result.outcome, ImportWriteResult::Outcome::WrittenToFallback)
            << "a destination whose status cannot be read was answered with: "
            << result.message;
        sealedFallback = result.path;
    }
    std::filesystem::permissions(sealed, std::filesystem::perms::owner_all);
    EXPECT_TRUE(sealedForReal)
        << "the sealed directory is still readable by this user, so the "
           "cannot-tell arm asserted nothing.";
    if (sealedForReal) {
        EXPECT_FALSE(std::filesystem::exists(sealed / "x.chiplet"))
            << "the unreadable destination was written after all.";
        if (!sealedFallback.empty()) {
            EXPECT_TRUE(std::filesystem::exists(sealedFallback))
                << "the fallback path does not exist: " << sealedFallback;
            std::filesystem::remove_all(
                std::filesystem::path(sealedFallback).parent_path());
        }
    }

    std::filesystem::remove_all(dir);
}

}  // namespace
}  // namespace chiplet
