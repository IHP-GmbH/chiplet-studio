/**
 * test_layer_properties.cpp - Unit tests for LayerProperties
 */

#include <gtest/gtest.h>
#include "view2d/LayerProperties.h"
#include <filesystem>

namespace chiplet {
namespace {

// Fixture path (copied to build directory by CMake)
const char* SG13G2_LYP = "fixtures/sg13g2.lyp";

// Helper to check if fixture exists
bool fixture_exists(const char* path) {
    return std::filesystem::exists(path);
}

// =============================================================================
// Utility Function Tests
// =============================================================================

TEST(LayerPropertiesUtils, ParseHexColorFull)
{
    LayerColor color = parse_hex_color("#ff00ff");
    EXPECT_EQ(color.r, 255);
    EXPECT_EQ(color.g, 0);
    EXPECT_EQ(color.b, 255);
    EXPECT_EQ(color.a, 255);
}

TEST(LayerPropertiesUtils, ParseHexColorShort)
{
    LayerColor color = parse_hex_color("#f0f");
    EXPECT_EQ(color.r, 255);
    EXPECT_EQ(color.g, 0);
    EXPECT_EQ(color.b, 255);
}

TEST(LayerPropertiesUtils, ParseHexColorWhite)
{
    LayerColor color = parse_hex_color("#ffffff");
    EXPECT_EQ(color.r, 255);
    EXPECT_EQ(color.g, 255);
    EXPECT_EQ(color.b, 255);
}

TEST(LayerPropertiesUtils, ParseHexColorBlack)
{
    LayerColor color = parse_hex_color("#000000");
    EXPECT_EQ(color.r, 0);
    EXPECT_EQ(color.g, 0);
    EXPECT_EQ(color.b, 0);
}

TEST(LayerPropertiesUtils, ParseHexColorEmpty)
{
    LayerColor color = parse_hex_color("");
    EXPECT_EQ(color.r, 0);
    EXPECT_EQ(color.g, 0);
    EXPECT_EQ(color.b, 0);
}

TEST(LayerPropertiesUtils, ParseHexColorNoHash)
{
    LayerColor color = parse_hex_color("ff00ff");
    EXPECT_EQ(color.r, 0);  // Should fail without #
    EXPECT_EQ(color.g, 0);
    EXPECT_EQ(color.b, 0);
}

TEST(LayerPropertiesUtils, ParseLayerSourceBasic)
{
    LayerKey key = parse_layer_source("40/0");
    EXPECT_EQ(key.layer, 40);
    EXPECT_EQ(key.datatype, 0);
}

TEST(LayerPropertiesUtils, ParseLayerSourceWithDatatype)
{
    LayerKey key = parse_layer_source("1/25");
    EXPECT_EQ(key.layer, 1);
    EXPECT_EQ(key.datatype, 25);
}

TEST(LayerPropertiesUtils, ParseLayerSourceInvalid)
{
    LayerKey key = parse_layer_source("invalid");
    EXPECT_EQ(key.layer, 0);
    EXPECT_EQ(key.datatype, 0);
}

TEST(LayerPropertiesUtils, ParseLayerSourceEmpty)
{
    LayerKey key = parse_layer_source("");
    EXPECT_EQ(key.layer, 0);
    EXPECT_EQ(key.datatype, 0);
}

// =============================================================================
// LayerKey Tests
// =============================================================================

TEST(LayerKey, Equality)
{
    LayerKey a(1, 0);
    LayerKey b(1, 0);
    LayerKey c(1, 1);
    LayerKey d(2, 0);

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    EXPECT_NE(a, d);
}

TEST(LayerKey, LessThan)
{
    LayerKey a(1, 0);
    LayerKey b(2, 0);
    LayerKey c(1, 1);

    EXPECT_TRUE(a < b);
    EXPECT_TRUE(a < c);
    EXPECT_FALSE(b < a);
}

// =============================================================================
// LayerColor Tests
// =============================================================================

TEST(LayerColor, DefaultValues)
{
    LayerColor color;
    EXPECT_EQ(color.r, 0);
    EXPECT_EQ(color.g, 0);
    EXPECT_EQ(color.b, 0);
    EXPECT_EQ(color.a, 255);
}

TEST(LayerColor, Constructor)
{
    LayerColor color(128, 64, 32, 200);
    EXPECT_EQ(color.r, 128);
    EXPECT_EQ(color.g, 64);
    EXPECT_EQ(color.b, 32);
    EXPECT_EQ(color.a, 200);
}

TEST(LayerColor, Equality)
{
    LayerColor a(255, 0, 0);
    LayerColor b(255, 0, 0);
    LayerColor c(0, 255, 0);

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

// =============================================================================
// LayerStyle Tests
// =============================================================================

TEST(LayerStyle, DefaultValues)
{
    LayerStyle style;
    EXPECT_EQ(style.key.layer, 0);
    EXPECT_EQ(style.key.datatype, 0);
    EXPECT_TRUE(style.name.empty());
    EXPECT_TRUE(style.visible);
    EXPECT_FALSE(style.transparent);
    EXPECT_TRUE(style.valid);
    EXPECT_EQ(style.width, 1);
}

TEST(LayerStyle, IsVisible)
{
    LayerStyle style;

    style.visible = true;
    style.valid = true;
    EXPECT_TRUE(style.is_visible());

    style.visible = false;
    EXPECT_FALSE(style.is_visible());

    style.visible = true;
    style.valid = false;
    EXPECT_FALSE(style.is_visible());
}

// =============================================================================
// LayerPropertiesFile Tests
// =============================================================================

class LayerPropertiesFileTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!fixture_exists(SG13G2_LYP)) {
            GTEST_SKIP() << "sg13g2.lyp fixture not found";
        }
    }
};

TEST_F(LayerPropertiesFileTest, LoadValidFile)
{
    LayerPropertiesFile lyp;
    bool result = lyp.load(SG13G2_LYP);

    ASSERT_TRUE(result) << "Error: " << lyp.error();
    EXPECT_GT(lyp.layer_count(), 0u);
    EXPECT_EQ(lyp.path(), SG13G2_LYP);
}

TEST_F(LayerPropertiesFileTest, LayerCountReasonable)
{
    LayerPropertiesFile lyp;
    lyp.load(SG13G2_LYP);

    // IHP SG13G2 has many layers (100+)
    EXPECT_GT(lyp.layer_count(), 50u);
}

TEST_F(LayerPropertiesFileTest, FindSubstrateLayer)
{
    LayerPropertiesFile lyp;
    lyp.load(SG13G2_LYP);

    // Substrate.drawing is layer 40/0
    const LayerStyle* style = lyp.find(40, 0);
    ASSERT_NE(style, nullptr);
    EXPECT_EQ(style->key.layer, 40);
    EXPECT_EQ(style->key.datatype, 0);
    EXPECT_EQ(style->name, "Substrate.drawing");
}

TEST_F(LayerPropertiesFileTest, FindActivLayer)
{
    LayerPropertiesFile lyp;
    lyp.load(SG13G2_LYP);

    // Activ.drawing is layer 1/0
    const LayerStyle* style = lyp.find(1, 0);
    ASSERT_NE(style, nullptr);
    EXPECT_EQ(style->name, "Activ.drawing");
}

TEST_F(LayerPropertiesFileTest, FindNonexistentLayer)
{
    LayerPropertiesFile lyp;
    lyp.load(SG13G2_LYP);

    const LayerStyle* style = lyp.find(9999, 9999);
    EXPECT_EQ(style, nullptr);
}

TEST_F(LayerPropertiesFileTest, ColorParsedCorrectly)
{
    LayerPropertiesFile lyp;
    lyp.load(SG13G2_LYP);

    // Activ.drawing should be green (#00ff00)
    const LayerStyle* style = lyp.find(1, 0);
    ASSERT_NE(style, nullptr);
    EXPECT_EQ(style->fill_color.r, 0);
    EXPECT_EQ(style->fill_color.g, 255);
    EXPECT_EQ(style->fill_color.b, 0);
}

TEST_F(LayerPropertiesFileTest, VisibilityParsed)
{
    LayerPropertiesFile lyp;
    lyp.load(SG13G2_LYP);

    // Most layers should be visible
    const LayerStyle* style = lyp.find(1, 0);
    ASSERT_NE(style, nullptr);
    EXPECT_TRUE(style->visible);
}

TEST_F(LayerPropertiesFileTest, IterateLayers)
{
    LayerPropertiesFile lyp;
    lyp.load(SG13G2_LYP);

    size_t count = 0;
    for (const auto& layer : lyp.layers()) {
        EXPECT_FALSE(layer.name.empty());
        count++;
    }
    EXPECT_EQ(count, lyp.layer_count());
}

// =============================================================================
// Error Handling Tests
// =============================================================================

TEST(LayerPropertiesFile, LoadNonexistentFile)
{
    LayerPropertiesFile lyp;
    bool result = lyp.load("/nonexistent/file.lyp");

    EXPECT_FALSE(result);
    EXPECT_EQ(lyp.layer_count(), 0u);
    EXPECT_FALSE(lyp.error().empty());
}

TEST(LayerPropertiesFile, LoadEmptyPath)
{
    LayerPropertiesFile lyp;
    bool result = lyp.load("");

    EXPECT_FALSE(result);
    EXPECT_EQ(lyp.layer_count(), 0u);
}

TEST(LayerPropertiesFile, DefaultState)
{
    LayerPropertiesFile lyp;

    EXPECT_EQ(lyp.layer_count(), 0u);
    EXPECT_TRUE(lyp.path().empty());
    EXPECT_TRUE(lyp.layers().empty());
    EXPECT_EQ(lyp.find(0, 0), nullptr);
}

} // namespace
} // namespace chiplet
