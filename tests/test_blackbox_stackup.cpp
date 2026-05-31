/**
 * test_blackbox_stackup.cpp - The 3D black-box augmentation premise.
 *
 * AssemblyView::buildLayerGeometry, when a component has no .lyp (a black-box /
 * closed-PDK chiplet), augments a local copy of the stackup with the GDS layers
 * actually present so LayerMeshBuilder renders them instead of skipping them.
 * This test pins the premise that augmentation relies on: a foreign pad layer
 * produces a 3D mesh iff it is present in the stackup.
 */

#include <gtest/gtest.h>

#include "view3d/LayerMeshBuilder.h"
#include "view3d/GDSLayerExtractor.h"
#include "core/LayerStackup.h"

using namespace chiplet;

namespace {

// One 60x60 um square pad on the given layer, the shape a closed-PDK chiplet
// GDS would carry with no .lyp to describe it.
std::map<LayerKey, LayerPolygons> onePadLayer(int layer, int datatype) {
    SimplePolygon sq;
    sq.points = {Point2D(0.0, 0.0), Point2D(60.0, 0.0),
                 Point2D(60.0, 60.0), Point2D(0.0, 60.0)};
    LayerPolygons lp;
    lp.key = LayerKey(layer, datatype);
    lp.name = "pad";
    lp.polygons.push_back(sq);
    std::map<LayerKey, LayerPolygons> m;
    m[lp.key] = lp;
    return m;
}

bool hasLayerMesh(const Component3DGeometry& g, int layer, int datatype) {
    for (const auto& lm : g.layers) {
        if (lm.key.layer == layer && lm.key.datatype == datatype) {
            return true;
        }
    }
    return false;
}

}  // namespace

// Baseline: a layer absent from the stackup is intentionally skipped (the guard
// AssemblyView's augmentation exists to defeat for the no-LYP case).
TEST(BlackBoxStackup, ForeignPadLayerSkippedWhenAbsentFromStackup) {
    auto polys = onePadLayer(205, 0);
    LayerStackup empty;  // models nothing
    LayerMeshBuilder builder;
    Component3DGeometry geo = builder.build(polys, empty);
    EXPECT_FALSE(hasLayerMesh(geo, 205, 0));
}

// Augmenting the stackup with the present layer (what buildLayerGeometry does
// when lyp == nullptr) makes the pad render.
TEST(BlackBoxStackup, ForeignPadLayerRendersWhenAugmented) {
    auto polys = onePadLayer(205, 0);
    LayerStackup stk;
    stk.addLayer(205, 0, 0.0, 1.0, "blackbox");  // the augmentation
    LayerMeshBuilder builder;
    Component3DGeometry geo = builder.build(polys, stk);
    ASSERT_TRUE(hasLayerMesh(geo, 205, 0))
        << "augmented pad layer must produce a mesh";
    EXPECT_GT(geo.totalTriangles(), 0u);
}

// The augmentation also covers arbitrary commercial layer numbers, not just the
// ADK canonical 205/0.
TEST(BlackBoxStackup, ArbitraryCommercialLayerRendersWhenAugmented) {
    auto polys = onePadLayer(77, 0);
    LayerStackup stk;
    stk.addLayer(77, 0, 0.0, 1.0, "blackbox");
    LayerMeshBuilder builder;
    Component3DGeometry geo = builder.build(polys, stk);
    EXPECT_TRUE(hasLayerMesh(geo, 77, 0));
}
