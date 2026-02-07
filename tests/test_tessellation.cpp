/**
 * test_tessellation.cpp - Unit tests for GDS3D tessellation integration
 *
 * Tests the LayerMeshBuilder tessellation engine, which uses GDS3D's robust
 * GDSPolygon::Tesselate() with automatic fallback to legacy ear-clipping.
 */

#include <gtest/gtest.h>
#include "view3d/LayerMeshBuilder.h"
#include "view3d/GDSLayerExtractor.h"

using namespace chiplet;

// ============================================================================
// Tessellation Tests
// ============================================================================

class TessellationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // GDS3D is stable - use as the default tessellation engine
        LayerMeshBuilder::setTessellatorMode(LayerMeshBuilder::TessellatorMode::GDS3D);
    }

    void TearDown() override {
        // Reset to GDS3D mode
        LayerMeshBuilder::setTessellatorMode(LayerMeshBuilder::TessellatorMode::GDS3D);
    }
};

// ============================================================================
// Basic Polygon Tests
// ============================================================================

TEST_F(TessellationTest, TrianglePolygon) {
    SimplePolygon poly;
    poly.points = {{0, 0}, {10, 0}, {5, 10}};

    auto triangles = LayerMeshBuilder::triangulatePolygon(poly);

    // Should produce 1 triangle (3 indices)
    EXPECT_EQ(triangles.size(), 3u);
}

TEST_F(TessellationTest, QuadPolygon) {
    // Counter-clockwise quad
    SimplePolygon poly;
    poly.points = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};

    auto triangles = LayerMeshBuilder::triangulatePolygon(poly);

    // Should produce 2 triangles (6 indices)
    EXPECT_EQ(triangles.size(), 6u);

    // Verify all indices are in valid range [0, 3]
    for (int idx : triangles) {
        EXPECT_GE(idx, 0);
        EXPECT_LT(idx, 4);
    }
}

TEST_F(TessellationTest, QuadPolygonClockwise) {
    // Clockwise quad (negative area)
    SimplePolygon poly;
    poly.points = {{0, 0}, {0, 10}, {10, 10}, {10, 0}};

    auto triangles = LayerMeshBuilder::triangulatePolygon(poly);

    // Should produce 2 triangles (6 indices) even for clockwise
    EXPECT_EQ(triangles.size(), 6u);
}

TEST_F(TessellationTest, LShapedPolygon) {
    // L-shaped polygon (6 vertices, concave)
    //
    //  +---+
    //  |   |
    //  +---+----+
    //      |    |
    //      +----+
    //
    SimplePolygon poly;
    poly.points = {{0, 0}, {60, 0}, {60, 40}, {20, 40}, {20, 100}, {0, 100}};

    auto triangles = LayerMeshBuilder::triangulatePolygon(poly);

    // Should produce 4 triangles (12 indices)
    // (6 vertices - 2 = 4 triangles)
    EXPECT_EQ(triangles.size(), 12u);

    // Verify all indices are in valid range [0, 5]
    for (int idx : triangles) {
        EXPECT_GE(idx, 0);
        EXPECT_LT(idx, 6);
    }
}

TEST_F(TessellationTest, PentagonPolygon) {
    // Regular pentagon-ish shape (5 vertices)
    SimplePolygon poly;
    poly.points = {{5, 0}, {10, 4}, {8, 10}, {2, 10}, {0, 4}};

    auto triangles = LayerMeshBuilder::triangulatePolygon(poly);

    // Should produce 3 triangles (9 indices)
    // (5 vertices - 2 = 3 triangles)
    EXPECT_EQ(triangles.size(), 9u);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(TessellationTest, DegeneratePolygonTwoPoints) {
    // Degenerate polygon (2 points - not a polygon)
    SimplePolygon poly;
    poly.points = {{0, 0}, {10, 0}};

    auto triangles = LayerMeshBuilder::triangulatePolygon(poly);

    // Should return empty (no crash)
    EXPECT_TRUE(triangles.empty());
}

TEST_F(TessellationTest, DegeneratePolygonOnePoint) {
    // Single point
    SimplePolygon poly;
    poly.points = {{5, 5}};

    auto triangles = LayerMeshBuilder::triangulatePolygon(poly);

    EXPECT_TRUE(triangles.empty());
}

TEST_F(TessellationTest, EmptyPolygon) {
    SimplePolygon poly;

    auto triangles = LayerMeshBuilder::triangulatePolygon(poly);

    EXPECT_TRUE(triangles.empty());
}

TEST_F(TessellationTest, CollinearPoints) {
    // All points on a line (degenerate)
    SimplePolygon poly;
    poly.points = {{0, 0}, {5, 0}, {10, 0}};

    auto triangles = LayerMeshBuilder::triangulatePolygon(poly);

    // Should handle gracefully (may return empty or degenerate triangles)
    // Either is acceptable as long as no crash
    SUCCEED();
}

// ============================================================================
// Tessellator Mode Tests
// ============================================================================

TEST_F(TessellationTest, LegacyModeWorks) {
    LayerMeshBuilder::setTessellatorMode(LayerMeshBuilder::TessellatorMode::Legacy);

    SimplePolygon poly;
    poly.points = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};

    auto triangles = LayerMeshBuilder::triangulatePolygon(poly);

    // Should produce 2 triangles (6 indices) using legacy algorithm
    EXPECT_EQ(triangles.size(), 6u);
}

TEST_F(TessellationTest, LegacyModeTriangle) {
    LayerMeshBuilder::setTessellatorMode(LayerMeshBuilder::TessellatorMode::Legacy);

    SimplePolygon poly;
    poly.points = {{0, 0}, {10, 0}, {5, 10}};

    auto triangles = LayerMeshBuilder::triangulatePolygon(poly);

    EXPECT_EQ(triangles.size(), 3u);
}

TEST_F(TessellationTest, LegacyModeLShape) {
    LayerMeshBuilder::setTessellatorMode(LayerMeshBuilder::TessellatorMode::Legacy);

    SimplePolygon poly;
    poly.points = {{0, 0}, {60, 0}, {60, 40}, {20, 40}, {20, 100}, {0, 100}};

    auto triangles = LayerMeshBuilder::triangulatePolygon(poly);

    // Should produce 4 triangles (12 indices)
    EXPECT_EQ(triangles.size(), 12u);
}

TEST_F(TessellationTest, ModeGetterSetter) {
    // Test after SetUp (which sets to GDS3D - the stable default)
    EXPECT_EQ(LayerMeshBuilder::tessellatorMode(), LayerMeshBuilder::TessellatorMode::GDS3D);

    // Set to Legacy
    LayerMeshBuilder::setTessellatorMode(LayerMeshBuilder::TessellatorMode::Legacy);
    EXPECT_EQ(LayerMeshBuilder::tessellatorMode(), LayerMeshBuilder::TessellatorMode::Legacy);

    // Set back to GDS3D
    LayerMeshBuilder::setTessellatorMode(LayerMeshBuilder::TessellatorMode::GDS3D);
    EXPECT_EQ(LayerMeshBuilder::tessellatorMode(), LayerMeshBuilder::TessellatorMode::GDS3D);
}

// ============================================================================
// Consistency Tests (GDS3D vs Legacy should produce same triangle count)
// ============================================================================

TEST_F(TessellationTest, GDS3DAndLegacyConsistentTriangleCount) {
    SimplePolygon poly;
    poly.points = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};

    // GDS3D mode
    LayerMeshBuilder::setTessellatorMode(LayerMeshBuilder::TessellatorMode::GDS3D);
    auto gds3dTriangles = LayerMeshBuilder::triangulatePolygon(poly);

    // Legacy mode
    LayerMeshBuilder::setTessellatorMode(LayerMeshBuilder::TessellatorMode::Legacy);
    auto legacyTriangles = LayerMeshBuilder::triangulatePolygon(poly);

    // Both should produce same number of triangles
    EXPECT_EQ(gds3dTriangles.size(), legacyTriangles.size());
}

TEST_F(TessellationTest, LegacyFunctionDirectly) {
    // Test the legacy function directly
    SimplePolygon poly;
    poly.points = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};

    auto triangles = LayerMeshBuilder::triangulatePolygonLegacy(poly);

    EXPECT_EQ(triangles.size(), 6u);
}

// ============================================================================
// Large Polygon Test
// ============================================================================

TEST_F(TessellationTest, LargePolygon) {
    // Polygon with many vertices (simulating complex GDS shape)
    SimplePolygon poly;

    // Create a star-like shape with 20 points
    const int N = 20;
    const double outerR = 100.0;
    const double innerR = 50.0;

    for (int i = 0; i < N; ++i) {
        double angle = 2.0 * M_PI * i / N;
        double r = (i % 2 == 0) ? outerR : innerR;
        poly.points.push_back({r * cos(angle), r * sin(angle)});
    }

    auto triangles = LayerMeshBuilder::triangulatePolygon(poly);

    // Should produce N-2 triangles
    EXPECT_EQ(triangles.size(), static_cast<size_t>((N - 2) * 3));
}
