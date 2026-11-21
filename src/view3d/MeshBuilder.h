// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * MeshBuilder.h - Geometry generation for 3D components
 */

#ifndef CHIPLET_VIEW3D_MESHBUILDER_H
#define CHIPLET_VIEW3D_MESHBUILDER_H

#include "ComponentMesh.h"
#include "core/Component.h"
#include "view2d/LayerProperties.h"
#include <QColor>

namespace chiplet {

/**
 * MeshBuilder generates renderable geometry from assembly components.
 */
class MeshBuilder {
public:
    // The dimensions (um) a component of `type` is drawn with when its own
    // are missing or not positive.
    static Dimensions3D defaultDimensions(ComponentType type);

    // A box `width` x `height` x `depth`, centred on offsetX and offsetY. In Z
    // it spans [offsetZ - depth, offsetZ]: offsetZ is its +Z face, not its
    // centre.
    static ComponentMesh buildBox(float width, float height, float depth,
                                  float offsetX = 0, float offsetY = 0, float offsetZ = 0);

    // Scene-Y (vertical) center to pass as buildBox's offsetY so a box is
    // anchored to the physical stack instead of centered on position.z.
    // Dies / die-arrays / substrate are BOTTOM-anchored (bottom face at
    // position.z, box grows upward). An interposer that declares an
    // attachment_surface_z is TOP-anchored (top face at that surface, body
    // hangs downward); a legacy interposer without it falls back to bottom.
    //   baseSceneY = sceneFromChiplet(pos).y  (== position.z in mm)
    //   depthMm    = dimensions.thickness in mm (buildBox's 2nd argument)
    static float anchoredOffsetY(const Component& comp,
                                 float baseSceneY, float depthMm);

    // Generate a solid plane on the XZ plane (for substrate/base)
    // With optional center position and Y offset
    static ComponentMesh buildPlaneMesh(float size,
                                         float centerX = 0.0f,
                                         float centerZ = 0.0f,
                                         float y = 0.0f);

    // Get color for component based on type and optional .lyp
    static QColor colorForComponent(const Component& comp,
                                    const LayerPropertiesFile* lyp = nullptr);

    // Get fallback color for component type (when no .lyp available)
    static QColor colorForComponentType(ComponentType type);

    // Convert LayerColor to QColor
    static QColor toQColor(const LayerColor& lc);

    // Layer keys for top metal (used for component colors)
    static constexpr int kTopMetal2Layer = 134;
    static constexpr int kTopMetal2Datatype = 0;
};

} // namespace chiplet

#endif // CHIPLET_VIEW3D_MESHBUILDER_H
