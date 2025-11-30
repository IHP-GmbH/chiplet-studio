/**
 * MeshBuilder.h - Geometry generation for 3D components
 */

#ifndef CHIPLET_VIEW3D_MESHBUILDER_H
#define CHIPLET_VIEW3D_MESHBUILDER_H

#include "ComponentMesh.h"
#include "core/Component.h"
#include <QColor>

namespace chiplet {

/**
 * MeshBuilder generates renderable geometry from assembly components.
 */
class MeshBuilder {
public:
    // Generate box mesh for a component
    static ComponentMesh buildComponentMesh(const Component& comp);

    // Generate a simple box mesh with given dimensions
    static ComponentMesh buildBox(float width, float height, float depth,
                                  float offsetX = 0, float offsetY = 0, float offsetZ = 0);

    // Generate a reference grid on the XZ plane
    static ComponentMesh buildGridMesh(float size, float spacing);

    // Get color for component type
    static QColor colorForComponentType(ComponentType type);
};

} // namespace chiplet

#endif // CHIPLET_VIEW3D_MESHBUILDER_H
