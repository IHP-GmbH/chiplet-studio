/**
 * test_layer_stackup_blendergds.cpp - Tests for BlenderGDS YAML integration
 */

#include <gtest/gtest.h>
#include "core/LayerStackup.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>

using namespace chiplet;

// Helper to check if a file exists
static bool fileExists(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}

// Write a YAML file, creating parent directories
static void writeYaml(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    out << content;
}

// BlenderGDS stackup loading tests

TEST(BlenderGDSStackup, LoadSG13G2) {
    std::string path = std::string(CONFIGS_DIR) + "/stackups/ihp-sg13g2.yaml";
    if (!fileExists(path)) {
        GTEST_SKIP() << "BlenderGDS config not found: " << path;
    }

    LayerStackup stackup;
    ASSERT_TRUE(stackup.loadFromBlenderGDS(path));

    // SG13G2 has many layers (including filler variants)
    EXPECT_GE(stackup.layerCount(), 20u);

    // Spot-check Metal1: index=8, type=0
    const LayerElevation* metal1 = stackup.find(8, 0);
    ASSERT_NE(metal1, nullptr);
    EXPECT_EQ(metal1->name, "Metal1");
    EXPECT_NEAR(metal1->z_bottom, 0.640, 0.01);
    EXPECT_NEAR(metal1->thickness, 0.40, 0.01);

    // Spot-check TopMetal2: index=134, type=0
    const LayerElevation* topMetal2 = stackup.find(134, 0);
    ASSERT_NE(topMetal2, nullptr);
    EXPECT_EQ(topMetal2->name, "TopMetal2");
    EXPECT_NEAR(topMetal2->z_bottom, 10.83, 0.01);
    EXPECT_NEAR(topMetal2->thickness, 3.0, 0.01);
}

TEST(BlenderGDSStackup, LoadSky130) {
    std::string path = std::string(CONFIGS_DIR) + "/stackups/sky130.yaml";
    if (!fileExists(path)) {
        GTEST_SKIP() << "BlenderGDS config not found: " << path;
    }

    LayerStackup stackup;
    ASSERT_TRUE(stackup.loadFromBlenderGDS(path));

    // SKY130 has fewer layers than SG13G2
    EXPECT_GE(stackup.layerCount(), 10u);

    // Spot-check met1: index=68, type=20
    const LayerElevation* met1 = stackup.find(68, 20);
    ASSERT_NE(met1, nullptr);
    EXPECT_EQ(met1->name, "met1");
    EXPECT_NEAR(met1->z_bottom, 1.38, 0.01);
}

// Color scheme loading tests

TEST(BlenderGDSColorScheme, LoadRealistic) {
    std::string path = std::string(CONFIGS_DIR) + "/stackups/colors/ihp-sg13g2/realistic.yaml";
    if (!fileExists(path)) {
        GTEST_SKIP() << "BlenderGDS color scheme not found: " << path;
    }

    LayerColorScheme scheme;
    ASSERT_TRUE(scheme.loadFromYAML(path));
    EXPECT_EQ(scheme.name, "Realistic");
    EXPECT_FALSE(scheme.layers.empty());

    // Check Metal1 color
    const LayerColorEntry* metal1 = scheme.find("Metal1");
    ASSERT_NE(metal1, nullptr);
    EXPECT_NEAR(metal1->color[0], 0.63f, 0.01f);  // R
    EXPECT_NEAR(metal1->color[1], 0.64f, 0.01f);  // G
    EXPECT_NEAR(metal1->color[2], 0.65f, 0.01f);  // B
    EXPECT_NEAR(metal1->color[3], 1.0f, 0.01f);   // A
    EXPECT_NEAR(metal1->metallic, 0.8f, 0.01f);
    EXPECT_NEAR(metal1->roughness, 0.3f, 0.01f);

    // Check that non-metallic layer has default values
    const LayerColorEntry* activ = scheme.find("Activ");
    ASSERT_NE(activ, nullptr);
    EXPECT_NEAR(activ->metallic, 0.0f, 0.01f);  // No metallic specified
}

// Config path resolution tests

TEST(BlenderGDSConfigs, ResolveStackupPath) {
    BlenderGDSConfigs::setConfigsDir(CONFIGS_DIR);

    std::string path;

    path = BlenderGDSConfigs::stackupPath("sg13g2");
    EXPECT_FALSE(path.empty());
    EXPECT_NE(path.find("ihp-sg13g2.yaml"), std::string::npos);

    path = BlenderGDSConfigs::stackupPath("sky130");
    EXPECT_FALSE(path.empty());
    EXPECT_NE(path.find("sky130.yaml"), std::string::npos);

    path = BlenderGDSConfigs::stackupPath("gf180mcu");
    EXPECT_FALSE(path.empty());
    EXPECT_NE(path.find("gf180mcu.yaml"), std::string::npos);

    path = BlenderGDSConfigs::stackupPath("IHP_SG13G2");
    EXPECT_FALSE(path.empty());
    EXPECT_NE(path.find("ihp-sg13g2.yaml"), std::string::npos);
}

TEST(BlenderGDSConfigs, ResolveColorSchemePath) {
    BlenderGDSConfigs::setConfigsDir(CONFIGS_DIR);

    std::string path = BlenderGDSConfigs::colorSchemePath("sg13g2", "realistic");
    EXPECT_FALSE(path.empty());
    EXPECT_NE(path.find("ihp-sg13g2/realistic.yaml"), std::string::npos);

    path = BlenderGDSConfigs::colorSchemePath("sky130", "fancy");
    EXPECT_FALSE(path.empty());
    EXPECT_NE(path.find("sky130/fancy.yaml"), std::string::npos);
}

TEST(BlenderGDSConfigs, UnknownTechReturnsEmpty) {
    BlenderGDSConfigs::setConfigsDir(CONFIGS_DIR);

    std::string path = BlenderGDSConfigs::stackupPath("unknown_tech_xyz");
    EXPECT_TRUE(path.empty());

    path = BlenderGDSConfigs::colorSchemePath("unknown_tech_xyz");
    EXPECT_TRUE(path.empty());
}

// ---------------------------------------------------------------------
// Scalar metadata keys (z_reference / attachment_surface_z) and the shared
// interconnect-fragment merge helper (interconnect_render_contract.md, L1).
// Hermetic: temp-file YAMLs plus a fake INTERCONNECT_PDK_ROOT tree; no
// dependency on the real configs or sibling checkouts.
// ---------------------------------------------------------------------

namespace {

// Lays out a fake interconnect PDK with relative ("unit_rel") and legacy
// absolute ("unit_abs") fragments, returning the tree root.
std::filesystem::path makeFragmentTree() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "chiplet_lsu_frag_merge";
    fs::remove_all(root);
    const fs::path frags =
        root / "libs.tech" / "chiplet_studio" / "stackup_fragments";
    writeYaml(frags / "unit_rel.stackup.yaml",
              "z_reference: attachment_surface\n"
              "Body:\n  index: 500\n  type: 35\n  z: 0.0\n  height: 10.0\n");
    writeYaml(frags / "unit_abs.stackup.yaml",
              "Body:\n  index: 500\n  type: 35\n  z: 7.5\n  height: 10.0\n");
    return root;
}

} // namespace

TEST(BlenderGDSStackupMetadata, ParsesScalarKeysAndClearResetsThem) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "chiplet_lsu_meta";
    fs::remove_all(dir);

    writeYaml(dir / "with_meta.yaml",
              "z_reference: attachment_surface\n"
              "attachment_surface_z: 13.83\n"
              "Pad:\n  index: 134\n  type: 0\n  z: 10.83\n  height: 3.0\n");
    writeYaml(dir / "plain.yaml",
              "Pad:\n  index: 134\n  type: 0\n  z: 10.83\n  height: 3.0\n");

    LayerStackup s;
    ASSERT_TRUE(s.loadFromBlenderGDS((dir / "with_meta.yaml").string()));
    EXPECT_EQ(s.zReference(), "attachment_surface");
    ASSERT_TRUE(s.hasAttachmentSurfaceZ());
    EXPECT_NEAR(s.attachmentSurfaceZ(), 13.83, 1e-9);
    // The scalar keys must not leak into the layer map.
    EXPECT_EQ(s.layerCount(), 1u);

    // Loading a file without the keys (load clears first) resets them.
    ASSERT_TRUE(s.loadFromBlenderGDS((dir / "plain.yaml").string()));
    EXPECT_TRUE(s.zReference().empty());
    EXPECT_FALSE(s.hasAttachmentSurfaceZ());
}

TEST(InterconnectFragmentMerge, RelativeFragmentOffsetByDeclaredSurface) {
    const auto root = makeFragmentTree();
    setenv("INTERCONNECT_PDK_ROOT", root.string().c_str(), 1);

    // Declared surface (4.0) deliberately differs from totalHeight() (5.0):
    // the declaration must win for relative fragments.
    LayerStackup base;
    {
        namespace fs = std::filesystem;
        const fs::path baseYaml =
            fs::temp_directory_path() / "chiplet_lsu_frag_merge" / "base.yaml";
        writeYaml(baseYaml,
                  "attachment_surface_z: 4.0\n"
                  "Pad:\n  index: 134\n  type: 0\n  z: 2.0\n  height: 3.0\n");
        ASSERT_TRUE(base.loadFromBlenderGDS(baseYaml.string()));
    }

    EXPECT_EQ(base.mergeInterconnectFragment("unit_rel"), 1u);
    const LayerElevation* body = base.find(500, 35);
    ASSERT_NE(body, nullptr);
    EXPECT_NEAR(body->z_bottom, 4.0, 1e-9);
    EXPECT_NEAR(body->thickness, 10.0, 1e-9);

    unsetenv("INTERCONNECT_PDK_ROOT");
}

TEST(InterconnectFragmentMerge, RelativeFragmentFallsBackToTotalHeight) {
    const auto root = makeFragmentTree();
    setenv("INTERCONNECT_PDK_ROOT", root.string().c_str(), 1);

    // No attachment_surface_z declared: best-effort totalHeight() = 5.0
    // (warned about), instead of seating the body at z = 0.
    LayerStackup base;
    base.addLayer(134, 0, 2.0, 3.0, "Pad");

    EXPECT_EQ(base.mergeInterconnectFragment("unit_rel"), 1u);
    const LayerElevation* body = base.find(500, 35);
    ASSERT_NE(body, nullptr);
    EXPECT_NEAR(body->z_bottom, 5.0, 1e-9);

    unsetenv("INTERCONNECT_PDK_ROOT");
}

TEST(InterconnectFragmentMerge, LegacyAbsoluteFragmentIgnoresDeclaredSurface) {
    const auto root = makeFragmentTree();
    setenv("INTERCONNECT_PDK_ROOT", root.string().c_str(), 1);

    // Even with a declared surface, a markerless fragment keeps its
    // absolute z (deprecation path).
    LayerStackup base;
    {
        namespace fs = std::filesystem;
        const fs::path baseYaml =
            fs::temp_directory_path() / "chiplet_lsu_frag_merge" / "base_abs.yaml";
        writeYaml(baseYaml,
                  "attachment_surface_z: 4.0\n"
                  "Pad:\n  index: 134\n  type: 0\n  z: 2.0\n  height: 3.0\n");
        ASSERT_TRUE(base.loadFromBlenderGDS(baseYaml.string()));
    }

    EXPECT_EQ(base.mergeInterconnectFragment("unit_abs"), 1u);
    const LayerElevation* body = base.find(500, 35);
    ASSERT_NE(body, nullptr);
    EXPECT_NEAR(body->z_bottom, 7.5, 1e-9);

    unsetenv("INTERCONNECT_PDK_ROOT");
}

TEST(InterconnectFragmentMerge, UnresolvableAdapterMergesNothing) {
    const auto root = makeFragmentTree();
    setenv("INTERCONNECT_PDK_ROOT", root.string().c_str(), 1);

    LayerStackup base;
    base.addLayer(134, 0, 2.0, 3.0, "Pad");

    EXPECT_EQ(base.mergeInterconnectFragment("no_such_adapter_xyz"), 0u);
    EXPECT_EQ(base.mergeInterconnectFragment(""), 0u);
    EXPECT_EQ(base.layerCount(), 1u);

    unsetenv("INTERCONNECT_PDK_ROOT");
}

TEST(InterconnectFragmentMerge, RawLoaderKeepsDeclaredZAndReportsPath) {
    const auto root = makeFragmentTree();
    setenv("INTERCONNECT_PDK_ROOT", root.string().c_str(), 1);

    std::string path;
    LayerStackup frag = LayerStackup::loadInterconnectFragment("unit_rel", &path);
    ASSERT_FALSE(frag.empty());
    EXPECT_NE(path.find("unit_rel.stackup.yaml"), std::string::npos);
    EXPECT_EQ(frag.zReference(), "attachment_surface");
    const LayerElevation* body = frag.find(500, 35);
    ASSERT_NE(body, nullptr);
    // No offset applied by the raw loader.
    EXPECT_NEAR(body->z_bottom, 0.0, 1e-9);

    unsetenv("INTERCONNECT_PDK_ROOT");
}
