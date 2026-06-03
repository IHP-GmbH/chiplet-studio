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
    // Generate box mesh for a component
    // If lyp is provided, uses layer colors from .lyp file
    static ComponentMesh buildComponentMesh(const Component& comp,
                                            const LayerPropertiesFile* lyp = nullptr);

    // Generate box mesh for a component at origin (for instanced rendering)
    // The position is NOT baked into geometry - use instance transform instead
    static ComponentMesh buildComponentMeshAtOrigin(const Component& comp,
                                                     const LayerPropertiesFile* lyp = nullptr);

    // Generate a simple box mesh with given dimensions
    static ComponentMesh buildBox(float width, float height, float depth,
                                  float offsetX = 0, float offsetY = 0, float offsetZ = 0);

    // Generate a reference grid on the XZ plane (lines)
    static ComponentMesh buildGridMesh(float size, float spacing);

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
