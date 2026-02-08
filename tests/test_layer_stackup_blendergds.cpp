/**
 * test_layer_stackup_blendergds.cpp - Tests for BlenderGDS YAML integration
 */

#include <gtest/gtest.h>
#include "core/LayerStackup.h"
#include <fstream>

using namespace chiplet;

// Helper to check if a file exists
static bool fileExists(const std::string& path) {
    std::ifstream f(path);
    return f.good();
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
