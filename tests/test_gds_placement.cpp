// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later
#include <gtest/gtest.h>
#include "view3d/GdsPlacement.h"
#include "view3d/MeshBuilder.h"

namespace chiplet {
namespace {

// Exercise the box vertices consumed by the renderer, without an upload/GL context.
AA_BOUNDING_BOX placedBox(const GDSBoundingBox& bbox, double x, double y,
                         Anchor anchor, Orientation orientation)
{
    Position3D position;
    position.x = x; position.y = y; position.z = 0;
    const auto center = gdsBoxCenter(bbox, position, anchor, orientation);
    auto mesh = MeshBuilder::buildBox(bbox.width() / 1000.0f, 0.2f,
        bbox.height() / 1000.0f, center.x, center.y,
        center.z + bbox.height() / 2000.0f);
    return mesh.boundingBox();
}

void expectFloor(const AA_BOUNDING_BOX& bounds, float x0, float x1, float z0, float z1)
{
    EXPECT_NEAR(bounds.mins.x, x0, 1e-6f);
    EXPECT_NEAR(bounds.maxes.x, x1, 1e-6f);
    EXPECT_NEAR(bounds.mins.z, z0, 1e-6f);
    EXPECT_NEAR(bounds.maxes.z, z1, 1e-6f);
}

TEST(GdsPlacement, NonzeroOriginFaceUp)
{
    expectFloor(placedBox({100, 200, 500, 800}, 1000, 2000,
        Anchor::GdsOrigin, Orientation::FaceUp), 1.1f, 1.5f, -2.8f, -2.2f);
}

TEST(GdsPlacement, NonzeroOriginFaceDown)
{
    expectFloor(placedBox({100, 200, 500, 800}, 1000, 2000,
        Anchor::GdsOrigin, Orientation::FaceDown), .5f, .9f, -2.8f, -2.2f);
}

TEST(GdsPlacement, BboxCenterBothOrientations)
{
    for (const auto orientation : {Orientation::FaceUp, Orientation::FaceDown}) {
        expectFloor(placedBox({100, 200, 500, 800}, 1000, 2000,
            Anchor::BboxCenter, orientation), .8f, 1.2f, -2.3f, -1.7f);
    }
}

TEST(GdsPlacement, R3DiesContainedInCarrier)
{
    auto carrier = placedBox({0, 0, 5000, 3200}, 2500, 1600,
                            Anchor::BboxCenter, Orientation::FaceUp);
    const auto driver = placedBox({0, 0, 2000, 2000}, 4500, 600,
                                 Anchor::GdsOrigin, Orientation::FaceDown);
    const auto echo = placedBox({0, 0, 1850, 1850}, 2225, 675,
                               Anchor::GdsOrigin, Orientation::FaceDown);
    expectFloor(driver, 2.5f, 4.5f, -2.6f, -.6f);
    expectFloor(echo, .375f, 2.225f, -2.525f, -.675f);
    carrier.AddBounds(driver);
    carrier.AddBounds(echo);
    expectFloor(carrier, 0, 5, -3.2f, 0);
}

// An exported draft gives a flip-chip die `anchor: gds_origin` and, as its
// position, the GDS origin of the mirrored die: a corner, not the centre. U1's
// 1000 x 1000 um bbox from (0,0), at (1500, 1700), spans x 500..1500 and
// y 1700..2700 (centre 1000, 2200, as the authored design places it); a box
// centred on the position would land at (1500, 1700) instead.
TEST(GdsPlacement, ExportedDraftFlipChipU1)
{
    expectFloor(placedBox({0, 0, 1000, 1000}, 1500, 1700,
        Anchor::GdsOrigin, Orientation::FaceDown), .5f, 1.5f, -2.7f, -1.7f);
}

TEST(GdsPlacement, DetailedFitTransformsReflectionBeforeTakingExtrema)
{
    AA_BOUNDING_BOX local;
    local.SetFromMinsMaxes(VECTOR3D(.1f, .02f, -.8f), VECTOR3D(.5f, .04f, -.2f));
    QMatrix4x4 transform;
    transform.translate(1.0f, .3f, -2.0f);
    transform.scale(-1.0f, 1.0f, 1.0f);
    const auto world = transformedMeshBounds(local, transform);
    expectFloor(world, .5f, .9f, -2.8f, -2.2f);
    EXPECT_NEAR(world.mins.y, .32f, 1e-6f);
    EXPECT_NEAR(world.maxes.y, .34f, 1e-6f);
}

} // namespace
} // namespace chiplet
