// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * test_shape_tiles.cpp - The fast 3D view's tiles and its per-frame choice.
 *
 * Every prepared shape lands in exactly one tile and, each frame, in exactly
 * one bucket; the nearest tiles are drawn as shapes first; a tile between the
 * two pixel thresholds keeps its state; Every shape has no coverage; and the
 * row and note text add up. No GL and no display.
 */

#include <gtest/gtest.h>

#include "view3d/ShapeTiles.h"
#include "view3d/TileChoice.h"

#include <array>
#include <cmath>
#include <map>
#include <random>
#include <vector>

using namespace chiplet;

namespace {

using Mat = std::array<float, 16>;

Mat multiply(const Mat& a, const Mat& b)
{
    Mat r{};
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a[k * 4 + row] * b[c * 4 + k];
            r[c * 4 + row] = s;
        }
    }
    return r;
}

Mat perspective(float fovYDeg, float aspect, float near, float far)
{
    const float t = 1.0f / std::tan(fovYDeg * 3.14159265f / 360.0f);
    Mat m{};
    m[0] = t / aspect;
    m[5] = t;
    m[10] = -(far + near) / (far - near);
    m[11] = -1;
    m[14] = -2 * far * near / (far - near);
    return m;
}

Mat lookAt(const std::array<float, 3>& e, const std::array<float, 3>& c,
           const std::array<float, 3>& up)
{
    float f[3] = {c[0] - e[0], c[1] - e[1], c[2] - e[2]};
    const float fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    for (float& v : f) v /= fl;
    float s[3] = {f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2],
                  f[0] * up[1] - f[1] * up[0]};
    const float sl = std::sqrt(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
    for (float& v : s) v /= sl;
    const float u[3] = {s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2],
                        s[0] * f[1] - s[1] * f[0]};
    Mat m{};
    m[0] = s[0]; m[4] = s[1]; m[8] = s[2];
    m[1] = u[0]; m[5] = u[1]; m[9] = u[2];
    m[2] = -f[0]; m[6] = -f[1]; m[10] = -f[2];
    m[12] = -(s[0] * e[0] + s[1] * e[1] + s[2] * e[2]);
    m[13] = -(u[0] * e[0] + u[1] * e[1] + u[2] * e[2]);
    m[14] = (f[0] * e[0] + f[1] * e[1] + f[2] * e[2]);
    m[15] = 1;
    return m;
}

Mat identity()
{
    Mat m{};
    m[0] = m[5] = m[10] = m[15] = 1;
    return m;
}

// Looking from `eye` at `at`, z up, 45 degrees, 1280x800.
TileCamera camera(const std::array<float, 3>& eye, const std::array<float, 3>& at,
                  const std::array<float, 3>& up = {0, 0, 1})
{
    float d = 0;
    for (int a = 0; a < 3; ++a) d += (eye[a] - at[a]) * (eye[a] - at[a]);
    d = std::sqrt(d);
    TileCamera c;
    c.viewProjection = multiply(perspective(45.0f, 1.6f, d * 0.001f, d * 100.0f), lookAt(eye, at, up));
    c.eye = eye;
    c.fovYDegrees = 45.0f;
    c.viewportHeight = 800;
    return c;
}

// nx * ny square boxes of side s at pitch p.
LayerRecords grid(int nx, int ny, float s, float p, float ox = 0, float oy = 0)
{
    LayerRecords r;
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            r.boxes.push_back({ox + i * p, oy + j * p, ox + i * p + s, oy + j * p + s});
        }
    }
    r.inGds = r.shapes();
    return r;
}

TileLayerInput input(const TiledLayer& layer, float z0 = 0, float z1 = 1, bool visible = true,
                     const Mat& model = identity())
{
    TileLayerInput in;
    in.tiles = &layer;
    in.model = model;
    in.zBottom = z0;
    in.zTop = z1;
    in.visible = visible;
    return in;
}

void expectAddsUp(const TileChoice& c, const std::vector<TileLayerInput>& layers)
{
    ASSERT_EQ(c.layers.size(), layers.size());
    for (std::size_t i = 0; i < layers.size(); ++i) {
        EXPECT_EQ(c.layers[i].total(), layers[i].tiles->shapes) << "layer " << i;
    }
}

}  // namespace

TEST(ShapeTiles, EveryShapeLandsInOneTileAndATileIsOneRange)
{
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(0, 1000);
    LayerRecords in;
    std::map<std::array<float, 4>, int> boxesBefore;
    for (int i = 0; i < 9000; ++i) {
        const float x = u(rng), y = u(rng);
        in.boxes.push_back({x, y, x + 0.5f, y + 0.3f});
        boxesBefore[{x, y, x + 0.5f, y + 0.3f}]++;
    }
    // 1500 shapes that are not boxes, of 1 to 3 trapezoids each, marked by
    // their first trapezoid's yb so a split shape would show.
    std::map<float, int> partsBefore;
    for (int i = 0; i < 1500; ++i) {
        const float y = u(rng), x = u(rng);
        const int parts = 1 + i % 3;
        std::vector<TileTrap> shape;
        for (int k = 0; k < parts; ++k) {
            shape.push_back({y + k, y + k + 1, x, x + 3, x + 1, x + 2});
        }
        in.addShape(shape);
        partsBefore[y] = parts;
    }
    in.inGds = in.shapes() + 7;  // 7 shapes the reader did not prepare

    const TiledLayer t = tileLayer(in);
    EXPECT_EQ(t.inGds, 10507u);
    EXPECT_EQ(t.shapes, 10500u);
    EXPECT_EQ(t.records(), 9000u + 3000u);

    std::map<std::array<float, 4>, int> boxesAfter;
    for (const TileBox& b : t.boxes) boxesAfter[{b.x0, b.y0, b.x1, b.y1}]++;
    EXPECT_EQ(boxesBefore, boxesAfter);

    std::size_t nb = 0, nt = 0, shapes = 0;
    for (const ShapeTile& tile : t.tiles) {
        EXPECT_LE(tile.records(), kTileRecords);
        EXPECT_GT(tile.records(), 0u);
        EXPECT_EQ(tile.b0, nb);
        EXPECT_EQ(tile.t0, nt);
        nb = tile.b1;
        nt = tile.t1;
        shapes += tile.shapes;
        for (std::size_t i = tile.b0; i < tile.b1; ++i) {
            EXPECT_TRUE(t.boxes[i].x0 >= tile.x0 && t.boxes[i].x1 <= tile.x1);
            EXPECT_TRUE(t.boxes[i].y0 >= tile.y0 && t.boxes[i].y1 <= tile.y1);
        }
        // A shape's trapezoids are consecutive and in one tile: walk them by
        // the yb step of 1 they were made with.
        std::size_t i = tile.t0;
        while (i < tile.t1) {
            const float y = t.traps[i].yb;
            ASSERT_TRUE(partsBefore.count(y)) << "a shape split across tiles";
            const int parts = partsBefore[y];
            ASSERT_LE(i + parts, tile.t1) << "a shape split across tiles";
            for (int k = 0; k < parts; ++k) EXPECT_EQ(t.traps[i + k].yb, y + k);
            i += parts;
        }
    }
    EXPECT_EQ(nb, t.boxes.size());
    EXPECT_EQ(nt, t.traps.size());
    EXPECT_EQ(shapes, t.shapes);
    EXPECT_EQ(t.maps.size(), t.tiles.size() * kTileMapTexels);
}

TEST(ShapeTiles, AShapeLargerThanATileIsATileOfItsOwn)
{
    LayerRecords in = grid(10, 10, 1, 2);
    std::vector<TileTrap> big;
    for (std::size_t k = 0; k < kTileRecords + 500; ++k) {
        const float y = 100.0f + k;
        big.push_back({y, y + 1, 0, 5, 0, 5});
    }
    in.addShape(big);
    in.inGds = in.shapes();
    const TiledLayer t = tileLayer(in);
    std::size_t bigTiles = 0;
    for (const ShapeTile& tile : t.tiles) {
        if (tile.records() > kTileRecords) {
            ++bigTiles;
            EXPECT_EQ(tile.shapes, 1u);
            EXPECT_EQ(tile.records(), kTileRecords + 500);
        }
    }
    EXPECT_EQ(bigTiles, 1u);
    EXPECT_EQ(t.shapes, 101u);
}

TEST(ShapeTiles, CoverageMapIsTheAreaFractionAndNeverDropsAShape)
{
    LayerRecords full;
    full.boxes.push_back({0, 0, 100, 100});
    const TiledLayer f = tileLayer(full);
    ASSERT_EQ(f.tiles.size(), 1u);
    for (std::size_t i = 0; i < kTileMapTexels; ++i) ASSERT_EQ(f.map(0)[i], 255);

    // A shape far smaller than a texel still marks its texel.
    LayerRecords sparse;
    sparse.boxes.push_back({0, 0, 0.01f, 0.01f});
    sparse.boxes.push_back({319.99f, 319.99f, 320.0f, 320.0f});
    const TiledLayer s = tileLayer(sparse);
    ASSERT_EQ(s.tiles.size(), 1u);
    EXPECT_GE(s.map(0)[0], 1);
    EXPECT_GE(s.map(0)[kTileMapTexels - 1], 1);
    std::size_t empty = 0;
    for (std::size_t i = 0; i < kTileMapTexels; ++i) empty += s.map(0)[i] == 0;
    EXPECT_EQ(empty, kTileMapTexels - 2);

    // A quarter of the area covered: the mean is about a quarter.
    const TiledLayer g = tileLayer(grid(64, 64, 0.5f, 1.0f));
    double mean = 0;
    for (uint8_t v : g.maps) mean += v;
    mean /= g.maps.size() * 255.0;
    EXPECT_GT(mean, 0.2);
    EXPECT_LT(mean, 0.32);
}

TEST(TileChoice, BucketsAddUpAndHiddenAndOutsideAreWhole)
{
    const TiledLayer a = tileLayer(grid(100, 100, 0.2f, 1.0f));
    const TiledLayer b = tileLayer(grid(100, 100, 0.2f, 1.0f));
    std::vector<TileLayerInput> layers{input(a), input(b, 5, 6, false)};
    TileChoiceMemory memory;
    const TileChoice c = chooseTiles(layers, camera({50, -60, 120}, {50, 50, 0}), {}, memory);
    expectAddsUp(c, layers);
    EXPECT_EQ(c.layers[1].hidden, 10000u);
    EXPECT_EQ(c.layers[1].shapeTiles + c.layers[1].coverageTiles, 0u);

    layers[1].visible = true;
    const TileChoice away = chooseTiles(layers, camera({50, 50, 100}, {50, 1000, 600}), {}, memory);
    expectAddsUp(away, layers);
    EXPECT_EQ(away.layers[0].outside, 10000u);
    EXPECT_EQ(away.layers[1].outside, 10000u);

    // 0.2 um shapes: coverage from far away, shapes close up.
    const TileChoice far = chooseTiles(layers, camera({50, 50, 5000}, {50, 50, 0}), {}, memory);
    EXPECT_EQ(far.layers[0].shape + far.layers[1].shape, 0u);
    EXPECT_EQ(far.layers[0].coverage + far.layers[1].coverage, 20000u);
    const TileChoice near = chooseTiles(layers, camera({50, 50, 20}, {50, 50, 0}), {}, memory);
    EXPECT_GT(near.layers[0].shape, 0u);
    expectAddsUp(near, layers);
    EXPECT_EQ(near.shapeRecords, near.layers[0].shapeRecords + near.layers[1].shapeRecords);
}

TEST(TileChoice, TheBudgetServesTheNearestTilesFirst)
{
    const TiledLayer t = tileLayer(grid(200, 200, 0.5f, 1.0f));  // 40000 boxes
    ASSERT_GE(t.tiles.size(), 20u);
    const std::size_t per = t.tiles[0].records();
    std::vector<TileLayerInput> layers{input(t)};
    TileChoiceParams p;
    p.upPx = p.downPx = 0.0f;
    p.budget = 3 * per;
    const std::array<float, 3> eye{0, 0, 50};
    TileChoiceMemory memory;
    const TileChoice c = chooseTiles(layers, camera(eye, {100, 100, 0}), p, memory);
    expectAddsUp(c, layers);
    float farthestShape = 0, nearestOther = 1e30f;
    for (std::size_t k = 0; k < t.tiles.size(); ++k) {
        const ShapeTile& tile = t.tiles[k];
        const float lo[3] = {tile.x0, tile.y0, 0}, hi[3] = {tile.x1, tile.y1, 1};
        float d2 = 0;
        for (int a = 0; a < 3; ++a) {
            const float q = std::clamp(eye[a], lo[a], hi[a]);
            d2 += (eye[a] - q) * (eye[a] - q);
        }
        if (c.buckets[0][k] == TileBucket::Shape) farthestShape = std::max(farthestShape, d2);
        if (c.buckets[0][k] == TileBucket::Coverage) nearestOther = std::min(nearestOther, d2);
    }
    EXPECT_GE(c.layers[0].shapeTiles, 1u);
    EXPECT_LE(c.layers[0].shapeTiles, 3u);
    EXPECT_LE(c.shapeRecords, p.budget);
    EXPECT_LE(farthestShape, nearestOther);
}

TEST(TileChoice, EveryShapeHasNoCoverageAndNoBudget)
{
    const TiledLayer t = tileLayer(grid(200, 200, 0.5f, 1.0f));
    std::vector<TileLayerInput> layers{input(t)};
    TileChoiceParams p;
    p.every = true;
    p.budget = 1;
    TileChoiceMemory memory;
    const TileChoice c = chooseTiles(layers, camera({100, 100, 5000}, {100, 100, 0}), p, memory);
    expectAddsUp(c, layers);
    EXPECT_EQ(c.layers[0].coverage, 0u);
    EXPECT_EQ(c.layers[0].shape, 40000u);
    EXPECT_EQ(c.shapeRecords, 40000u);
}

TEST(TileChoice, ATileBetweenTheThresholdsKeepsItsState)
{
    const TiledLayer t = tileLayer(grid(10, 10, 1.0f, 2.0f));  // one tile, 1 um shapes
    ASSERT_EQ(t.tiles.size(), 1u);
    std::vector<TileLayerInput> layers{input(t)};
    const float k = 800 / (2.0f * std::tan(22.5f * 3.14159265f / 180.f));
    const float mid = k / 1.75f;  // 1 um is 1.75 px: between 1.5 and 2
    TileChoiceMemory memory;
    EXPECT_EQ(chooseTiles(layers, camera({10, 10, mid}, {10, 10, 0}), {}, memory).layers[0].shape, 0u);
    EXPECT_EQ(chooseTiles(layers, camera({10, 10, mid / 2}, {10, 10, 0}), {}, memory).layers[0].shape, 100u);
    EXPECT_EQ(chooseTiles(layers, camera({10, 10, mid}, {10, 10, 0}), {}, memory).layers[0].shape, 100u);
    // A new layer list starts afresh.
    std::vector<TileLayerInput> two{input(t), input(t)};
    const TileChoice fresh = chooseTiles(two, camera({10, 10, mid}, {10, 10, 0}), {}, memory);
    EXPECT_EQ(fresh.layers[0].shape + fresh.layers[1].shape, 0u);
}

TEST(TileChoice, TheModelMatrixPlacesAndScalesTheTiles)
{
    // Studio's frame: GDS x -> world x, layer z -> world y, GDS y -> world -z,
    // in mm. The same 1 um shapes seen from 0.2 mm are about 5 px, so shapes;
    // from 1 mm under 1 px, so coverage; moved 10 mm away along x they are
    // outside a camera looking at the origin.
    const TiledLayer t = tileLayer(grid(10, 10, 1.0f, 2.0f));
    Mat m{};
    m[0] = 0.001f;    // x um -> x mm
    m[6] = -0.001f;   // y um -> -z mm
    m[9] = 0.001f;    // z um -> y mm
    m[15] = 1;
    std::vector<TileLayerInput> layers{input(t, 0, 1, true, m)};
    TileChoiceMemory memory;
    const TileCamera cam = camera({0.01f, 0.2f, -0.01f}, {0.01f, 0, -0.01f}, {0, 0, -1});
    EXPECT_EQ(chooseTiles(layers, cam, {}, memory).layers[0].shape, 100u);
    const TileCamera far = camera({0.01f, 1.0f, -0.01f}, {0.01f, 0, -0.01f}, {0, 0, -1});
    EXPECT_EQ(chooseTiles(layers, far, {}, memory).layers[0].coverage, 100u);
    layers[0].model[12] = 10.0f;
    EXPECT_EQ(chooseTiles(layers, cam, {}, memory).layers[0].outside, 100u);
}

TEST(TileChoiceText, RowsAddUpAndSayWhichBucket)
{
    LayerBuckets b;
    b.shape = 3912;
    b.coverage = 712000;
    b.outside = 33306;
    EXPECT_EQ(fastRowLabel(749218, b),
              "749,218 shapes; at this zoom 3,912 drawn as shapes, 712,000 as coverage, "
              "33,306 outside the view");

    LayerBuckets all;
    all.shape = 64;
    EXPECT_EQ(fastRowLabel(64, all), "64 shapes, all drawn");

    LayerBuckets hidden;
    hidden.hidden = 55609;
    EXPECT_EQ(fastRowLabel(55609, hidden), "55,609 shapes, hidden");

    LayerBuckets part;
    part.shape = 40000;
    EXPECT_EQ(fastRowLabel(55609, part),
              "55,609 shapes; at this zoom 40,000 drawn as shapes, 0 as coverage, 0 outside "
              "the view, 15,609 not prepared (memory limit)");
}

TEST(TileChoiceText, NotesNameTheSettingTheCoverageAndTheLimit)
{
    EXPECT_EQ(shapeModeNote(ShapeMode::Standard), "");
    EXPECT_EQ(shapeModeNote(ShapeMode::Fast), "3D: Fast");
    EXPECT_EQ(shapeModeNote(ShapeMode::Every), "3D: Every shape");
    EXPECT_EQ(fastCoverageNote("interposer", 4, 1560312, 1605829),
              "3D at this zoom: interposer draws 4 layers partly as coverage (1,560,312 of "
              "1,605,829 shapes). Zoom in, or choose Every shape.");
    EXPECT_EQ(fastCoverageNote("U1", 1, 10, 20),
              "3D at this zoom: U1 draws 1 layer partly as coverage (10 of 20 shapes). Zoom "
              "in, or choose Every shape.");
    EXPECT_EQ(fastCoverageNote("U1", 0, 0, 0), "");
    EXPECT_EQ(preparedNote("U1", 50, 22, "Metal4Filler", 40000, 55609),
              "3D: U1 50/22 Metal4Filler: 40,000 of 55,609 shapes prepared (memory limit).");
    EXPECT_EQ(preparedNote("U1", 50, 22, "Metal4Filler", 55609, 55609), "");
    EXPECT_EQ(everyShapeNote(4.1234), "3D: Every shape; the last frame took 4.1 s.");
}
