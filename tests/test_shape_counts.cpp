// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * test_shape_counts.cpp - 3D never draws fewer shapes than the GDS holds
 * without saying so.
 *
 * Four limits can stop a shape between the GDS and the screen: the
 * extractor's read limit per layer, its area floor, its via-layer cut limit,
 * and the mesh builder's limit per layer. The extractor counts what each one
 * took, the mesh builder adds its own, and the note and the layer row carry
 * both numbers. All without a GL context.
 */

#include <gtest/gtest.h>

#include "core/LayerStackup.h"
#include "core/TechResolve.h"
#include "view3d/GDSLayerExtractor.h"
#include "view3d/LayerMeshBuilder.h"

#include <QColor>

#include <cmath>
#include <map>
#include <vector>

#ifdef HAVE_KLAYOUT
#include "dbBox.h"
#include "dbCell.h"
#include "dbLayerProperties.h"
#include "dbLayout.h"
#include "dbText.h"
#endif

using namespace chiplet;

namespace {

SimplePolygon square(double x, double size)
{
    SimplePolygon sq;
    sq.points = {Point2D(x, 0.0), Point2D(x + size, 0.0), Point2D(x + size, size),
                 Point2D(x, size)};
    return sq;
}

const ShapeCount* countOf(const std::vector<ShapeCount>& counts, int layer, int datatype)
{
    for (const auto& c : counts) {
        if (c.key == LayerKey(layer, datatype)) return &c;
    }
    return nullptr;
}

}  // namespace

TEST(ShapeCounts, NoteNamesBothNumbersAndEachLimitThatTookShapes)
{
    ShapeCount c;
    c.key = LayerKey(50, 22);
    c.name = "Metal4.filler";
    c.inGds = 1234567;
    c.drawn = 10000;
    c.underArea = 1203;
    c.areaFloor = 0.1;
    c.pastRead = 1000000;
    c.readLimit = 10000;
    c.viaSkipped = 7;
    c.viaLimit = 50000;
    c.pastMesh = 999;
    c.meshLimit = 50000;
    EXPECT_EQ(shapeCountNote("interposer", c),
              "3D: interposer 50/22 Metal4.filler: 10,000 of 1,234,567 shapes shown; "
              "1,203 under 0.1 um² skipped; 1,000,000 past the read limit of 10,000 "
              "per layer; a via layer over 50,000 cuts is not drawn; 999 past the mesh "
              "limit of 50,000 per layer.");
    EXPECT_EQ(shapeCountLabel(c), "3D shows 10,000 of 1,234,567 shapes");
}

TEST(ShapeCounts, NoteIsEmptyOnlyWhenEveryShapeIsDrawn)
{
    ShapeCount all;
    all.key = LayerKey(8, 0);
    all.inGds = 412;
    all.drawn = 412;
    EXPECT_EQ(shapeCountNote("U1", all), "");
    EXPECT_EQ(shapeCountLabel(all), "");

    // Fewer drawn with no limit named still says both numbers.
    ShapeCount fewer;
    fewer.key = LayerKey(7, 0);
    fewer.inGds = 5;
    EXPECT_EQ(shapeCountNote("U1", fewer), "3D: U1 7/0: 0 of 5 shapes shown.");
    EXPECT_EQ(shapeCountLabel(fewer), "3D shows 0 of 5 shapes");
}

TEST(ShapeCounts, MeshBuilderStopsWhereItSaysItDoes)
{
    LayerMeshBuilder builder;
    builder.setMaxPolygonsPerLayer(3);
    LayerPolygons polys;
    polys.key = LayerKey(1, 0);
    for (int i = 0; i < 5; ++i) {
        polys.polygons.push_back(square(i * 20.0, 10.0));
    }
    const LayerMesh mesh = builder.buildLayerMesh(polys, 0.0, 1.0, QColor(Qt::red));
    EXPECT_EQ(mesh.shapes, 3u);
    EXPECT_EQ(builder.builtOf(5), mesh.shapes);
    EXPECT_EQ(builder.builtOf(2), 2u);
}

TEST(ShapeCounts, RowsCarryTheCountAndListALayer3DDrewNoneOf)
{
    Component3DGeometry geometry;
    geometry.layers.emplace_back();
    geometry.layers.back().key = LayerKey(1, 0);
    geometry.layers.back().name = "M1";

    ShapeCount m1;
    m1.key = LayerKey(1, 0);
    m1.name = "M1";
    m1.inGds = 30;
    m1.drawn = 15;
    m1.pastRead = 15;
    m1.readLimit = 15;
    ShapeCount via;
    via.key = LayerKey(3, 0);
    via.name = "Via3";
    via.inGds = 30;
    via.viaSkipped = 30;
    via.viaLimit = 25;
    ShapeCount whole;
    whole.key = LayerKey(4, 0);
    whole.name = "Via4";
    whole.inGds = 20;
    whole.drawn = 20;

    const auto rows = layerRows("U1", geometry, {m1, via, whole});
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].key, LayerKey(1, 0));
    EXPECT_TRUE(rows[0].meshed);
    EXPECT_EQ(rows[0].count, "3D shows 15 of 30 shapes");
    EXPECT_EQ(rows[0].countNote,
              "3D: U1 1/0 M1: 15 of 30 shapes shown; 15 past the read limit of 15 per layer.");
    EXPECT_EQ(rows[1].key, LayerKey(3, 0));
    EXPECT_FALSE(rows[1].meshed);
    EXPECT_EQ(rows[1].name, "Via3");
    EXPECT_EQ(rows[1].count, "3D shows 0 of 30 shapes");
    EXPECT_EQ(rows[1].countNote,
              "3D: U1 3/0 Via3: 0 of 30 shapes shown; a via layer over 25 cuts is not drawn.");
}

TEST(ShapeCounts, ADatatypeWithNoEntryIsCountedAtTheHeightItIsDrawnAt)
{
    // 8/2 and 126/2 have no stackup entry. 3D draws them at their layer
    // number's height, so they are counted there, and the note says whose
    // height it is. Layer 9 has no entry at all: placed nowhere, not counted.
    LayerStackup stackup;
    stackup.addLayer(8, 0, 0.0, 1.0, "Metal1");
    stackup.addLayer(126, 0, 5.0, 2.0, "TopMetal1");

    std::map<LayerKey, LayerShapeTally> tally;
    tally[LayerKey(8, 0)].inGds = 5;
    tally[LayerKey(8, 0)].kept = 5;
    LayerShapeTally& pin = tally[LayerKey(8, 2)];
    pin.inGds = 30;
    pin.kept = 20;
    pin.pastRead = 10;
    LayerShapeTally& topPin = tally[LayerKey(126, 2)];
    topPin.inGds = 179;
    topPin.kept = 178;
    topPin.underArea = 1;
    tally[LayerKey(9, 2)].inGds = 3;
    tally[LayerKey(9, 2)].kept = 3;

    ExtractionConfig config;
    config.max_polygons_per_layer = 20;
    config.min_polygon_area = 0.1;
    const LayerMeshBuilder builder;

    const auto counts = shapeCounts(tally, config, stackup, builder);
    ASSERT_EQ(counts.size(), 3u);
    EXPECT_FALSE(countOf(counts, 8, 0)->borrowed);
    EXPECT_EQ(countOf(counts, 9, 2), nullptr);

    const ShapeCount* m1 = countOf(counts, 8, 2);
    ASSERT_NE(m1, nullptr);
    EXPECT_EQ(m1->name, "Metal1");
    EXPECT_TRUE(m1->borrowed);
    EXPECT_EQ(shapeCountNote("U1", *m1),
              "3D: U1 8/2 has no stackup entry; drawn at Metal1's height: 20 of 30 shapes "
              "shown; 10 past the read limit of 20 per layer.");

    const ShapeCount* tm1 = countOf(counts, 126, 2);
    ASSERT_NE(tm1, nullptr);
    EXPECT_EQ(shapeCountNote("U1", *tm1),
              "3D: U1 126/2 has no stackup entry; drawn at TopMetal1's height: 178 of 179 "
              "shapes shown; 1 under 0.1 um² skipped.");

    // The count names the height the mesh is placed at.
    const LayerPlacement place =
        LayerMeshBuilder::placeLayer(LayerKey(126, 2), stackup, nullptr, nullptr, false, 0.0);
    const LayerPlacement own =
        LayerMeshBuilder::placeLayer(LayerKey(126, 0), stackup, nullptr, nullptr, false, 0.0);
    EXPECT_TRUE(place.placed);
    EXPECT_EQ(place.name, tm1->name);
    EXPECT_EQ(place.zBottom, own.zBottom);

    Component3DGeometry geometry;
    geometry.layers.emplace_back();
    geometry.layers.back().key = LayerKey(8, 2);
    geometry.layers.back().name = "Metal1";
    const auto rows = layerRows("U1", geometry, counts);
    ASSERT_FALSE(rows.empty());
    EXPECT_EQ(rows[0].key, LayerKey(8, 2));
    EXPECT_EQ(rows[0].count, "3D shows 20 of 30 shapes");
    EXPECT_EQ(rows[0].countNote, shapeCountNote("U1", *m1));
}

#ifdef HAVE_KLAYOUT

namespace {

// `n` squares of `size_um` in a row at `pitch_um`, starting at `y_um`.
void addRow(db::Layout& ly, db::Cell& cell, unsigned int li, int n, double size_um,
            double pitch_um, double y_um)
{
    const double dbu = ly.dbu();
    auto c = [dbu](double um) { return static_cast<db::Coord>(std::llround(um / dbu)); };
    for (int i = 0; i < n; ++i) {
        const double x = i * pitch_um;
        cell.shapes(li).insert(db::Box(c(x), c(y_um), c(x + size_um), c(y_um + size_um)));
    }
}

}  // namespace

TEST(ShapeCounts, ExtractorCountsWhatEachLimitTookAndTheNoteSaysIt)
{
    db::Layout ly;
    ly.dbu(0.001);
    db::Cell& top = ly.cell(ly.add_cell("TOP"));
    auto layer = [&](int l) { return ly.insert_layer(db::LayerProperties(l, 0)); };

    const unsigned int m1 = layer(1);       // 30 shapes, read limit 20
    addRow(ly, top, m1, 30, 1.0, 2.0, 0.0);
    top.shapes(m1).insert(db::Text("label", db::Trans()));  // not a shape
    const unsigned int m2 = layer(2);       // 10 kept, 5 under the area floor
    addRow(ly, top, m2, 10, 1.0, 2.0, 10.0);
    addRow(ly, top, m2, 5, 0.2, 2.0, 20.0);
    const unsigned int via3 = layer(3);     // 30 cuts, over the via limit of 25
    addRow(ly, top, via3, 30, 0.19, 0.41, 30.0);
    const unsigned int via4 = layer(4);     // 12 cuts, under it and the mesh limit
    addRow(ly, top, via4, 12, 0.19, 0.41, 40.0);
    const unsigned int cont5 = layer(5);    // all tiny: drawn as cuts, not culled
    addRow(ly, top, cont5, 6, 0.2, 2.0, 50.0);
    const unsigned int other = layer(6);    // not in the stackup
    addRow(ly, top, other, 3, 1.0, 2.0, 60.0);

    ExtractionConfig config;
    config.max_polygons_per_layer = 20;
    config.min_polygon_area = 0.1;
    config.via_layers = {LayerKey(3, 0), LayerKey(4, 0)};
    config.via_max_cuts = 25;
    config.modeled_layers = {LayerKey(1, 0), LayerKey(2, 0), LayerKey(3, 0),
                             LayerKey(4, 0), LayerKey(5, 0)};
    GDSLayerExtractor extractor;
    extractor.setConfig(config);
    const auto polygons = extractor.extract(&ly, "TOP", {});

    const auto& tally = extractor.lastTally();
    ASSERT_EQ(tally.size(), 6u);
    EXPECT_EQ(tally.at(LayerKey(1, 0)).inGds, 30u);
    EXPECT_EQ(tally.at(LayerKey(1, 0)).kept, 20u);
    EXPECT_EQ(tally.at(LayerKey(1, 0)).pastRead, 10u);
    EXPECT_EQ(tally.at(LayerKey(2, 0)).underArea, 5u);
    EXPECT_EQ(tally.at(LayerKey(3, 0)).viaSkipped, 30u);
    EXPECT_EQ(tally.at(LayerKey(4, 0)).kept, 12u);
    EXPECT_EQ(polygons.count(LayerKey(3, 0)), 0u);
    EXPECT_EQ(tally.at(LayerKey(5, 0)).kept, 6u);
    EXPECT_EQ(tally.at(LayerKey(5, 0)).underArea, 0u);

    LayerStackup stackup;
    stackup.addLayer(1, 0, 0.0, 1.0, "M1");
    stackup.addLayer(2, 0, 2.0, 1.0, "M2");
    stackup.addLayer(3, 0, 1.0, 1.0, "Via3");
    stackup.addLayer(4, 0, 3.0, 1.0, "Via4");
    stackup.addLayer(5, 0, -1.0, 1.0, "Cont5");
    LayerMeshBuilder builder;
    builder.setMaxPolygonsPerLayer(15);

    const auto counts = shapeCounts(tally, config, stackup, builder);
    ASSERT_EQ(counts.size(), 5u);
    EXPECT_EQ(countOf(counts, 6, 0), nullptr);
    EXPECT_EQ(shapeCountNote("U1", *countOf(counts, 1, 0)),
              "3D: U1 1/0 M1: 15 of 30 shapes shown; 10 past the read limit of 20 per "
              "layer; 5 past the mesh limit of 15 per layer.");
    EXPECT_EQ(shapeCountNote("U1", *countOf(counts, 2, 0)),
              "3D: U1 2/0 M2: 10 of 15 shapes shown; 5 under 0.1 um² skipped.");
    EXPECT_EQ(shapeCountNote("U1", *countOf(counts, 3, 0)),
              "3D: U1 3/0 Via3: 0 of 30 shapes shown; a via layer over 25 cuts is not drawn.");
    EXPECT_EQ(shapeCountNote("U1", *countOf(counts, 4, 0)), "");
    EXPECT_EQ(shapeCountNote("U1", *countOf(counts, 5, 0)), "");
}

#endif  // HAVE_KLAYOUT
