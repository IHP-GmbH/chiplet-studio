// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Via layers must render their INDIVIDUAL cuts (physical pillars), not vanish.
//
// A via cut array is sub-micron: the interposer Via4 cut is 0.19um = 0.036 um^2,
// which sits below the min_polygon_area the render path uses to drop noise
// (0.1 um^2 in AssemblyView::renderComponent). A normal extract therefore
// deletes the whole M4<->M5 connector while the larger TopVia1 (0.176) and
// TopVia2 (0.81) cuts survive -- the reported "M4 has no pillars to M5".
//
// GDSLayerExtractor fixes this: layers listed in ExtractionConfig::via_layers
// are exempt from the area cull and their cuts are drawn as-is (NOT fused into a
// solid body), so the 3D view stays faithful to the real via array. Layers
// denser than via_max_cuts are skipped. These tests lock that behavior in.

#include <gtest/gtest.h>
#include "view3d/GDSLayerExtractor.h"
#include "core/LayerStackup.h"
#include <algorithm>
#include <cctype>

#ifdef HAVE_KLAYOUT

#include "dbLayout.h"
#include "dbCell.h"
#include "dbBox.h"
#include "dbLayerProperties.h"

#include <cmath>

namespace chiplet {
namespace {

constexpr int kViaLayer = 66;      // Via4
constexpr int kViaDatatype = 0;

// Insert an n x n grid of `cut_um` squares at `pitch_um` pitch onto an already
// created layer index `li`, with the lower-left cut centered at
// (origin_x_um, origin_y_um). Takes the layer index (not layer/datatype) so
// several grids can share one layer -- insert_layer() always mints a NEW index,
// which would otherwise scatter the cuts across duplicate (layer/datatype) keys.
void addCutGrid(db::Layout& ly, db::Cell& cell, unsigned int li, int n,
                double cut_um, double pitch_um,
                double origin_x_um = 0.0, double origin_y_um = 0.0)
{
    const double dbu = ly.dbu();
    const db::Coord half = static_cast<db::Coord>(std::llround((cut_um / 2.0) / dbu));
    const db::Coord pitch = static_cast<db::Coord>(std::llround(pitch_um / dbu));
    const db::Coord ox = static_cast<db::Coord>(std::llround(origin_x_um / dbu));
    const db::Coord oy = static_cast<db::Coord>(std::llround(origin_y_um / dbu));
    for (int r = 0; r < n; ++r) {
        for (int c = 0; c < n; ++c) {
            const db::Coord cx = ox + c * pitch;
            const db::Coord cy = oy + r * pitch;
            cell.shapes(li).insert(
                db::Box(cx - half, cy - half, cx + half, cy + half));
        }
    }
}

// A layout with a single 5x5 Via4-like cluster (0.19um cuts, 0.41um pitch).
db::Layout makeSingleClusterLayout()
{
    db::Layout ly;
    ly.dbu(0.001);
    db::Cell& top = ly.cell(ly.add_cell("TOP"));
    const unsigned int li = ly.insert_layer(db::LayerProperties(kViaLayer, kViaDatatype));
    addCutGrid(ly, top, li, 5, 0.19, 0.41);
    return ly;
}

const LayerKey kViaKey(kViaLayer, kViaDatatype);

// Baseline: with the render path's area cull and no via handling, every
// 0.036 um^2 cut is dropped -> the layer disappears entirely (the bug).
TEST(ViaCutRendering, TinyCutsCulledWhenNotMarkedVia)
{
    db::Layout ly = makeSingleClusterLayout();

    GDSLayerExtractor ex;
    ExtractionConfig cfg;
    cfg.min_polygon_area = 0.1;  // same value renderComponent uses
    ex.setConfig(cfg);

    auto polys = ex.extract(&ly, "TOP");
    EXPECT_EQ(polys.count(kViaKey), 0u)
        << "0.036 um^2 cuts should be culled by min_polygon_area=0.1";
}

// Marking the layer as a via keeps ALL its cuts (exempt from the area cull) and
// draws them individually -- one polygon per cut, not a fused body -- so the
// view matches the physical via array.
TEST(ViaCutRendering, ViaCutsDrawnIndividuallyAndSurviveCull)
{
    db::Layout ly = makeSingleClusterLayout();  // 5x5 = 25 cuts of 0.036 um^2

    GDSLayerExtractor ex;
    ExtractionConfig cfg;
    cfg.min_polygon_area = 0.1;  // would cull every 0.036 um^2 cut
    cfg.via_layers.insert(kViaKey);
    ex.setConfig(cfg);

    auto polys = ex.extract(&ly, "TOP");
    ASSERT_EQ(polys.count(kViaKey), 1u) << "via layer must survive the cull";

    const LayerPolygons& lp = polys.at(kViaKey);
    EXPECT_EQ(lp.polygons.size(), 25u)
        << "each cut is drawn as its own pillar (not fused into one body)";
    // Each polygon is a single 0.19um cut, i.e. below the cull threshold.
    EXPECT_LT(std::abs(lp.polygons[0].area()), 0.1)
        << "cuts are kept at their true (sub-cull) size, not enlarged";
}

// Backstop: a connector layer the name rule did not recognize as a via (so it
// is NOT in via_layers) must still not be deleted by the area cull, as long as
// the stackup models it. This is what guarantees the bug cannot recur for a PDK
// whose contact is named neither "via" nor "con" (e.g. a future "V1"/"M4M5").
TEST(ViaCutRendering, ModeledLayerRescuedWhenNameRuleMisses)
{
    db::Layout ly = makeSingleClusterLayout();

    GDSLayerExtractor ex;
    ExtractionConfig cfg;
    cfg.min_polygon_area = 0.1;
    cfg.modeled_layers.insert(kViaKey);  // stackup models it, name rule missed it
    ex.setConfig(cfg);

    auto polys = ex.extract(&ly, "TOP");
    ASSERT_EQ(polys.count(kViaKey), 1u)
        << "a modeled layer must never be fully deleted by the area cull";
    EXPECT_EQ(polys.at(kViaKey).polygons.size(), 25u)
        << "the rescued layer draws its individual cuts";
}

// Perf guard: a via layer denser than via_max_cuts is skipped, not drawn. A
// dense standard-cell die carries ~5e5 cuts per via layer; drawing every one
// would stall load and swamp the view. Skipped = not drawn (as before this
// path) -- those internal vias are not the targeted connector.
TEST(ViaCutRendering, DenseViaLayerSkippedByCap)
{
    db::Layout ly = makeSingleClusterLayout();  // 5x5 = 25 cuts

    GDSLayerExtractor ex;
    ExtractionConfig cfg;
    cfg.min_polygon_area = 0.1;
    cfg.via_layers.insert(kViaKey);
    cfg.via_max_cuts = 10;  // 25 cuts > 10 -> skip
    ex.setConfig(cfg);

    auto polys = ex.extract(&ly, "TOP");
    EXPECT_EQ(polys.count(kViaKey), 0u)
        << "a via layer above the cut cap must be skipped, not drawn";
}

// Classification: the name rule (contains "via"/"con") must pick out exactly the
// connector layers of the real interposer stackup -- the studio derives
// config.via_layers this same way, so an empty result would silently drop the
// M4<->M5 pillars.
TEST(ViaCutRendering, RealStackupClassifiesConnectors)
{
    LayerStackup sk;
    ASSERT_TRUE(sk.loadFromBlenderGDS(
        std::string(CONFIGS_DIR) + "/stackups/intm4tm2.yaml"));

    std::set<LayerKey> via_layers;
    for (const auto& elev : sk.sortedLayers()) {
        std::string ln = elev.name;
        std::transform(ln.begin(), ln.end(), ln.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (ln.find("via") != std::string::npos ||
            ln.find("con") != std::string::npos) {
            via_layers.insert(elev.key());
        }
    }
    // Via4 (66/0), TopVia1 (125/0), TopVia2 (133/0).
    EXPECT_EQ(via_layers.size(), 3u);
    EXPECT_TRUE(via_layers.count(LayerKey(66, 0)));
    EXPECT_TRUE(via_layers.count(LayerKey(125, 0)));
    EXPECT_TRUE(via_layers.count(LayerKey(133, 0)));
}

}  // namespace
}  // namespace chiplet

#endif  // HAVE_KLAYOUT
