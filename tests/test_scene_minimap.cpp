// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

// Unit tests for the pure floor-plan math behind the 3D top-down mini-map
// (SceneOverview.h). The widget (SceneMiniMap) and the AssemblyView extraction
// are display-only; the correctness that matters, the scene<->floor mapping and
// the camera marker, lives in these header-only functions and is tested here.

#include <gtest/gtest.h>
#include <cmath>

#include "view3d/SceneOverview.h"

using namespace chiplet;

TEST(SceneOverview, FloorConventionFlipsZ) {
    const FloorPoint f = floorFromScene(12.0, 34.0);
    EXPECT_DOUBLE_EQ(f.x, 12.0);
    EXPECT_DOUBLE_EQ(f.y, -34.0);
}

TEST(SceneOverview, SceneFloorRoundTrip) {
    double sx = 0.0, sz = 0.0;
    const FloorPoint f = floorFromScene(-7.5, 21.0);
    sceneFromFloor(f.x, f.y, sx, sz);
    EXPECT_DOUBLE_EQ(sx, -7.5);
    EXPECT_DOUBLE_EQ(sz, 21.0);
}

TEST(SceneOverview, FloorRectNormalizedFromSceneXZ) {
    // scene X in [-2, 8], scene Z in [3, 11] -> floor Y in [-11, -3].
    const ViewBox b = floorRectFromSceneXZ(-2.0, 3.0, 8.0, 11.0);
    EXPECT_TRUE(b.valid());
    EXPECT_DOUBLE_EQ(b.xmin, -2.0);
    EXPECT_DOUBLE_EQ(b.xmax, 8.0);
    EXPECT_DOUBLE_EQ(b.ymin, -11.0);
    EXPECT_DOUBLE_EQ(b.ymax, -3.0);
    EXPECT_DOUBLE_EQ(b.width(), 10.0);
    EXPECT_DOUBLE_EQ(b.height(), 8.0);
}

TEST(SceneOverview, CameraMarkerTargetMapsToFloor) {
    const OverviewCameraMarker m =
        computeCameraMarker(10.0, 20.0, 1000.0, 45.0, 0.0, 89.0, 1.5);
    EXPECT_TRUE(m.valid);
    EXPECT_DOUBLE_EQ(m.x, 10.0);
    EXPECT_DOUBLE_EQ(m.y, -20.0);
}

TEST(SceneOverview, CameraMarkerBoxScalesWithDistanceAndAspect) {
    const double fov = 45.0;
    const double aspect = 2.0;
    const OverviewCameraMarker near =
        computeCameraMarker(0.0, 0.0, 500.0, fov, 0.0, 60.0, aspect);
    const OverviewCameraMarker far =
        computeCameraMarker(0.0, 0.0, 2000.0, fov, 0.0, 60.0, aspect);

    const double expectedNearH = 500.0 * std::tan(fov * M_PI / 180.0 * 0.5);
    EXPECT_NEAR(near.halfH, expectedNearH, 1e-6);
    EXPECT_NEAR(near.halfW, expectedNearH * aspect, 1e-6);
    // Zooming out (larger distance) grows the visible box.
    EXPECT_GT(far.halfH, near.halfH);
    EXPECT_NEAR(far.halfH / near.halfH, 4.0, 1e-6);
}

TEST(SceneOverview, CameraMarkerNonPositiveAspectTreatedAsSquare) {
    const OverviewCameraMarker m =
        computeCameraMarker(0.0, 0.0, 1000.0, 45.0, 0.0, 60.0, 0.0);
    EXPECT_NEAR(m.halfW, m.halfH, 1e-9);
}

TEST(SceneOverview, CameraMarkerTopDownHasNoHeading) {
    // pitch == 90 -> straight down -> the floor heading is undefined.
    const OverviewCameraMarker m =
        computeCameraMarker(0.0, 0.0, 1000.0, 45.0, 30.0, 90.0, 1.0);
    EXPECT_FALSE(m.dirValid);
}

TEST(SceneOverview, CameraMarkerTiltedHeadingIsUnitAndConsistent) {
    // yaw 0, tilted: camera sits toward +Z and looks toward -Z, which is +floorY
    // (up on the mini-map). Heading must be a unit vector.
    const OverviewCameraMarker m =
        computeCameraMarker(0.0, 0.0, 1000.0, 45.0, 0.0, 45.0, 1.0);
    ASSERT_TRUE(m.dirValid);
    EXPECT_NEAR(m.dirX, 0.0, 1e-6);
    EXPECT_NEAR(m.dirY, 1.0, 1e-6);
    EXPECT_NEAR(std::sqrt(m.dirX * m.dirX + m.dirY * m.dirY), 1.0, 1e-6);
}

TEST(SceneOverview, SceneOverviewValidTracksFloorBounds) {
    SceneOverview ov;
    EXPECT_FALSE(ov.valid());
    ov.floorBounds = floorRectFromSceneXZ(0.0, 0.0, 5.0, 5.0);
    EXPECT_TRUE(ov.valid());
}
