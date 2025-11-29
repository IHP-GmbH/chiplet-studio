/**
 * test_klayout_bridge.cpp - Unit tests for KLayoutBridge
 */

#include <gtest/gtest.h>
#include "view2d/KLayoutBridge.h"

namespace chiplet {
namespace {

// =============================================================================
// Basic Tests (work with or without HAVE_KLAYOUT)
// =============================================================================

TEST(KLayoutBridge, Availability)
{
    // klayout_available() should return true if HAVE_KLAYOUT is defined
#ifdef HAVE_KLAYOUT
    EXPECT_TRUE(klayout_available());
#else
    EXPECT_FALSE(klayout_available());
#endif
}

TEST(KLayoutBridge, DefaultConstruction)
{
    KLayoutBridge bridge;

    EXPECT_FALSE(bridge.is_loaded());
    EXPECT_TRUE(bridge.path().empty());
    EXPECT_TRUE(bridge.top_cell().empty());
    EXPECT_TRUE(bridge.cell_names().empty());
    EXPECT_EQ(bridge.cell_count(), 0u);
    EXPECT_EQ(bridge.layer_count(), 0u);
}

TEST(KLayoutBridge, LoadNonexistentFile)
{
    KLayoutBridge bridge;

    bool result = bridge.load_layout("/nonexistent/file.gds");

    EXPECT_FALSE(result);
    EXPECT_FALSE(bridge.is_loaded());
}

TEST(KLayoutBridge, DefaultDbu)
{
    KLayoutBridge bridge;

    // Default DBU should be 0.001 (1nm)
    EXPECT_DOUBLE_EQ(bridge.dbu(), 0.001);
}

TEST(KLayoutBridge, BoundingBoxUnloaded)
{
    KLayoutBridge bridge;

    BoundingBox bbox = bridge.bounding_box();

    EXPECT_DOUBLE_EQ(bbox.x_min, 0.0);
    EXPECT_DOUBLE_EQ(bbox.y_min, 0.0);
    EXPECT_DOUBLE_EQ(bbox.x_max, 0.0);
    EXPECT_DOUBLE_EQ(bbox.y_max, 0.0);
}

TEST(KLayoutBridge, SetTopCellWithoutLayout)
{
    KLayoutBridge bridge;

    bool result = bridge.set_top_cell("SomeCell");

    EXPECT_FALSE(result);
}

TEST(KLayoutBridge, MoveConstruction)
{
    KLayoutBridge bridge1;
    KLayoutBridge bridge2(std::move(bridge1));

    EXPECT_FALSE(bridge2.is_loaded());
}

TEST(KLayoutBridge, MoveAssignment)
{
    KLayoutBridge bridge1;
    KLayoutBridge bridge2;

    bridge2 = std::move(bridge1);

    EXPECT_FALSE(bridge2.is_loaded());
}

// =============================================================================
// Tests that require HAVE_KLAYOUT
// =============================================================================

#ifdef HAVE_KLAYOUT

TEST(KLayoutBridge, LoadValidGds)
{
    // This test requires a valid GDS file in the fixtures
    KLayoutBridge bridge;

    bool result = bridge.load_layout("fixtures/sample.gds");

    if (result) {
        EXPECT_TRUE(bridge.is_loaded());
        EXPECT_EQ(bridge.path(), "fixtures/sample.gds");
        EXPECT_FALSE(bridge.format().empty());
        EXPECT_GT(bridge.cell_count(), 0u);
    } else {
        // File doesn't exist - skip test
        GTEST_SKIP() << "sample.gds fixture not available";
    }
}

TEST(KLayoutBridge, AccessLayout)
{
    KLayoutBridge bridge;

    // Without loading, layout should be null
    EXPECT_EQ(bridge.layout(), nullptr);

    const KLayoutBridge& const_bridge = bridge;
    EXPECT_EQ(const_bridge.layout(), nullptr);
}

#endif // HAVE_KLAYOUT

// =============================================================================
// BoundingBox Tests
// =============================================================================

TEST(BoundingBox, DefaultValues)
{
    BoundingBox bbox;

    EXPECT_DOUBLE_EQ(bbox.x_min, 0.0);
    EXPECT_DOUBLE_EQ(bbox.y_min, 0.0);
    EXPECT_DOUBLE_EQ(bbox.x_max, 0.0);
    EXPECT_DOUBLE_EQ(bbox.y_max, 0.0);
}

TEST(BoundingBox, Width)
{
    BoundingBox bbox;
    bbox.x_min = 10.0;
    bbox.x_max = 50.0;

    EXPECT_DOUBLE_EQ(bbox.width(), 40.0);
}

TEST(BoundingBox, Height)
{
    BoundingBox bbox;
    bbox.y_min = 5.0;
    bbox.y_max = 25.0;

    EXPECT_DOUBLE_EQ(bbox.height(), 20.0);
}

TEST(BoundingBox, WidthNegative)
{
    BoundingBox bbox;
    bbox.x_min = 50.0;
    bbox.x_max = 10.0;  // Inverted

    EXPECT_DOUBLE_EQ(bbox.width(), -40.0);
}

} // namespace
} // namespace chiplet
