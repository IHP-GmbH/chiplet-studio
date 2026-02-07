/**
 * test_stackup_rendering.cpp - Tests for GDSProcess integration in Technology class
 *
 * Validates that techfiles are properly loaded and converted to LayerStackup
 * with correct unit conversion (nm to um).
 */

#include <gtest/gtest.h>
#include "core/Technology.h"
#include "core/LayerStackup.h"
#include "process_cfg.h"

class StackupRenderingTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(StackupRenderingTest, TechnologyLoadsTechfile) {
    chiplet::Technology tech("sg13g2");

    std::string techfile = std::string(FIXTURES_DIR) + "/../../pdks/ihp-sg13g2/techfile/sg13g2.txt";
    ASSERT_TRUE(tech.load_process_def(techfile)) << "Should load techfile";
    EXPECT_TRUE(tech.has_process_def());
    EXPECT_EQ(tech.process_def_path(), techfile);
}

TEST_F(StackupRenderingTest, TechfileHeightsInMicrons) {
    chiplet::Technology tech("sg13g2");

    std::string techfile = std::string(FIXTURES_DIR) + "/../../pdks/ihp-sg13g2/techfile/sg13g2.txt";
    ASSERT_TRUE(tech.load_process_def(techfile));

    // Metal4 in sg13g2.txt: Height: 3880 (nm) -> 3.88 um
    double z = tech.get_layer_z_um(50, 0);
    EXPECT_NEAR(z, 3.88, 0.01) << "Metal4 Z should be ~3.88um";

    // Metal4 thickness: 450nm -> 0.45um
    double thick = tech.get_layer_thickness_um(50, 0);
    EXPECT_NEAR(thick, 0.45, 0.01) << "Metal4 thickness should be ~0.45um";
}

TEST_F(StackupRenderingTest, CreateStackupFromTechfile) {
    chiplet::Technology tech("sg13g2");

    std::string techfile = std::string(FIXTURES_DIR) + "/../../pdks/ihp-sg13g2/techfile/sg13g2.txt";
    ASSERT_TRUE(tech.load_process_def(techfile));

    chiplet::LayerStackup stackup = tech.createStackup();

    // sg13g2.txt has 21 layers
    EXPECT_GT(stackup.layerCount(), 10) << "Should have many layers";

    // Verify Metal4 is in stackup with correct Z
    const chiplet::LayerElevation* metal4 = stackup.find(50, 0);
    ASSERT_NE(metal4, nullptr) << "Metal4 should be in stackup";
    EXPECT_NEAR(metal4->z_bottom, 3.88, 0.01);
    EXPECT_NEAR(metal4->thickness, 0.45, 0.01);
}

TEST_F(StackupRenderingTest, InterposerTechfile) {
    chiplet::Technology tech("interposer");

    std::string techfile = std::string(FIXTURES_DIR) + "/../../pdks/interposer/techfile/interposer.txt";
    ASSERT_TRUE(tech.load_process_def(techfile));

    chiplet::LayerStackup stackup = tech.createStackup();
    EXPECT_EQ(stackup.layerCount(), 7) << "Interposer has 7 layers";

    // TopMetal2 (layer 134): Height 11160nm -> 11.16um
    const chiplet::LayerElevation* topMetal2 = stackup.find(134, 0);
    ASSERT_NE(topMetal2, nullptr);
    EXPECT_NEAR(topMetal2->z_bottom, 11.16, 0.01);
}

TEST_F(StackupRenderingTest, LayerColorFromTechfile) {
    chiplet::Technology tech("sg13g2");

    std::string techfile = std::string(FIXTURES_DIR) + "/../../pdks/ihp-sg13g2/techfile/sg13g2.txt";
    ASSERT_TRUE(tech.load_process_def(techfile));

    // Metal4: Red: 0.58, Green: 0.91, Blue: 0.22
    auto color = tech.get_layer_color(50, 0);
    EXPECT_NEAR(color[0], 0.58, 0.01);  // Red
    EXPECT_NEAR(color[1], 0.91, 0.01);  // Green
    EXPECT_NEAR(color[2], 0.22, 0.01);  // Blue
}

TEST_F(StackupRenderingTest, UnknownLayerReturnsDefaults) {
    chiplet::Technology tech("test");

    // Without loading techfile
    EXPECT_EQ(tech.get_layer_z_um(999, 0), 0.0);
    EXPECT_EQ(tech.get_layer_thickness_um(999, 0), 1.0);

    auto color = tech.get_layer_color(999, 0);
    EXPECT_EQ(color[0], 0.5f);
    EXPECT_EQ(color[1], 0.5f);
    EXPECT_EQ(color[2], 0.5f);
}

TEST_F(StackupRenderingTest, InvalidTechfilePathReturnsFalse) {
    chiplet::Technology tech("test");

    EXPECT_FALSE(tech.load_process_def(""));
    EXPECT_FALSE(tech.load_process_def("/nonexistent/path/techfile.txt"));
    EXPECT_FALSE(tech.has_process_def());
}
