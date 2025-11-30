/**
 * test_component_style.cpp - Unit tests for ComponentStyle
 */

#include <gtest/gtest.h>
#include "ui/ComponentStyle.h"

namespace chiplet {
namespace {

// =============================================================================
// Utility Function Tests
// =============================================================================

TEST(ComponentStyle, ParseHexColorRRGGBB)
{
    ComponentColor c = parse_component_color("#6495ED");
    EXPECT_EQ(c.r, 100);
    EXPECT_EQ(c.g, 149);
    EXPECT_EQ(c.b, 237);
    EXPECT_EQ(c.a, 255);
}

TEST(ComponentStyle, ParseHexColorRGB)
{
    ComponentColor c = parse_component_color("#FFF");
    EXPECT_EQ(c.r, 255);
    EXPECT_EQ(c.g, 255);
    EXPECT_EQ(c.b, 255);
}

TEST(ComponentStyle, ParseHexColorEmpty)
{
    ComponentColor c = parse_component_color("");
    EXPECT_EQ(c.r, 0);
    EXPECT_EQ(c.g, 0);
    EXPECT_EQ(c.b, 0);
}

TEST(ComponentStyle, ParseHexColorInvalid)
{
    ComponentColor c = parse_component_color("not-a-color");
    EXPECT_EQ(c.r, 0);
    EXPECT_EQ(c.g, 0);
    EXPECT_EQ(c.b, 0);
}

TEST(ComponentStyle, ParseIconShapeKnown)
{
    EXPECT_EQ(parse_icon_shape("square"), IconShape::Square);
    EXPECT_EQ(parse_icon_shape("circle"), IconShape::Circle);
    EXPECT_EQ(parse_icon_shape("diamond"), IconShape::Diamond);
    EXPECT_EQ(parse_icon_shape("grid"), IconShape::Grid);
    EXPECT_EQ(parse_icon_shape("layers"), IconShape::Layers);
    EXPECT_EQ(parse_icon_shape("rectangle"), IconShape::Rectangle);
}

TEST(ComponentStyle, ParseIconShapeUnknown)
{
    EXPECT_EQ(parse_icon_shape("unknown"), IconShape::Square);
    EXPECT_EQ(parse_icon_shape(""), IconShape::Square);
}

TEST(ComponentStyle, IconShapeToString)
{
    EXPECT_EQ(icon_shape_to_string(IconShape::Square), "square");
    EXPECT_EQ(icon_shape_to_string(IconShape::Circle), "circle");
    EXPECT_EQ(icon_shape_to_string(IconShape::Grid), "grid");
}

// =============================================================================
// Default Style Tests
// =============================================================================

TEST(ComponentStyleFile, DefaultConstruction)
{
    ComponentStyleFile styles;
    EXPECT_TRUE(styles.path().empty());
    EXPECT_TRUE(styles.error().empty());
}

TEST(ComponentStyleFile, DefaultStyleDie)
{
    ComponentStyle style = ComponentStyleFile::default_style(ComponentType::Die);
    EXPECT_EQ(style.type, ComponentType::Die);
    EXPECT_EQ(style.fill_color.r, 100);   // Cornflower blue
    EXPECT_EQ(style.fill_color.g, 149);
    EXPECT_EQ(style.fill_color.b, 237);
    EXPECT_EQ(style.icon, IconShape::Square);
}

TEST(ComponentStyleFile, DefaultStyleInterposer)
{
    ComponentStyle style = ComponentStyleFile::default_style(ComponentType::Interposer);
    EXPECT_EQ(style.type, ComponentType::Interposer);
    EXPECT_EQ(style.fill_color.r, 60);    // Medium sea green
    EXPECT_EQ(style.fill_color.g, 179);
    EXPECT_EQ(style.fill_color.b, 113);
    EXPECT_EQ(style.icon, IconShape::Layers);
}

TEST(ComponentStyleFile, DefaultStyleSubstrate)
{
    ComponentStyle style = ComponentStyleFile::default_style(ComponentType::Substrate);
    EXPECT_EQ(style.type, ComponentType::Substrate);
    EXPECT_EQ(style.fill_color.r, 139);   // Saddle brown
    EXPECT_EQ(style.fill_color.g, 90);
    EXPECT_EQ(style.fill_color.b, 43);
    EXPECT_EQ(style.icon, IconShape::Rectangle);
}

TEST(ComponentStyleFile, DefaultStyleDieArray)
{
    ComponentStyle style = ComponentStyleFile::default_style(ComponentType::DieArray);
    EXPECT_EQ(style.type, ComponentType::DieArray);
    EXPECT_EQ(style.icon, IconShape::Grid);
}

// =============================================================================
// Style Lookup Tests
// =============================================================================

TEST(ComponentStyleFile, StyleForType)
{
    ComponentStyleFile styles;

    const ComponentStyle* die = styles.style_for_type(ComponentType::Die);
    ASSERT_NE(die, nullptr);
    EXPECT_EQ(die->type, ComponentType::Die);

    const ComponentStyle* interposer = styles.style_for_type(ComponentType::Interposer);
    ASSERT_NE(interposer, nullptr);
    EXPECT_EQ(interposer->type, ComponentType::Interposer);
}

TEST(ComponentStyleFile, StyleForComponent)
{
    ComponentStyleFile styles;

    Component die("test_die", ComponentType::Die);
    const ComponentStyle* style = styles.style_for(die);

    ASSERT_NE(style, nullptr);
    EXPECT_EQ(style->type, ComponentType::Die);
}

TEST(ComponentStyleFile, StyleForIdNotFound)
{
    ComponentStyleFile styles;

    const ComponentStyle* style = styles.style_for_id("nonexistent");
    EXPECT_EQ(style, nullptr);
}

// =============================================================================
// Load Defaults Tests
// =============================================================================

TEST(ComponentStyleFile, LoadDefaultsInitializesAllTypes)
{
    ComponentStyleFile styles;

    // All four types should have styles
    EXPECT_NE(styles.style_for_type(ComponentType::Die), nullptr);
    EXPECT_NE(styles.style_for_type(ComponentType::DieArray), nullptr);
    EXPECT_NE(styles.style_for_type(ComponentType::Interposer), nullptr);
    EXPECT_NE(styles.style_for_type(ComponentType::Substrate), nullptr);
}

// =============================================================================
// ComponentColor Tests
// =============================================================================

TEST(ComponentColor, DefaultConstruction)
{
    ComponentColor c;
    EXPECT_EQ(c.r, 0);
    EXPECT_EQ(c.g, 0);
    EXPECT_EQ(c.b, 0);
    EXPECT_EQ(c.a, 255);
}

TEST(ComponentColor, Equality)
{
    ComponentColor a(100, 150, 200);
    ComponentColor b(100, 150, 200);
    ComponentColor c(100, 150, 201);

    EXPECT_EQ(a, b);
    EXPECT_FALSE(a == c);
}

} // namespace
} // namespace chiplet
