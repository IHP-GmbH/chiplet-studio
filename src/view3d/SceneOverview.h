// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * SceneOverview.h - Top-down floor-plan model for the 3D view mini-map.
 *
 * Pure value types + pure math (no Qt, no OpenGL) so the geometry that matters
 * (footprint projection, click-to-navigate mapping, camera marker) is unit
 * testable headless. The widget (SceneMiniMap) and the data extraction on
 * AssemblyView are the display-only halves, mirroring how OverviewTransform is
 * the tested core behind the 2D OverviewNavigator.
 *
 * Floor convention. The 3D scene is Y-up: chiplet X -> scene X, chiplet Y ->
 * scene -Z, chiplet Z (elevation) -> scene Y (see CoordFrame.h). A top-down
 * mini-map therefore projects onto the scene X-Z plane. To match the on-screen
 * orientation of the default (near top-down) 3D view, where screen-right is
 * scene +X and screen-up is scene -Z, we map:
 *
 *     floorX =  sceneX
 *     floorY = -sceneZ
 *
 * The mini-map then feeds these floor coordinates to OverviewTransform (which
 * adds its own aspect-preserving letterbox and pixel Y-flip). Because footprints,
 * the camera marker, and the click mapping all pass through the same floor
 * convention + transform, they can never drift relative to each other.
 */

#ifndef CHIPLET_VIEW3D_SCENEOVERVIEW_H
#define CHIPLET_VIEW3D_SCENEOVERVIEW_H

#include <cmath>
#include <string>
#include <vector>

#include "view2d/ViewBox.h"

namespace chiplet {

/// A point on the floor plane (mm), floor convention above.
struct FloorPoint {
    double x = 0.0;
    double y = 0.0;
};

/// scene X-Z -> floor point.
inline FloorPoint floorFromScene(double sceneX, double sceneZ) {
    return { sceneX, -sceneZ };
}

/// floor point -> scene X-Z (inverse of floorFromScene).
inline void sceneFromFloor(double floorX, double floorY, double& sceneX, double& sceneZ) {
    sceneX = floorX;
    sceneZ = -floorY;
}

/// A scene-space axis-aligned X-Z rectangle projected onto the floor. Y-flip
/// means scene [minZ,maxZ] -> floor [-maxZ,-minZ], so the result is normalized.
inline ViewBox floorRectFromSceneXZ(double minX, double minZ, double maxX, double maxZ) {
    return ViewBox{ minX, -maxZ, maxX, -minZ };
}

/// One component drawn on the mini-map: its floor footprint, fill color as a
/// packed 0xRRGGBB, and whether it is the currently selected component.
struct OverviewFootprint {
    std::string id;
    ViewBox     rect;              // floor coordinates
    unsigned int rgb = 0x808080u;  // 0xRRGGBB
    bool        selected = false;
};

/// Where the 3D camera is looking, expressed on the floor. `x,y` is the look-at
/// target; `halfW,halfH` is a coarse half-extent of the on-screen visible area
/// at the target plane (a zoom-level cue, exact only for a top-down untilted
/// view); `dirX,dirY` is the unit floor direction the camera looks toward, valid
/// only when the view is tilted enough to have a well-defined heading.
struct OverviewCameraMarker {
    double x = 0.0;
    double y = 0.0;
    double halfW = 0.0;
    double halfH = 0.0;
    double dirX = 0.0;
    double dirY = 0.0;
    bool   dirValid = false;
    bool   valid = false;
};

/// Complete top-down model the mini-map paints.
struct SceneOverview {
    ViewBox floorBounds;                       // full assembly floor extent
    std::vector<OverviewFootprint> footprints;
    OverviewCameraMarker camera;

    bool valid() const { return floorBounds.valid(); }
};

/**
 * Build the camera marker from orbit-camera parameters.
 *
 * @param targetSceneX,targetSceneZ  Look-at target (scene coords).
 * @param distance                   Camera distance to target (mm).
 * @param fovDeg                      Vertical field of view (degrees).
 * @param yawDeg,pitchDeg            Orbit angles (degrees); pitch 90 = straight down.
 * @param aspect                     Viewport width/height (for the horizontal extent).
 */
inline OverviewCameraMarker computeCameraMarker(double targetSceneX, double targetSceneZ,
                                                double distance, double fovDeg,
                                                double yawDeg, double pitchDeg,
                                                double aspect)
{
    OverviewCameraMarker m;
    const FloorPoint t = floorFromScene(targetSceneX, targetSceneZ);
    m.x = t.x;
    m.y = t.y;

    if (aspect <= 0.0) {
        aspect = 1.0;
    }

    // Half-height of the visible slab at the target plane. tan(fov/2)*distance is
    // the exact half-height only for an untilted view; here it is a stable,
    // always-finite proxy for the current zoom level.
    const double halfFov = fovDeg * M_PI / 180.0 * 0.5;
    const double half = distance * std::tan(halfFov);
    m.halfH = std::fabs(half);
    m.halfW = m.halfH * aspect;

    // Heading: the horizontal (X,Z) part of the camera offset from target is
    // distance*cos(pitch)*(sin(yaw), cos(yaw)); the camera looks back along it.
    const double cosPitch = std::cos(pitchDeg * M_PI / 180.0);
    const double offX = distance * cosPitch * std::sin(yawDeg * M_PI / 180.0);
    const double offZ = distance * cosPitch * std::cos(yawDeg * M_PI / 180.0);
    // Look direction (camera -> target) in scene X-Z is (-offX, -offZ); map to floor.
    const FloorPoint look = floorFromScene(-offX, -offZ);
    const double len = std::sqrt(look.x * look.x + look.y * look.y);
    if (len > 1e-6 * (std::fabs(distance) + 1.0)) {
        m.dirX = look.x / len;
        m.dirY = look.y / len;
        m.dirValid = true;
    }

    m.valid = true;
    return m;
}

} // namespace chiplet

#endif // CHIPLET_VIEW3D_SCENEOVERVIEW_H
