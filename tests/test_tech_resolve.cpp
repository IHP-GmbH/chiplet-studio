// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * test_tech_resolve.cpp - Which sheet and stackup a technology is drawn with,
 * and what the views say when a declared one is not used.
 *
 * Covers the shared resolver (TechResolve.h), the `purpose` field it reads,
 * the two viewing path fields that now open with a note instead of refusing
 * the document, and the bundled interposer stackup against the copy the
 * interposer PDK ships.
 */

#include <gtest/gtest.h>

#include "core/Assembly.h"
#include "core/Component.h"
#include "core/LayerStackup.h"
#include "core/TechResolve.h"
#include "core/Technology.h"
#include "formats/ChipletFormat.h"
#include "view2d/LayerProperties.h"

#include <QByteArray>
#include <QCryptographicHash>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <yaml-cpp/yaml.h>

using namespace chiplet;
namespace fs = std::filesystem;

namespace {

std::string fixture(const std::string& rel)
{
    return std::string(FIXTURES_DIR) + "/" + rel;
}

// The resolver's built-in rung reads the configs dir; every test here pins it
// and puts the default back.
struct ConfigsDir {
    explicit ConfigsDir(const std::string& dir) { BlenderGDSConfigs::setConfigsDir(dir); }
    ~ConfigsDir() { BlenderGDSConfigs::setConfigsDir(""); }
    ConfigsDir(const ConfigsDir&) = delete;
    ConfigsDir& operator=(const ConfigsDir&) = delete;
};

// Snapshot and restore one environment variable.
class EnvGuard {
public:
    explicit EnvGuard(const char* name) : m_name(name)
    {
        if (const char* v = std::getenv(name)) {
            m_had = true;
            m_value = v;
        }
    }
    ~EnvGuard()
    {
        if (m_had) setenv(m_name.c_str(), m_value.c_str(), 1);
        else unsetenv(m_name.c_str());
    }
    EnvGuard(const EnvGuard&) = delete;
    EnvGuard& operator=(const EnvGuard&) = delete;

private:
    std::string m_name, m_value;
    bool m_had = false;
};

// A fresh directory under the system temp dir, removed on scope exit.
struct TempDir {
    fs::path path;
    explicit TempDir(const std::string& name)
        : path(fs::temp_directory_path() / name)
    {
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
};

void writeText(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream out(path);
    out << text;
}

std::string sha256(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string bytes = ss.str();
    return QCryptographicHash::hash(QByteArray(bytes.data(), static_cast<int>(bytes.size())),
                                    QCryptographicHash::Sha256).toHex().toStdString();
}

bool contains(const std::vector<std::string>& notes, const std::string& text)
{
    return std::find(notes.begin(), notes.end(), text) != notes.end();
}

std::string dump(const std::vector<std::string>& notes)
{
    std::string out;
    for (const auto& n : notes) out += "\n  " + n;
    return out.empty() ? "\n  (none)" : out;
}

// One technology with the two viewing fields, one die using it.
fs::path writeViewingChiplet(const fs::path& dir, const std::string& techId,
                             const std::string& lyp, const std::string& stackup,
                             const std::string& layout = "")
{
    const fs::path file = dir / "viewing.chiplet";
    std::ostringstream text;
    text << "format_version: \"1.0\"\n"
         << "assembly:\n"
         << "  name: \"Viewing\"\n"
         << "  units: \"um\"\n"
         << "technologies:\n"
         << "  " << techId << ":\n";
    if (!lyp.empty()) text << "    layer_properties: \"" << lyp << "\"\n";
    if (!stackup.empty()) text << "    stackup: \"" << stackup << "\"\n";
    text << "components:\n"
         << "  - id: die_a\n"
         << "    type: die\n"
         << "    technology: " << techId << "\n";
    if (!layout.empty()) text << "    layout: \"" << layout << "\"\n";
    text << "    dimensions: { width: 100, height: 100, thickness: 10 }\n"
         << "    position: { x: 0, y: 0, z: 0 }\n";
    writeText(file, text.str());
    return file;
}

} // namespace

// ---------------------------------------------------------------------------
// purpose
// ---------------------------------------------------------------------------

TEST(StackupPurpose, FillerIsReadAndOtherValuesAreKeptAsText)
{
    TempDir dir("chiplet_purpose_parse");
    const fs::path yaml = dir.path / "p.yaml";
    writeText(yaml,
              "attachment_surface_z: 1.0\n"
              "Metal: {index: 8, type: 0, z: 0.5, height: 0.4}\n"
              "MetalFill: {index: 8, type: 22, z: 0.5, height: 0.4, purpose: filler}\n"
              "Seal: {index: 39, type: 0, z: 0.5, height: 0.4, purpose: sealring}\n");
    LayerStackup s;
    ASSERT_TRUE(s.loadFromBlenderGDS(yaml.string()));

    ASSERT_NE(s.find(8, 0), nullptr);
    ASSERT_NE(s.find(8, 22), nullptr);
    ASSERT_NE(s.find(39, 0), nullptr);
    EXPECT_EQ(s.find(8, 0)->purpose, "");
    EXPECT_FALSE(s.find(8, 0)->isFiller());
    EXPECT_EQ(s.find(8, 22)->purpose, "filler");
    EXPECT_TRUE(s.find(8, 22)->isFiller());
    // Only `filler` acts; anything else survives as text and acts as nothing.
    EXPECT_EQ(s.find(39, 0)->purpose, "sealring");
    EXPECT_FALSE(s.find(39, 0)->isFiller());

    const std::vector<LayerKey> keys = fillerKeys(s);
    ASSERT_EQ(keys.size(), 1u);
    EXPECT_EQ(keys[0], LayerKey(8, 22));
}

// ---------------------------------------------------------------------------
// The bundled interposer stackup, and the PDK's own copy
// ---------------------------------------------------------------------------

namespace {
const std::vector<LayerKey> kInterposerFill = {
    LayerKey(50, 22), LayerKey(67, 22), LayerKey(126, 22), LayerKey(134, 22)};
}

TEST(TechResolve, BundledIntm4tm2HasFourFillersTheContractAttachAndARealSubstrate)
{
    ConfigsDir configs(CONFIGS_DIR);
    const TechStackup r = resolveTechnologyStackup(nullptr, "intm4tm2");
    ASSERT_EQ(r.source, StackupSource::BuiltIn);
    EXPECT_EQ(fs::path(r.path).filename(), "intm4tm2.yaml");
    EXPECT_TRUE(r.problem.empty());

    // Fill: the four metals' datatype 22, each at its metal's own z and height.
    EXPECT_EQ(fillerKeys(r.stackup), kInterposerFill);
    for (const LayerKey& key : kInterposerFill) {
        const LayerElevation* fill = r.stackup.find(key);
        const LayerElevation* metal = r.stackup.find(key.layer, 0);
        ASSERT_NE(fill, nullptr) << key.layer << "/" << key.datatype;
        ASSERT_NE(metal, nullptr) << key.layer << "/0";
        EXPECT_EQ(fill->purpose, "filler");
        EXPECT_DOUBLE_EQ(fill->z_bottom, metal->z_bottom) << key.layer;
        EXPECT_DOUBLE_EQ(fill->thickness, metal->thickness) << key.layer;
    }

    // The attach plane is the interposer PDK key's, which the contract names.
    ASSERT_TRUE(r.stackup.hasAttachmentSurfaceZ());
    EXPECT_DOUBLE_EQ(r.stackup.attachmentSurfaceZ(), 10.8103);

    // The substrate is the interposer PDK's 250 um, top at 0.
    const LayerElevation* substrate = r.stackup.find(40, 0);
    ASSERT_NE(substrate, nullptr);
    EXPECT_DOUBLE_EQ(substrate->thickness, 250.0);
    EXPECT_DOUBLE_EQ(substrate->z_bottom, -250.0);
}

namespace {
const char kPdkStackupCopySha[] =
    "d4111b96a0224b5b90f96a0e4417dd596b019c790ae8860147eac55cff2b29e6";

// The vendored PDK stackup, through the declared rung.
TechStackup pdkStackupCopy()
{
    const std::string pdkCopy = fixture("intm4tm2.stackup.yaml");
    Technology declared("intm4tm2");
    declared.set_stackup_source(pdkCopy);
    declared.set_stackup_path(pdkCopy);
    return resolveTechnologyStackup(&declared, "intm4tm2");
}

// Each layer of `other` that the PDK file also has is at the PDK's z and
// height. Returns how many layers it compared.
std::size_t expectPdkZAndHeight(const LayerStackup& pdk, const LayerStackup& other,
                                const std::string& what)
{
    std::size_t shared = 0;
    for (const LayerElevation& a : other.sortedLayers()) {
        const LayerElevation* b = pdk.find(a.key());
        if (!b) continue;
        const std::string at = what + " " + a.name + " " + std::to_string(a.layer) + "/"
                             + std::to_string(a.datatype);
        EXPECT_DOUBLE_EQ(a.z_bottom, b->z_bottom) << at;
        EXPECT_DOUBLE_EQ(a.thickness, b->thickness) << at;
        ++shared;
    }
    return shared;
}
} // namespace

// The interposer PDK ships its own Studio stackup. Studio keeps its bundled
// one, with the key's z and height for every layer the PDK file has, so the two
// must agree on each of those layers and on the attach plane. Names differ by
// convention only: X.filler is XFiller here and X.pillar is X_pillar. A layer
// the PDK file does not have is Studio's own and must say so (render_only).
TEST(TechResolve, PdkStackupFixtureAgreesWithBundledOnEverySharedLayer)
{
    const std::string pdkCopy = fixture("intm4tm2.stackup.yaml");
    ASSERT_EQ(sha256(pdkCopy), kPdkStackupCopySha)
        << "tests/fixtures/intm4tm2.stackup.yaml is not the copy its PROVENANCE.md "
           "declares. Change it in the interposer PDK and re-copy it.";

    ConfigsDir configs(CONFIGS_DIR);
    const TechStackup bundled = resolveTechnologyStackup(nullptr, "intm4tm2");
    ASSERT_EQ(bundled.source, StackupSource::BuiltIn);

    // Through the declared rung, the way a project would point at the PDK file.
    Technology declared("intm4tm2");
    declared.set_stackup_source(pdkCopy);
    declared.set_stackup_path(pdkCopy);
    const TechStackup pdk = resolveTechnologyStackup(&declared, "intm4tm2");
    ASSERT_EQ(pdk.source, StackupSource::Declared) << pdk.problem;
    EXPECT_EQ(pdk.path, pdkCopy);

    const auto bundledName = [](const std::string& name) {
        const std::pair<std::string, std::string> map[] = {{".filler", "Filler"},
                                                           {".pillar", "_pillar"}};
        for (const auto& [from, to] : map) {
            if (name.size() > from.size()
                && name.compare(name.size() - from.size(), from.size(), from) == 0) {
                return name.substr(0, name.size() - from.size()) + to;
            }
        }
        return name;
    };

    std::size_t shared = 0;
    for (const LayerElevation& b : pdk.stackup.sortedLayers()) {
        const std::string at = b.name + " " + std::to_string(b.layer) + "/"
                             + std::to_string(b.datatype);
        const LayerElevation* a = bundled.stackup.find(b.key());
        ASSERT_NE(a, nullptr) << at << " is missing from the bundled file";
        EXPECT_EQ(a->name, bundledName(b.name)) << at;
        EXPECT_EQ(a->purpose, b.purpose) << at;
        EXPECT_DOUBLE_EQ(a->z_bottom, b.z_bottom) << at;
        EXPECT_DOUBLE_EQ(a->thickness, b.thickness) << at;
        ++shared;
    }
    // Every layer of the PDK file, so none of the comparisons above is vacuous.
    EXPECT_EQ(shared, 15u);

    std::vector<std::string> ownOnly;
    for (const LayerElevation& a : bundled.stackup.sortedLayers()) {
        if (pdk.stackup.find(a.key())) continue;
        EXPECT_EQ(a.purpose, "render_only")
            << a.name << " is not in the PDK file and is not marked render_only";
        ownOnly.push_back(a.name);
    }
    EXPECT_EQ(ownOnly, std::vector<std::string>{"Passiv"});

    ASSERT_TRUE(bundled.stackup.hasAttachmentSurfaceZ());
    ASSERT_TRUE(pdk.stackup.hasAttachmentSurfaceZ());
    EXPECT_DOUBLE_EQ(bundled.stackup.attachmentSurfaceZ(), pdk.stackup.attachmentSurfaceZ());
}

// The GDS3D techfile beside the interposer sheet holds the same BEOL in nm, so
// it is a third copy of the PDK's z and heights. Its other layers are Studio's
// own: Passiv as the bundled file draws it, and the connection bodies standing
// on the PDK's plane.
TEST(TechResolve, InterposerTechfileAgreesWithThePdkStackupOnEverySharedLayer)
{
    ASSERT_EQ(sha256(fixture("intm4tm2.stackup.yaml")), kPdkStackupCopySha);
    const TechStackup pdk = pdkStackupCopy();
    ASSERT_EQ(pdk.source, StackupSource::Declared) << pdk.problem;
    ASSERT_TRUE(pdk.stackup.hasAttachmentSurfaceZ());
    const double plane = pdk.stackup.attachmentSurfaceZ();

    Technology tech("interposer");
    const fs::path techfile =
        fs::path(CONFIGS_DIR).parent_path() / "pdks" / "interposer" / "techfile" / "interposer.txt";
    ASSERT_TRUE(tech.load_process_def(techfile.string())) << techfile;
    const LayerStackup t = tech.createStackup();

    // Metal4 to TopMetal2, every one of them, so the comparison is not vacuous.
    EXPECT_EQ(expectPdkZAndHeight(pdk.stackup, t, "techfile"), 7u);

    std::vector<std::string> ownOnly;
    for (const LayerElevation& l : t.sortedLayers()) {
        if (!pdk.stackup.find(l.key())) ownOnly.push_back(l.name);
    }
    std::sort(ownOnly.begin(), ownOnly.end());
    EXPECT_EQ(ownOnly,
              (std::vector<std::string>{"CuPillar", "Passiv", "SnAgCap", "SolderBall"}));

    ConfigsDir configs(CONFIGS_DIR);
    const TechStackup bundled = resolveTechnologyStackup(nullptr, "intm4tm2");
    const LayerElevation* passiv = t.find(9, 0);
    const LayerElevation* bundledPassiv = bundled.stackup.find(9, 0);
    ASSERT_NE(passiv, nullptr);
    ASSERT_NE(bundledPassiv, nullptr);
    EXPECT_DOUBLE_EQ(passiv->z_bottom, bundledPassiv->z_bottom);
    EXPECT_DOUBLE_EQ(passiv->thickness, bundledPassiv->thickness);

    const LayerElevation* pillar = t.find(500, 35);
    const LayerElevation* cap = t.find(501, 35);
    const LayerElevation* ball = t.find(502, 36);
    ASSERT_NE(pillar, nullptr);
    ASSERT_NE(cap, nullptr);
    ASSERT_NE(ball, nullptr);
    EXPECT_DOUBLE_EQ(pillar->z_bottom, plane);
    EXPECT_DOUBLE_EQ(cap->z_bottom, pillar->z_top());
    EXPECT_DOUBLE_EQ(ball->z_bottom, plane);
}

// The last-resort stack draws an interposer with no stackup and no sheet. Every
// layer it has is one the PDK file has, at the PDK's z and height, and its top
// is the PDK's plane.
TEST(TechResolve, LastResortStackAgreesWithThePdkStackupOnEveryLayer)
{
    ASSERT_EQ(sha256(fixture("intm4tm2.stackup.yaml")), kPdkStackupCopySha);
    const TechStackup pdk = pdkStackupCopy();
    ASSERT_EQ(pdk.source, StackupSource::Declared) << pdk.problem;

    const LayerStackup last = Stackups::createInterposer();
    ASSERT_EQ(last.layerCount(), 7u);
    EXPECT_EQ(expectPdkZAndHeight(pdk.stackup, last, "last resort"), last.layerCount());
    ASSERT_TRUE(pdk.stackup.hasAttachmentSurfaceZ());
    EXPECT_DOUBLE_EQ(last.sortedLayers().back().z_top(), pdk.stackup.attachmentSurfaceZ());
}

// The gf180mcu PDK's own layer sheet names each layer's fill <layer>_Dummy.
// Each of them is fill in the bundled stackup, at its drawn layer's z and
// height, and nothing else is, so a gf180mcu die opens with its fill off.
TEST(TechResolve, BundledGf180mcuMarksEveryFillLayerItsPdkSheetNames)
{
    const std::string sheet = fixture("gf180mcu.lyp");
    ASSERT_EQ(sha256(sheet),
              "4d0e42a66b16f598bc4c401c193e60b8aeabc45bba7ff96d2f93e84cf6acb31f")
        << "tests/fixtures/gf180mcu.lyp is not the copy its PROVENANCE.md declares. "
           "Re-copy it from the PDK.";
    LayerPropertiesFile lyp;
    ASSERT_TRUE(lyp.load(sheet)) << lyp.error();
    std::vector<LayerKey> named;
    for (const auto& layer : lyp.layers()) {
        const std::string& n = layer.name;
        if (n.size() > 6 && n.compare(n.size() - 6, 6, "_Dummy") == 0) {
            named.push_back(layer.key);
        }
    }
    std::sort(named.begin(), named.end());
    ASSERT_EQ(named.size(), 8u);

    ConfigsDir configs(CONFIGS_DIR);
    const TechStackup r = resolveTechnologyStackup(nullptr, "gf180mcu");
    ASSERT_EQ(r.source, StackupSource::BuiltIn);
    std::vector<LayerKey> fill = fillerKeys(r.stackup);
    std::sort(fill.begin(), fill.end());
    EXPECT_EQ(fill, named);
    for (const LayerKey& key : named) {
        const LayerElevation* f = r.stackup.find(key);
        const LayerElevation* drawn = r.stackup.find(key.layer, 0);
        ASSERT_NE(f, nullptr) << key.layer << "/" << key.datatype;
        ASSERT_NE(drawn, nullptr) << key.layer << "/0";
        EXPECT_TRUE(f->isFiller()) << key.layer << "/" << key.datatype;
        EXPECT_DOUBLE_EQ(f->z_bottom, drawn->z_bottom) << key.layer;
        EXPECT_DOUBLE_EQ(f->thickness, drawn->thickness) << key.layer;
    }
}

// ---------------------------------------------------------------------------
// Fallbacks and their notes
// ---------------------------------------------------------------------------

TEST(TechResolve, MissingDeclaredStackupFallsBackToBuiltInAndSaysSo)
{
    ConfigsDir configs(CONFIGS_DIR);
    Technology tech("intm4tm2");
    tech.set_stackup_source("/nonexistent/intm4tm2.stackup.yaml");
    tech.set_stackup_path("/nonexistent/intm4tm2.stackup.yaml");

    const TechStackup r = resolveTechnologyStackup(&tech, "intm4tm2");
    EXPECT_EQ(r.source, StackupSource::BuiltIn);
    EXPECT_EQ(r.problem, "file not found: /nonexistent/intm4tm2.stackup.yaml");
    EXPECT_EQ(stackupNote("intm4tm2", r, false),
              "intm4tm2 stackup not loaded: file not found: "
              "/nonexistent/intm4tm2.stackup.yaml. Using Studio's built-in stackup "
              "intm4tm2.yaml.");
}

TEST(TechResolve, UnreadableDeclaredStackupSaysCouldNotRead)
{
    ConfigsDir configs(CONFIGS_DIR);
    TempDir dir("chiplet_unreadable_stackup");
    const fs::path bad = dir.path / "bad.yaml";
    writeText(bad, "- not\n- a\n- stackup\n");
    Technology tech("intm4tm2");
    tech.set_stackup_path(bad.string());

    const TechStackup r = resolveTechnologyStackup(&tech, "intm4tm2");
    EXPECT_EQ(r.source, StackupSource::BuiltIn);
    EXPECT_EQ(r.problem, "could not read " + bad.string());
    EXPECT_EQ(stackupNote("intm4tm2", r, false),
              "intm4tm2 stackup not loaded: could not read " + bad.string() +
              ". Using Studio's built-in stackup intm4tm2.yaml.");
}

TEST(TechResolve, NoStackupAnywhereNamesWhat3DDerivesInstead)
{
    ConfigsDir configs(CONFIGS_DIR);
    Technology tech("acme-node");
    tech.set_stackup_path("/nonexistent/acme.yaml");
    const TechStackup r = resolveTechnologyStackup(&tech, "acme-node");
    ASSERT_EQ(r.source, StackupSource::NotModeled);
    EXPECT_EQ(stackupNote("acme-node", r, true),
              "acme-node stackup not loaded: file not found: /nonexistent/acme.yaml. "
              "3D stacks the layer sheet's layers at a default thickness; fill is not "
              "recognised.");
    EXPECT_EQ(stackupNote("acme-node", r, false),
              "acme-node stackup not loaded: file not found: /nonexistent/acme.yaml. "
              "3D uses a generic interposer stackup; fill is not recognised.");
    EXPECT_EQ(stackupLabel(r, true), "layer-sheet");
    EXPECT_EQ(stackupLabel(r, false), "generic interposer");

    // Nothing declared: no note, whatever the fallback.
    EXPECT_EQ(stackupNote("acme-node", resolveTechnologyStackup(nullptr, "acme-node"), false), "");
}

TEST(TechResolve, SheetNoteNamesTheBuiltInSheetOrDefaultStyling)
{
    ConfigsDir configs(CONFIGS_DIR);
    Technology g2("ihp-sg13g2");
    g2.set_layer_properties_path("/nonexistent/sg13g2.lyp");
    const TechSheet withBuiltIn = resolveTechnologySheet(&g2, "ihp-sg13g2");
    ASSERT_EQ(withBuiltIn.source, SheetSource::BuiltIn);
    EXPECT_EQ(fs::path(withBuiltIn.path).filename(), "sg13g2.lyp");
    EXPECT_EQ(sheetNote(withBuiltIn),
              "ihp-sg13g2 layer sheet not loaded: file not found: /nonexistent/sg13g2.lyp. "
              "2D uses Studio's built-in sheet sg13g2.lyp; 3D uses generated colours.");

    Technology acme("acme-node");
    acme.set_layer_properties_path("/nonexistent/acme.lyp");
    const TechSheet none = resolveTechnologySheet(&acme, "acme-node");
    EXPECT_EQ(none.source, SheetSource::None);
    EXPECT_EQ(sheetNote(none),
              "acme-node layer sheet not loaded: file not found: /nonexistent/acme.lyp. "
              "2D uses default styling; 3D uses generated colours.");

    // A declared sheet that is there is used, with no note.
    const std::string real = withBuiltIn.path;
    Technology ok("ihp-sg13g2");
    ok.set_layer_properties_path(real);
    const TechSheet declared = resolveTechnologySheet(&ok, "ihp-sg13g2");
    EXPECT_EQ(declared.source, SheetSource::Declared);
    EXPECT_EQ(sheetNote(declared), "");
}

TEST(TechResolve, UnmodeledNoteListsUpToTheLimitThenCounts)
{
    EXPECT_EQ(unmodeledNote("U1", {}, "ihp-sg13g2.yaml"), "");
    EXPECT_EQ(unmodeledNote("U1", {LayerKey(20, 22), LayerKey(63, 0)}, "intm4tm2.yaml"),
              "3D: U1 has shapes on 20/22, 63/0, not in the intm4tm2.yaml stackup, not drawn.");

    std::vector<LayerKey> many;
    for (int i = 0; i < 10; ++i) many.emplace_back(200 + i, 0);
    EXPECT_EQ(unmodeledNote("U2", many, "ihp-sg13g2.yaml"),
              "3D: U2 has shapes on 200/0, 201/0, 202/0, 203/0, 204/0, 205/0, 206/0, "
              "207/0 and 2 more, not in the ihp-sg13g2.yaml stackup, not drawn.");
}

// ---------------------------------------------------------------------------
// Seating goes through the same resolver as drawing
// ---------------------------------------------------------------------------

namespace {
// merge_z.chiplet with a declared interposer stackup that is not there.
fs::path writeMergeZWithMissingStackup(const fs::path& dir)
{
    std::ifstream in(fixture("interconnect_merge/merge_z.chiplet"));
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string text = ss.str();
    const std::string anchor = "    description: \"body-less interposer stackup\"\n";
    const auto at = text.find(anchor);
    if (at == std::string::npos) return {};
    text.insert(at + anchor.size(), "    stackup: \"/nonexistent/intm4tm2.stackup.yaml\"\n");
    const fs::path file = dir / "merge_z_missing_stackup.chiplet";
    writeText(file, text);
    return file;
}
} // namespace

// Before the shared resolver, seating read the declared file and, when it
// failed, used the interposer thickness (200 + 48 = 248) while the view drew
// the bumps on the built-in file. Now both use the built-in file.
TEST(TechResolve, SeatingFallsBackToTheBuiltInStackupLikeTheView)
{
    EnvGuard env("INTERCONNECT_PDK_ROOT");
    const std::string base = fixture("interconnect_merge");
    ConfigsDir configs(base + "/configs");
    setenv("INTERCONNECT_PDK_ROOT", (base + "/pdk").c_str(), 1);

    TempDir dir("chiplet_seat_fallback");
    const fs::path file = writeMergeZWithMissingStackup(dir.path);
    ASSERT_FALSE(file.empty()) << "merge_z.chiplet no longer has the anchor line";

    ChipletFormat format;
    auto assembly = format.load(file.string());
    ASSERT_NE(assembly, nullptr);
    ASSERT_NE(assembly->technology("intm4tm2"), nullptr);
    ASSERT_EQ(assembly->technology("intm4tm2")->stackup_path(),
              "/nonexistent/intm4tm2.stackup.yaml");

    // CuPillar on the built-in surface 13.83, plus the 48 um stack.
    EXPECT_NEAR(assembly->calculate_component_z("die_a"), 61.83, 0.01);

    const std::vector<std::string> notes = technologyNotes(*assembly);
    EXPECT_TRUE(contains(notes,
        "intm4tm2 stackup not loaded: file not found: /nonexistent/intm4tm2.stackup.yaml. "
        "Using Studio's built-in stackup intm4tm2.yaml.")) << dump(notes);
}

TEST(TechResolve, PlacementNoteForADieTheFilePlacesOffItsBumps)
{
    EnvGuard env("INTERCONNECT_PDK_ROOT");
    const std::string base = fixture("interconnect_merge");
    ConfigsDir configs(base + "/configs");
    setenv("INTERCONNECT_PDK_ROOT", (base + "/pdk").c_str(), 1);

    ChipletFormat format;
    auto assembly = format.load(base + "/merge_z.chiplet");
    ASSERT_NE(assembly, nullptr);
    Component* die = assembly->component("die_a");
    ASSERT_NE(die, nullptr);

    Position3D p = die->position();
    p.z = 57.83;
    die->set_position(p);
    std::vector<std::string> notes = placementNotes(*assembly);
    ASSERT_EQ(notes.size(), 1u) << dump(notes);
    EXPECT_EQ(notes[0],
              "die_a is placed at z 57.83 um, but its cupillar bumps end at 61.83 um on "
              "intm4tm2.yaml. Drawn where the file places it.");

    // On its bumps: nothing to say.
    p.z = 61.83;
    die->set_position(p);
    EXPECT_TRUE(placementNotes(*assembly).empty()) << dump(placementNotes(*assembly));

    // z 0 is seated by the reader at open; not a placement the file made.
    p.z = 0.0;
    die->set_position(p);
    EXPECT_TRUE(placementNotes(*assembly).empty());
}

// The merge fixture's stackup puts the plane at 13.83. An interposer that
// declares its own plane seats its dies on that plane, and the notes say the
// two differ.
TEST(TechResolve, TheFilesPlaneWinsOverTheStackups)
{
    EnvGuard env("INTERCONNECT_PDK_ROOT");
    const std::string base = fixture("interconnect_merge");
    ConfigsDir configs(base + "/configs");
    setenv("INTERCONNECT_PDK_ROOT", (base + "/pdk").c_str(), 1);

    ChipletFormat format;
    auto assembly = format.load(base + "/merge_z.chiplet");
    ASSERT_NE(assembly, nullptr);
    Component* interposer = assembly->component("interposer");
    ASSERT_NE(interposer, nullptr);
    ASSERT_FALSE(interposer->attachment_surface_z());
    Component* die = assembly->component("die_a");
    ASSERT_NE(die, nullptr);

    // No plane in the file: the stackup's, under the 48 um stack.
    EXPECT_EQ(assembly->calculate_component_z("die_a"), 61.83);
    EXPECT_TRUE(placementNotes(*assembly).empty()) << dump(placementNotes(*assembly));

    // The die moves with the plane, as a file that declares it places the die.
    interposer->set_attachment_surface_z(10.8103);
    EXPECT_EQ(assembly->calculate_component_z("die_a"), 58.8103);
    Position3D p = die->position();
    p.z = 58.8103;
    die->set_position(p);
    const std::vector<std::string> notes = placementNotes(*assembly);
    ASSERT_EQ(notes.size(), 1u) << dump(notes);
    EXPECT_EQ(notes[0],
              "interposer declares its attach plane at 10.8103 um, but intm4tm2.yaml puts "
              "it at 13.83 um. Dies seat on the file's plane.");

    // The same plane in both: nothing to say.
    interposer->set_attachment_surface_z(13.83);
    EXPECT_EQ(assembly->calculate_component_z("die_a"), 61.83);
    p.z = 61.83;
    die->set_position(p);
    EXPECT_TRUE(placementNotes(*assembly).empty()) << dump(placementNotes(*assembly));
}

// The stackup 3D draws the interposer's technology with (AssemblyView takes it
// from drawnStackup) puts the bumps on the file's plane when the interposer
// declares one, so they are drawn where the die seats.
TEST(TechResolve, TheViewDrawsTheBumpsOnTheFilesPlane)
{
    EnvGuard env("INTERCONNECT_PDK_ROOT");
    const std::string base = fixture("interconnect_merge");
    ConfigsDir configs(base + "/configs");
    setenv("INTERCONNECT_PDK_ROOT", (base + "/pdk").c_str(), 1);

    ChipletFormat format;
    auto assembly = format.load(base + "/merge_z.chiplet");
    ASSERT_NE(assembly, nullptr);
    Component* interposer = assembly->component("interposer");
    ASSERT_NE(interposer, nullptr);

    // No plane in the file: the stackup's.
    DrawnStackup drawn = drawnStackup(assembly.get(), "intm4tm2", nullptr);
    ASSERT_TRUE(drawn.modeled);
    EXPECT_EQ(drawn.bodiesMerged, 2u);
    ASSERT_NE(drawn.stackup.find(500, 35), nullptr);
    EXPECT_EQ(drawn.stackup.find(500, 35)->z_bottom, 13.83);

    interposer->set_attachment_surface_z(10.8103);
    drawn = drawnStackup(assembly.get(), "intm4tm2", nullptr);
    EXPECT_EQ(drawn.bodiesMerged, 2u);
    const LayerElevation* pillar = drawn.stackup.find(500, 35);
    const LayerElevation* cap = drawn.stackup.find(501, 35);
    ASSERT_NE(pillar, nullptr);
    ASSERT_NE(cap, nullptr);
    EXPECT_EQ(pillar->z_bottom, 10.8103);
    EXPECT_DOUBLE_EQ(cap->z_top(), 58.8103);
    EXPECT_EQ(assembly->calculate_component_z("die_a"), 58.8103);

    // A technology no interposer uses gets no bodies.
    EXPECT_EQ(drawnStackup(assembly.get(), "ihp-sg13g2", nullptr).bodiesMerged, 0u);
}

// ---------------------------------------------------------------------------
// The two viewing path fields open with a note; every other path still refuses
// ---------------------------------------------------------------------------

TEST(ViewingPaths, UnsetFlowTechOpensWithNotesAndSavesTheTextBack)
{
    EnvGuard env("CHIPLET_FLOW_TECH");
    unsetenv("CHIPLET_FLOW_TECH");
    ConfigsDir configs(CONFIGS_DIR);
    TempDir dir("chiplet_flow_tech_unset");
    const std::string lyp = "${CHIPLET_FLOW_TECH}/ihp-sg13g2/layers.lyp";
    const std::string stk = "${CHIPLET_FLOW_TECH}/ihp-sg13g2/stackup.yaml";
    const fs::path file = writeViewingChiplet(dir.path, "ihp-sg13g2", lyp, stk);

    ChipletFormat format;
    std::unique_ptr<Assembly> assembly;
    ASSERT_NO_THROW(assembly = format.load(file.string()));
    ASSERT_NE(assembly, nullptr);
    const Technology* tech = assembly->technology("ihp-sg13g2");
    ASSERT_NE(tech, nullptr);
    EXPECT_EQ(tech->layer_properties_path(), "");
    EXPECT_EQ(tech->layer_properties_source(), lyp);
    EXPECT_EQ(tech->layer_properties_problem(), "${CHIPLET_FLOW_TECH} is not set");
    EXPECT_EQ(tech->stackup_path(), "");
    EXPECT_EQ(tech->stackup_problem(), "${CHIPLET_FLOW_TECH} is not set");

    const std::vector<std::string> notes = technologyNotes(*assembly);
    EXPECT_TRUE(contains(notes,
        "ihp-sg13g2 layer sheet not loaded: ${CHIPLET_FLOW_TECH} is not set. 2D uses "
        "Studio's built-in sheet sg13g2.lyp; 3D uses generated colours.")) << dump(notes);
    EXPECT_TRUE(contains(notes,
        "ihp-sg13g2 stackup not loaded: ${CHIPLET_FLOW_TECH} is not set. Using Studio's "
        "built-in stackup ihp-sg13g2.yaml.")) << dump(notes);

    // Saved as written, not as the empty path nor the fallback.
    const fs::path out = dir.path / "saved.chiplet";
    ASSERT_NO_THROW(format.save(*assembly, out.string()));
    const YAML::Node root = YAML::LoadFile(out.string());
    EXPECT_EQ(root["technologies"]["ihp-sg13g2"]["layer_properties"].as<std::string>(), lyp);
    EXPECT_EQ(root["technologies"]["ihp-sg13g2"]["stackup"].as<std::string>(), stk);
}

TEST(ViewingPaths, FlowTechSetToAFolderResolvesBothFiles)
{
    EnvGuard env("CHIPLET_FLOW_TECH");
    TempDir dir("chiplet_flow_tech_set");
    const fs::path techDir = dir.path / "inputs" / "tech";
    fs::create_directories(techDir);
    setenv("CHIPLET_FLOW_TECH", techDir.string().c_str(), 1);
    const fs::path file = writeViewingChiplet(
        dir.path / "proj", "ihp-sg13g2", "${CHIPLET_FLOW_TECH}/ihp-sg13g2/layers.lyp",
        "${CHIPLET_FLOW_TECH}/ihp-sg13g2/stackup.yaml");

    ChipletFormat format;
    auto assembly = format.load(file.string());
    ASSERT_NE(assembly, nullptr);
    const Technology* tech = assembly->technology("ihp-sg13g2");
    ASSERT_NE(tech, nullptr);
    EXPECT_EQ(tech->layer_properties_path(), (techDir / "ihp-sg13g2" / "layers.lyp").string());
    EXPECT_EQ(tech->stackup_path(), (techDir / "ihp-sg13g2" / "stackup.yaml").string());
    EXPECT_EQ(tech->layer_properties_problem(), "");
    EXPECT_EQ(tech->stackup_problem(), "");
}

TEST(ViewingPaths, FlowTechSetToAFileIsNotAFolder)
{
    EnvGuard env("CHIPLET_FLOW_TECH");
    TempDir dir("chiplet_flow_tech_file");
    const fs::path notDir = dir.path / "tech.txt";
    writeText(notDir, "x");
    setenv("CHIPLET_FLOW_TECH", notDir.string().c_str(), 1);
    const fs::path file = writeViewingChiplet(
        dir.path / "proj", "ihp-sg13g2", "${CHIPLET_FLOW_TECH}/ihp-sg13g2/layers.lyp", "");

    ChipletFormat format;
    auto assembly = format.load(file.string());
    ASSERT_NE(assembly, nullptr);
    EXPECT_EQ(assembly->technology("ihp-sg13g2")->layer_properties_problem(),
              "${CHIPLET_FLOW_TECH} is set to '" + notDir.string() + "', which is not a folder");
}

TEST(ViewingPaths, UnknownVariableInLayerPropertiesIsANoteWithDefaultStyling)
{
    ConfigsDir configs(CONFIGS_DIR);
    TempDir dir("chiplet_unknown_var_lyp");
    const fs::path file = writeViewingChiplet(dir.path, "acme-node",
                                              "${NOT_A_STUDIO_VAR}/acme.lyp", "");
    ChipletFormat format;
    std::unique_ptr<Assembly> assembly;
    ASSERT_NO_THROW(assembly = format.load(file.string()));
    ASSERT_NE(assembly, nullptr);

    const std::vector<std::string> notes = technologyNotes(*assembly);
    EXPECT_TRUE(contains(notes,
        "acme-node layer sheet not loaded: ${NOT_A_STUDIO_VAR} is not a path variable "
        "Studio knows. 2D uses default styling; 3D uses generated colours.")) << dump(notes);
}

TEST(ViewingPaths, LayoutPathsStillRefuseAnUnresolvedVariable)
{
    EnvGuard env("CHIPLET_FLOW_TECH");
    unsetenv("CHIPLET_FLOW_TECH");
    TempDir dir("chiplet_flow_tech_layout");
    ChipletFormat format;

    const fs::path unset = writeViewingChiplet(dir.path / "a", "ihp-sg13g2", "", "",
                                               "${CHIPLET_FLOW_TECH}/die.gds");
    try {
        format.load(unset.string());
        ADD_FAILURE() << "a layout path with an unset ${CHIPLET_FLOW_TECH} opened";
    } catch (const ChipletFormatException& e) {
        EXPECT_NE(std::string(e.what()).find(
                      "set the CHIPLET_FLOW_TECH environment variable to a directory"),
                  std::string::npos) << e.what();
    }

    const fs::path unknown = writeViewingChiplet(dir.path / "b", "ihp-sg13g2", "", "",
                                                 "${NOT_A_STUDIO_VAR}/die.gds");
    EXPECT_THROW(format.load(unknown.string()), ChipletFormatException);
}

// ---------------------------------------------------------------------------
// The two roots example specs declare: each through set-with-marker,
// set-without-marker, unset beside a checkout, and unset with none. The roots
// sit one folder down ("pdks", "tools") so only the project placed beside them
// can find them by walking up.
// ---------------------------------------------------------------------------

TEST(TechResolve, Gf180mcuPdkRootResolvesFromTheEnvironmentOrBesideTheProject)
{
    EnvGuard env("GF180MCU_PDK_ROOT");
    ConfigsDir configs(CONFIGS_DIR);
    TempDir dir("chiplet_gf180_root");
    const std::string lyp = "${GF180MCU_PDK_ROOT}/gf180mcuD/libs.tech/klayout/tech/gf180mcu.lyp";
    const fs::path root = dir.path / "pdks" / "gf180mcu";
    const fs::path sheet = root / "gf180mcuD" / "libs.tech" / "klayout" / "tech" / "gf180mcu.lyp";
    writeText(sheet, "<?xml version=\"1.0\"?>\n<layer-properties/>\n");
    const fs::path bare = dir.path / "bare";
    fs::create_directories(bare);

    auto load = [&](const fs::path& project) {
        ChipletFormat format;
        return format.load(writeViewingChiplet(project, "gf180mcu", lyp, "").string());
    };

    setenv("GF180MCU_PDK_ROOT", root.string().c_str(), 1);
    {
        auto a = load(dir.path / "set");
        const Technology* t = a->technology("gf180mcu");
        EXPECT_EQ(t->layer_properties_problem(), "");
        EXPECT_EQ(t->layer_properties_path(), sheet.string());
        EXPECT_EQ(resolveTechnologySheet(t, "gf180mcu").source, SheetSource::Declared);
        EXPECT_TRUE(technologyNotes(*a).empty()) << dump(technologyNotes(*a));
    }

    setenv("GF180MCU_PDK_ROOT", bare.string().c_str(), 1);
    {
        auto a = load(dir.path / "bare_project");
        const Technology* t = a->technology("gf180mcu");
        EXPECT_EQ(t->layer_properties_path(), "");
        EXPECT_EQ(t->layer_properties_problem(),
                  "${GF180MCU_PDK_ROOT} is set to '" + bare.string()
                      + "', which has no gf180mcuD/libs.tech/klayout, and no gf180mcu "
                        "checkout was found above the file");
    }

    unsetenv("GF180MCU_PDK_ROOT");
    {
        auto a = load(dir.path / "pdks" / "project");
        const Technology* t = a->technology("gf180mcu");
        EXPECT_EQ(t->layer_properties_problem(), "");
        EXPECT_TRUE(fs::equivalent(t->layer_properties_path(), sheet))
            << t->layer_properties_path();
    }
    {
        auto a = load(dir.path / "alone");
        const Technology* t = a->technology("gf180mcu");
        EXPECT_EQ(t->layer_properties_path(), "");
        const std::vector<std::string> notes = technologyNotes(*a);
        EXPECT_TRUE(contains(notes,
            "gf180mcu layer sheet not loaded: ${GF180MCU_PDK_ROOT} is not set, and no "
            "gf180mcu checkout was found above the file. 2D uses default styling; 3D uses "
            "generated colours.")) << dump(notes);
    }
}

TEST(TechResolve, ChipletStudioRootResolvesFromTheEnvironmentOrBesideTheProject)
{
    EnvGuard env("CHIPLET_STUDIO_ROOT");
    ConfigsDir configs(CONFIGS_DIR);
    TempDir dir("chiplet_studio_root");
    const std::string stk = "${CHIPLET_STUDIO_ROOT}/configs/stackups/ihp-sg13g2.yaml";
    const fs::path root = dir.path / "tools" / "chiplet-studio";
    const fs::path file = root / "configs" / "stackups" / "ihp-sg13g2.yaml";
    writeText(file, "attachment_surface_z: 1.0\n"
                    "Metal1: {index: 8, type: 0, z: 0.5, height: 0.4}\n");
    const fs::path bare = dir.path / "bare";
    fs::create_directories(bare);

    auto load = [&](const fs::path& project) {
        ChipletFormat format;
        return format.load(writeViewingChiplet(project, "ihp-sg13g2", "", stk).string());
    };

    setenv("CHIPLET_STUDIO_ROOT", root.string().c_str(), 1);
    {
        auto a = load(dir.path / "set");
        const Technology* t = a->technology("ihp-sg13g2");
        EXPECT_EQ(t->stackup_problem(), "");
        EXPECT_EQ(t->stackup_path(), file.string());
        const TechStackup s = resolveTechnologyStackup(t, "ihp-sg13g2");
        EXPECT_EQ(s.source, StackupSource::Declared);
        EXPECT_EQ(s.stackup.layerCount(), 1u);
    }

    setenv("CHIPLET_STUDIO_ROOT", bare.string().c_str(), 1);
    {
        auto a = load(dir.path / "bare_project");
        const Technology* t = a->technology("ihp-sg13g2");
        EXPECT_EQ(t->stackup_path(), "");
        EXPECT_EQ(t->stackup_problem(),
                  "${CHIPLET_STUDIO_ROOT} is set to '" + bare.string()
                      + "', which has no configs/stackups, and no chiplet-studio "
                        "checkout was found above the file");
    }

    unsetenv("CHIPLET_STUDIO_ROOT");
    {
        auto a = load(dir.path / "tools" / "project");
        const Technology* t = a->technology("ihp-sg13g2");
        EXPECT_EQ(t->stackup_problem(), "");
        EXPECT_TRUE(fs::equivalent(t->stackup_path(), file)) << t->stackup_path();
    }
    {
        auto a = load(dir.path / "alone");
        const std::vector<std::string> notes = technologyNotes(*a);
        EXPECT_TRUE(contains(notes,
            "ihp-sg13g2 stackup not loaded: ${CHIPLET_STUDIO_ROOT} is not set, and no "
            "chiplet-studio checkout was found above the file. Using Studio's built-in "
            "stackup ihp-sg13g2.yaml.")) << dump(notes);
    }
}
