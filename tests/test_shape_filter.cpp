/**
 * test_shape_filter.cpp - Tests for area-based shape filtering
 */

#include <gtest/gtest.h>
#include "view3d/ShapeFilter.h"
#include <cmath>

using namespace chiplet;

// Helper: create a square polygon with known area
static SimplePolygon makeSquare(double side) {
    SimplePolygon poly;
    poly.points = {
        {0, 0}, {side, 0}, {side, side}, {0, side}
    };
    return poly;
}

// Helper: create a square polygon centered at (cx, cy)
static SimplePolygon makeSquareAt(double cx, double cy, double side) {
    double h = side / 2.0;
    SimplePolygon poly;
    poly.points = {
        {cx - h, cy - h}, {cx + h, cy - h},
        {cx + h, cy + h}, {cx - h, cy + h}
    };
    return poly;
}

// Helper: create polygon data with known areas
static std::map<LayerKey, LayerPolygons> makeTestPolygons() {
    std::map<LayerKey, LayerPolygons> result;

    // Layer 1: polygons with areas 1, 4, 9, 16 um^2
    LayerKey k1{1, 0};
    LayerPolygons lp1;
    lp1.key = k1;
    lp1.name = "Metal1";
    lp1.polygons.push_back(makeSquare(1.0));   // area = 1
    lp1.polygons.push_back(makeSquare(2.0));   // area = 4
    lp1.polygons.push_back(makeSquare(3.0));   // area = 9
    lp1.polygons.push_back(makeSquare(4.0));   // area = 16
    result[k1] = lp1;

    // Layer 2: polygons with areas 100, 10000 um^2
    LayerKey k2{2, 0};
    LayerPolygons lp2;
    lp2.key = k2;
    lp2.name = "Metal2";
    lp2.polygons.push_back(makeSquare(10.0));   // area = 100
    lp2.polygons.push_back(makeSquare(100.0));  // area = 10000
    result[k2] = lp2;

    return result;
}

// --- Statistics tests ---

TEST(ShapeFilterTest, ComputeStatisticsEmpty) {
    std::map<LayerKey, LayerPolygons> empty;
    auto stats = ShapeFilter::computeStatistics(empty);
    EXPECT_EQ(stats.total_polygons, 0u);
    EXPECT_EQ(stats.total_layers, 0u);
    EXPECT_DOUBLE_EQ(stats.min_area, 0.0);
    EXPECT_DOUBLE_EQ(stats.max_area, 0.0);
}

TEST(ShapeFilterTest, ComputeStatisticsSinglePolygon) {
    std::map<LayerKey, LayerPolygons> data;
    LayerKey k{1, 0};
    LayerPolygons lp;
    lp.key = k;
    lp.name = "test";
    lp.polygons.push_back(makeSquare(5.0));  // area = 25
    data[k] = lp;

    auto stats = ShapeFilter::computeStatistics(data);
    EXPECT_EQ(stats.total_polygons, 1u);
    EXPECT_EQ(stats.total_layers, 1u);
    EXPECT_NEAR(stats.min_area, 25.0, 0.01);
    EXPECT_NEAR(stats.max_area, 25.0, 0.01);
}

TEST(ShapeFilterTest, ComputeStatisticsMultiplePolygons) {
    auto data = makeTestPolygons();
    auto stats = ShapeFilter::computeStatistics(data);

    EXPECT_EQ(stats.total_polygons, 6u);
    EXPECT_EQ(stats.total_layers, 2u);
    EXPECT_NEAR(stats.min_area, 1.0, 0.01);
    EXPECT_NEAR(stats.max_area, 10000.0, 0.01);
}

TEST(ShapeFilterTest, ComputeStatisticsMultipleLayers) {
    auto data = makeTestPolygons();
    auto stats = ShapeFilter::computeStatistics(data);

    // min is from layer 1 (1 um^2), max from layer 2 (10000 um^2)
    EXPECT_NEAR(stats.min_area, 1.0, 0.01);
    EXPECT_NEAR(stats.max_area, 10000.0, 0.01);
    EXPECT_EQ(stats.total_layers, 2u);
}

// --- Threshold mapping tests ---

TEST(ShapeFilterTest, ThresholdZeroPercent) {
    auto data = makeTestPolygons();
    auto stats = ShapeFilter::computeStatistics(data);

    double t = ShapeFilter::thresholdFromPercentage(0.0, stats);
    EXPECT_DOUBLE_EQ(t, 0.0);
}

TEST(ShapeFilterTest, ThresholdHundredPercent) {
    auto data = makeTestPolygons();
    auto stats = ShapeFilter::computeStatistics(data);

    double t = ShapeFilter::thresholdFromPercentage(100.0, stats);
    // At 100%, threshold = min * (max/min)^1.0 = max
    EXPECT_NEAR(t, stats.max_area, 0.01);
}

TEST(ShapeFilterTest, ThresholdFiftyPercentLogarithmic) {
    auto data = makeTestPolygons();
    auto stats = ShapeFilter::computeStatistics(data);

    double t = ShapeFilter::thresholdFromPercentage(50.0, stats);
    // Geometric mean: sqrt(1 * 10000) = 100
    EXPECT_NEAR(t, 100.0, 1.0);
}

TEST(ShapeFilterTest, ThresholdEmptyStats) {
    AreaStatistics empty;
    double t = ShapeFilter::thresholdFromPercentage(50.0, empty);
    EXPECT_DOUBLE_EQ(t, 0.0);
}

TEST(ShapeFilterTest, ThresholdEqualMinMax) {
    AreaStatistics stats;
    stats.min_area = 10.0;
    stats.max_area = 10.0;
    stats.total_polygons = 5;

    // Any percentage > 0 should filter everything
    double t = ShapeFilter::thresholdFromPercentage(50.0, stats);
    EXPECT_GT(t, stats.max_area);
}

// --- Filtering tests ---

TEST(ShapeFilterTest, FilterKeepsAllAtZeroThreshold) {
    auto data = makeTestPolygons();
    auto filtered = ShapeFilter::filter(data, 0.0);

    size_t total = 0;
    for (const auto& [k, lp] : filtered) {
        total += lp.polygons.size();
    }
    EXPECT_EQ(total, 6u);
}

TEST(ShapeFilterTest, FilterRemovesSmallPolygons) {
    auto data = makeTestPolygons();

    // threshold = 5.0: keeps area>=5 => areas 9, 16, 100, 10000 (4 polygons)
    auto filtered = ShapeFilter::filter(data, 5.0);

    size_t total = 0;
    for (const auto& [k, lp] : filtered) {
        total += lp.polygons.size();
    }
    EXPECT_EQ(total, 4u);

    // Layer 1 should have 2 polygons (9, 16)
    LayerKey k1{1, 0};
    EXPECT_EQ(filtered[k1].polygons.size(), 2u);

    // Layer 2 should have 2 polygons (100, 10000)
    LayerKey k2{2, 0};
    EXPECT_EQ(filtered[k2].polygons.size(), 2u);
}

TEST(ShapeFilterTest, FilterRemovesAllAtHugeThreshold) {
    auto data = makeTestPolygons();
    auto filtered = ShapeFilter::filter(data, 1e12);

    size_t total = 0;
    for (const auto& [k, lp] : filtered) {
        total += lp.polygons.size();
    }
    EXPECT_EQ(total, 0u);
}

TEST(ShapeFilterTest, FilterPreservesLayerStructure) {
    auto data = makeTestPolygons();

    // Filter everything away
    auto filtered = ShapeFilter::filter(data, 1e12);

    // Both layers should still exist in the map
    EXPECT_EQ(filtered.size(), 2u);
    LayerKey k1{1, 0};
    LayerKey k2{2, 0};
    EXPECT_TRUE(filtered.count(k1));
    EXPECT_TRUE(filtered.count(k2));
    EXPECT_EQ(filtered[k1].name, "Metal1");
    EXPECT_EQ(filtered[k2].name, "Metal2");
}

TEST(ShapeFilterTest, ProgressiveFiltering) {
    auto data = makeTestPolygons();
    auto stats = ShapeFilter::computeStatistics(data);

    size_t prev_count = 6;  // total polygons

    // Increasing threshold should monotonically decrease polygon count
    for (double pct = 10.0; pct <= 100.0; pct += 10.0) {
        double threshold = ShapeFilter::thresholdFromPercentage(pct, stats);
        auto filtered = ShapeFilter::filter(data, threshold);

        size_t count = 0;
        for (const auto& [k, lp] : filtered) {
            count += lp.polygons.size();
        }

        EXPECT_LE(count, prev_count)
            << "At " << pct << "%, count " << count
            << " should be <= previous " << prev_count;
        prev_count = count;
    }
}
