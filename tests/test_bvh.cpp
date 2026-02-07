/**
 * test_bvh.cpp - Unit tests for BVH spatial acceleration structure
 */

#include <gtest/gtest.h>
#include "math/BVH.h"
#include "math/Maths.h"
#include <vector>
#include <cmath>

using namespace chiplet;

// Helper to create a box at a specific position
AA_BOUNDING_BOX makeBox(float x, float y, float z, float size = 1.0f) {
    AA_BOUNDING_BOX box;
    VECTOR3D mins(x - size/2, y - size/2, z - size/2);
    VECTOR3D maxes(x + size/2, y + size/2, z + size/2);
    box.SetFromMinsMaxes(mins, maxes);
    return box;
}

// ============================================================================
// AA_BOUNDING_BOX Extension Tests
// ============================================================================

class AABoundingBoxTest : public ::testing::Test {
protected:
    AA_BOUNDING_BOX box;

    void SetUp() override {
        VECTOR3D mins(0, 0, 0);
        VECTOR3D maxes(10, 20, 30);
        box.SetFromMinsMaxes(mins, maxes);
    }
};

TEST_F(AABoundingBoxTest, Center) {
    VECTOR3D center = box.center();
    EXPECT_FLOAT_EQ(center.x, 5.0f);
    EXPECT_FLOAT_EQ(center.y, 10.0f);
    EXPECT_FLOAT_EQ(center.z, 15.0f);
}

TEST_F(AABoundingBoxTest, Extent) {
    VECTOR3D ext = box.extent();
    EXPECT_FLOAT_EQ(ext.x, 5.0f);
    EXPECT_FLOAT_EQ(ext.y, 10.0f);
    EXPECT_FLOAT_EQ(ext.z, 15.0f);
}

TEST_F(AABoundingBoxTest, SurfaceArea) {
    // SA = 2 * (10*20 + 20*30 + 30*10) = 2 * (200 + 600 + 300) = 2200
    float sa = box.surfaceArea();
    EXPECT_FLOAT_EQ(sa, 2200.0f);
}

TEST_F(AABoundingBoxTest, LongestAxis) {
    // Z has extent 30, which is longest
    int axis = box.longestAxis();
    EXPECT_EQ(axis, 2);  // Z axis
}

TEST_F(AABoundingBoxTest, LongestAxisXDominant) {
    VECTOR3D mins(0, 0, 0);
    VECTOR3D maxes(100, 10, 10);
    box.SetFromMinsMaxes(mins, maxes);
    EXPECT_EQ(box.longestAxis(), 0);  // X axis
}

TEST_F(AABoundingBoxTest, LongestAxisYDominant) {
    VECTOR3D mins(0, 0, 0);
    VECTOR3D maxes(10, 100, 10);
    box.SetFromMinsMaxes(mins, maxes);
    EXPECT_EQ(box.longestAxis(), 1);  // Y axis
}

TEST_F(AABoundingBoxTest, RayIntersectHit) {
    // Ray from outside, pointing at box center
    VECTOR3D origin(-5, 10, 15);
    VECTOR3D dir(1, 0, 0);  // +X direction

    float tMin, tMax;
    bool hit = box.rayIntersect(origin, dir, tMin, tMax);

    EXPECT_TRUE(hit);
    EXPECT_FLOAT_EQ(tMin, 5.0f);   // Entry at x=0
    EXPECT_FLOAT_EQ(tMax, 15.0f);  // Exit at x=10
}

TEST_F(AABoundingBoxTest, RayIntersectMiss) {
    // Ray parallel to box, outside
    VECTOR3D origin(-5, 25, 15);  // Above the box
    VECTOR3D dir(1, 0, 0);

    float tMin, tMax;
    bool hit = box.rayIntersect(origin, dir, tMin, tMax);

    EXPECT_FALSE(hit);
}

TEST_F(AABoundingBoxTest, RayIntersectFromInside) {
    // Ray from inside the box
    VECTOR3D origin(5, 10, 15);
    VECTOR3D dir(1, 0, 0);

    float tMin, tMax;
    bool hit = box.rayIntersect(origin, dir, tMin, tMax);

    EXPECT_TRUE(hit);
    EXPECT_FLOAT_EQ(tMin, 0.0f);   // Already inside
    EXPECT_FLOAT_EQ(tMax, 5.0f);   // Exit at x=10
}

TEST_F(AABoundingBoxTest, RayIntersectDiagonal) {
    // Diagonal ray
    VECTOR3D origin(-1, -1, -1);
    VECTOR3D dir(1, 1, 1);
    dir.Normalize();

    float tMin, tMax;
    bool hit = box.rayIntersect(origin, dir, tMin, tMax);

    EXPECT_TRUE(hit);
    EXPECT_GT(tMin, 0.0f);
}

TEST_F(AABoundingBoxTest, Intersects) {
    AA_BOUNDING_BOX other;

    // Overlapping box
    VECTOR3D mins1(5, 5, 5);
    VECTOR3D maxes1(15, 15, 15);
    other.SetFromMinsMaxes(mins1, maxes1);
    EXPECT_TRUE(box.intersects(other));

    // Non-overlapping box
    VECTOR3D mins2(100, 100, 100);
    VECTOR3D maxes2(110, 110, 110);
    other.SetFromMinsMaxes(mins2, maxes2);
    EXPECT_FALSE(box.intersects(other));

    // Touching box (edge case)
    VECTOR3D mins3(10, 0, 0);
    VECTOR3D maxes3(20, 20, 30);
    other.SetFromMinsMaxes(mins3, maxes3);
    EXPECT_TRUE(box.intersects(other));  // Touching counts as intersection
}

// ============================================================================
// BVH Tests
// ============================================================================

class BVHTest : public ::testing::Test {
protected:
    BVH bvh;
};

TEST_F(BVHTest, EmptyBVH) {
    EXPECT_TRUE(bvh.empty());
    EXPECT_EQ(bvh.nodeCount(), 0);
    EXPECT_EQ(bvh.maxDepth(), 0);
}

TEST_F(BVHTest, BuildEmpty) {
    std::vector<AA_BOUNDING_BOX> boxes;
    std::vector<int> indices;

    bvh.build(boxes, indices);

    EXPECT_TRUE(bvh.empty());
}

TEST_F(BVHTest, BuildSingle) {
    std::vector<AA_BOUNDING_BOX> boxes = { makeBox(0, 0, 0) };
    std::vector<int> indices = { 0 };

    bvh.build(boxes, indices);

    EXPECT_FALSE(bvh.empty());
    EXPECT_EQ(bvh.nodeCount(), 1);
    EXPECT_EQ(bvh.leafCount(), 1);
    EXPECT_EQ(bvh.maxDepth(), 1);
}

TEST_F(BVHTest, BuildMultiple) {
    std::vector<AA_BOUNDING_BOX> boxes;
    std::vector<int> indices;

    for (int i = 0; i < 10; ++i) {
        boxes.push_back(makeBox(i * 5.0f, 0, 0));
        indices.push_back(i);
    }

    bvh.build(boxes, indices);

    EXPECT_FALSE(bvh.empty());
    EXPECT_EQ(bvh.leafCount(), 10);
    EXPECT_GT(bvh.nodeCount(), 10);  // Internal nodes + leaves
}

TEST_F(BVHTest, Clear) {
    std::vector<AA_BOUNDING_BOX> boxes = { makeBox(0, 0, 0), makeBox(5, 0, 0) };
    std::vector<int> indices = { 0, 1 };

    bvh.build(boxes, indices);
    EXPECT_FALSE(bvh.empty());

    bvh.clear();
    EXPECT_TRUE(bvh.empty());
    EXPECT_EQ(bvh.nodeCount(), 0);
}

TEST_F(BVHTest, RayQuerySingleHit) {
    std::vector<AA_BOUNDING_BOX> boxes = { makeBox(0, 0, 0, 2.0f) };
    std::vector<int> indices = { 42 };  // Custom index

    bvh.build(boxes, indices);

    // Ray through center
    VECTOR3D origin(-5, 0, 0);
    VECTOR3D dir(1, 0, 0);

    std::vector<int> hits = bvh.rayQuery(origin, dir);

    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0], 42);
}

TEST_F(BVHTest, RayQueryMiss) {
    std::vector<AA_BOUNDING_BOX> boxes = { makeBox(0, 0, 0, 2.0f) };
    std::vector<int> indices = { 0 };

    bvh.build(boxes, indices);

    // Ray that misses
    VECTOR3D origin(-5, 10, 0);  // Above the box
    VECTOR3D dir(1, 0, 0);

    std::vector<int> hits = bvh.rayQuery(origin, dir);

    EXPECT_TRUE(hits.empty());
}

TEST_F(BVHTest, RayQueryMultipleHits) {
    std::vector<AA_BOUNDING_BOX> boxes;
    std::vector<int> indices;

    // Create 3 boxes along X axis
    for (int i = 0; i < 3; ++i) {
        boxes.push_back(makeBox(i * 5.0f, 0, 0, 2.0f));
        indices.push_back(i);
    }

    bvh.build(boxes, indices);

    // Ray through all boxes
    VECTOR3D origin(-5, 0, 0);
    VECTOR3D dir(1, 0, 0);

    std::vector<int> hits = bvh.rayQuery(origin, dir);

    EXPECT_EQ(hits.size(), 3u);
}

TEST_F(BVHTest, RayQueryClosest) {
    std::vector<AA_BOUNDING_BOX> boxes;
    std::vector<int> indices;

    // Create boxes at different distances
    boxes.push_back(makeBox(10, 0, 0, 2.0f));  // Far
    boxes.push_back(makeBox(5, 0, 0, 2.0f));   // Near
    boxes.push_back(makeBox(15, 0, 0, 2.0f));  // Farthest
    indices = { 0, 1, 2 };

    bvh.build(boxes, indices);

    VECTOR3D origin(-5, 0, 0);
    VECTOR3D dir(1, 0, 0);

    int closest = bvh.rayQueryClosest(origin, dir,
        [&boxes, &origin, &dir](int idx) -> float {
            float tMin, tMax;
            if (boxes[idx].rayIntersect(origin, dir, tMin, tMax)) {
                return tMin > 0 ? tMin : -1.0f;
            }
            return -1.0f;
        });

    EXPECT_EQ(closest, 1);  // Box at x=5 is closest
}

TEST_F(BVHTest, RayQueryClosestNoHit) {
    std::vector<AA_BOUNDING_BOX> boxes = { makeBox(0, 10, 0, 2.0f) };
    std::vector<int> indices = { 0 };

    bvh.build(boxes, indices);

    VECTOR3D origin(-5, 0, 0);
    VECTOR3D dir(1, 0, 0);

    int closest = bvh.rayQueryClosest(origin, dir,
        [&boxes, &origin, &dir](int idx) -> float {
            float tMin, tMax;
            if (boxes[idx].rayIntersect(origin, dir, tMin, tMax)) {
                return tMin > 0 ? tMin : -1.0f;
            }
            return -1.0f;
        });

    EXPECT_EQ(closest, -1);
}

TEST_F(BVHTest, FrustumQueryAllVisible) {
    std::vector<AA_BOUNDING_BOX> boxes;
    std::vector<int> indices;

    // Create small boxes
    for (int i = 0; i < 5; ++i) {
        boxes.push_back(makeBox(i * 2.0f, 0, 0, 1.0f));
        indices.push_back(i);
    }

    bvh.build(boxes, indices);

    // Create a large frustum that contains all boxes
    FRUSTUM frustum;
    // Set up a frustum that encompasses all boxes
    // Using view/projection matrices that look at origin from -Z
    MATRIX4X4 view, proj;
    view.SetTranslation(VECTOR3D(0, 0, -50));
    proj.SetPerspective(90.0f, 1.0f, 0.1f, 1000.0f);
    frustum.SetFromMatrices(view, proj);

    std::vector<int> visible = bvh.frustumQuery(frustum);

    // All 5 boxes should be visible
    EXPECT_EQ(visible.size(), 5u);
}

TEST_F(BVHTest, PerformanceLinearVsBVH) {
    // Create many boxes
    const int N = 1000;
    std::vector<AA_BOUNDING_BOX> boxes;
    std::vector<int> indices;

    for (int i = 0; i < N; ++i) {
        float x = (i % 32) * 3.0f;
        float y = ((i / 32) % 32) * 3.0f;
        float z = (i / 1024) * 3.0f;
        boxes.push_back(makeBox(x, y, z, 2.0f));
        indices.push_back(i);
    }

    bvh.build(boxes, indices);

    // Verify BVH was built correctly
    EXPECT_EQ(bvh.leafCount(), N);
    EXPECT_GT(bvh.maxDepth(), 1);
    EXPECT_LT(bvh.maxDepth(), 20);  // Should be ~log2(N)

    // Perform ray queries (just verify they work)
    VECTOR3D origin(-10, 0, 0);
    VECTOR3D dir(1, 0, 0);

    std::vector<int> hits = bvh.rayQuery(origin, dir);
    EXPECT_GT(hits.size(), 0u);
}

TEST_F(BVHTest, RebuildPreservesCorrectness) {
    std::vector<AA_BOUNDING_BOX> boxes1 = { makeBox(0, 0, 0), makeBox(5, 0, 0) };
    std::vector<int> indices1 = { 0, 1 };

    bvh.build(boxes1, indices1);

    VECTOR3D origin(-5, 0, 0);
    VECTOR3D dir(1, 0, 0);

    auto hits1 = bvh.rayQuery(origin, dir);
    EXPECT_EQ(hits1.size(), 2u);

    // Rebuild with different boxes
    std::vector<AA_BOUNDING_BOX> boxes2 = { makeBox(0, 10, 0) };  // Above ray
    std::vector<int> indices2 = { 99 };

    bvh.build(boxes2, indices2);

    auto hits2 = bvh.rayQuery(origin, dir);
    EXPECT_TRUE(hits2.empty());  // Ray should miss now
}
