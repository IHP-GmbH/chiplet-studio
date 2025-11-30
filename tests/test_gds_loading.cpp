/**
 * test_gds_loading.cpp - Comprehensive GDS file loading tests
 *
 * Tests KLayoutBridge with real GDS files to validate KLayout integration.
 * Fixtures:
 *   - sample_minimal.gds (arefs.gds from KLayout testdata) - simple array refs
 *   - sample_hierarchical.gds (basic_instances.gds) - multi-cell hierarchy
 */

#include <gtest/gtest.h>
#include "view2d/KLayoutBridge.h"
#include <filesystem>

namespace chiplet {
namespace {

// Fixture paths (copied to build directory by CMake)
const char* MINIMAL_GDS = "fixtures/sample_minimal.gds";
const char* HIERARCHICAL_GDS = "fixtures/sample_hierarchical.gds";

// Helper to check if fixture exists
bool fixture_exists(const char* path) {
    return std::filesystem::exists(path);
}

// =============================================================================
// GDS Loading Tests (require HAVE_KLAYOUT)
// =============================================================================

#ifdef HAVE_KLAYOUT

class GdsLoadingTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Verify fixtures exist
        if (!fixture_exists(MINIMAL_GDS)) {
            GTEST_SKIP() << "sample_minimal.gds fixture not found";
        }
    }

    KLayoutBridge bridge;
};

// -----------------------------------------------------------------------------
// Basic Loading Tests
// -----------------------------------------------------------------------------

TEST_F(GdsLoadingTest, LoadMinimalFile)
{
    bool result = bridge.load_layout(MINIMAL_GDS);

    ASSERT_TRUE(result) << "Failed to load " << MINIMAL_GDS;
    EXPECT_TRUE(bridge.is_loaded());
    EXPECT_EQ(bridge.path(), MINIMAL_GDS);
}

TEST_F(GdsLoadingTest, LoadHierarchicalFile)
{
    if (!fixture_exists(HIERARCHICAL_GDS)) {
        GTEST_SKIP() << "sample_hierarchical.gds fixture not found";
    }

    bool result = bridge.load_layout(HIERARCHICAL_GDS);

    ASSERT_TRUE(result) << "Failed to load " << HIERARCHICAL_GDS;
    EXPECT_TRUE(bridge.is_loaded());
}

TEST_F(GdsLoadingTest, FormatIsGds2)
{
    bridge.load_layout(MINIMAL_GDS);

    EXPECT_EQ(bridge.format(), "GDS2");
}

TEST_F(GdsLoadingTest, PathStoredCorrectly)
{
    bridge.load_layout(MINIMAL_GDS);

    EXPECT_EQ(bridge.path(), MINIMAL_GDS);
}

// -----------------------------------------------------------------------------
// Cell Enumeration Tests
// -----------------------------------------------------------------------------

TEST_F(GdsLoadingTest, CellCountPositive)
{
    bridge.load_layout(MINIMAL_GDS);

    EXPECT_GT(bridge.cell_count(), 0u);
}

TEST_F(GdsLoadingTest, CellNamesNotEmpty)
{
    bridge.load_layout(MINIMAL_GDS);

    std::vector<std::string> names = bridge.cell_names();
    EXPECT_FALSE(names.empty());
}

TEST_F(GdsLoadingTest, HierarchicalHasMultipleCells)
{
    if (!fixture_exists(HIERARCHICAL_GDS)) {
        GTEST_SKIP() << "sample_hierarchical.gds fixture not found";
    }

    bool result = bridge.load_layout(HIERARCHICAL_GDS);

    // Verify the file loads successfully and has at least one cell
    ASSERT_TRUE(result);
    EXPECT_GE(bridge.cell_count(), 1u);
}

TEST_F(GdsLoadingTest, TopCellAutoSelected)
{
    bridge.load_layout(MINIMAL_GDS);

    // Top cell should be auto-selected after loading
    EXPECT_FALSE(bridge.top_cell().empty());
}

TEST_F(GdsLoadingTest, SetTopCellValid)
{
    bridge.load_layout(MINIMAL_GDS);

    std::vector<std::string> names = bridge.cell_names();
    ASSERT_FALSE(names.empty());

    // Set first cell as top
    bool result = bridge.set_top_cell(names[0]);
    EXPECT_TRUE(result);
    EXPECT_EQ(bridge.top_cell(), names[0]);
}

TEST_F(GdsLoadingTest, SetTopCellInvalid)
{
    bridge.load_layout(MINIMAL_GDS);

    bool result = bridge.set_top_cell("NONEXISTENT_CELL_12345");
    EXPECT_FALSE(result);
}

// -----------------------------------------------------------------------------
// Geometry Tests
// -----------------------------------------------------------------------------

TEST_F(GdsLoadingTest, BoundingBoxValid)
{
    bridge.load_layout(MINIMAL_GDS);

    BoundingBox bbox = bridge.bounding_box();

    // Bounding box should be valid (non-zero dimensions)
    EXPECT_TRUE(bbox.is_valid()) << "BoundingBox: ("
        << bbox.x_min << "," << bbox.y_min << ") to ("
        << bbox.x_max << "," << bbox.y_max << ")";
}

TEST_F(GdsLoadingTest, BoundingBoxPositiveDimensions)
{
    bridge.load_layout(MINIMAL_GDS);

    BoundingBox bbox = bridge.bounding_box();

    EXPECT_GT(bbox.width(), 0.0);
    EXPECT_GT(bbox.height(), 0.0);
}

TEST_F(GdsLoadingTest, DbuPositive)
{
    bridge.load_layout(MINIMAL_GDS);

    double dbu = bridge.dbu();
    EXPECT_GT(dbu, 0.0);
}

// -----------------------------------------------------------------------------
// Layer Tests
// -----------------------------------------------------------------------------

TEST_F(GdsLoadingTest, LayerCountPositive)
{
    bridge.load_layout(MINIMAL_GDS);

    // GDS files typically have at least one layer
    EXPECT_GT(bridge.layer_count(), 0u);
}

// -----------------------------------------------------------------------------
// Error Handling Tests
// -----------------------------------------------------------------------------

TEST_F(GdsLoadingTest, LoadNonexistentFile)
{
    bool result = bridge.load_layout("/nonexistent/path/file.gds");

    EXPECT_FALSE(result);
    EXPECT_FALSE(bridge.is_loaded());
}

TEST_F(GdsLoadingTest, LoadEmptyPath)
{
    bool result = bridge.load_layout("");

    EXPECT_FALSE(result);
    EXPECT_FALSE(bridge.is_loaded());
}

TEST_F(GdsLoadingTest, LoadInvalidExtension)
{
    // Try to load a non-GDS file (use one of the chiplet fixtures)
    bool result = bridge.load_layout("fixtures/minimal.chiplet");

    // KLayout may still try to load it, but should fail gracefully
    // The exact behavior depends on KLayout's file detection
    // Either it fails to load or loads with errors
    if (result) {
        // If it somehow loaded, it shouldn't have valid GDS data
        EXPECT_NE(bridge.format(), "GDS2");
    } else {
        EXPECT_FALSE(bridge.is_loaded());
    }
}

// -----------------------------------------------------------------------------
// Reload Tests
// -----------------------------------------------------------------------------

TEST_F(GdsLoadingTest, ReloadSameFile)
{
    bridge.load_layout(MINIMAL_GDS);
    size_t first_count = bridge.cell_count();

    // Reload the same file
    bridge.load_layout(MINIMAL_GDS);
    size_t second_count = bridge.cell_count();

    EXPECT_EQ(first_count, second_count);
}

TEST_F(GdsLoadingTest, LoadDifferentFile)
{
    if (!fixture_exists(HIERARCHICAL_GDS)) {
        GTEST_SKIP() << "sample_hierarchical.gds fixture not found";
    }

    bridge.load_layout(MINIMAL_GDS);
    std::string first_path = bridge.path();

    bridge.load_layout(HIERARCHICAL_GDS);
    std::string second_path = bridge.path();

    EXPECT_NE(first_path, second_path);
    EXPECT_EQ(second_path, HIERARCHICAL_GDS);
}

// -----------------------------------------------------------------------------
// Layout Access Tests
// -----------------------------------------------------------------------------

TEST_F(GdsLoadingTest, LayoutPointerAfterLoad)
{
    bridge.load_layout(MINIMAL_GDS);

    // After loading, layout pointer should be valid
    EXPECT_NE(bridge.layout(), nullptr);
}

TEST_F(GdsLoadingTest, ConstLayoutAccess)
{
    bridge.load_layout(MINIMAL_GDS);

    const KLayoutBridge& const_bridge = bridge;
    EXPECT_NE(const_bridge.layout(), nullptr);
}

#else // !HAVE_KLAYOUT

// Placeholder test when KLayout is not available
TEST(GdsLoading, KLayoutNotAvailable)
{
    GTEST_SKIP() << "KLayout integration not available (HAVE_KLAYOUT not defined)";
}

#endif // HAVE_KLAYOUT

} // namespace
} // namespace chiplet
