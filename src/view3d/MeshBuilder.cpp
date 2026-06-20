// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * MeshBuilder.cpp - Geometry generation implementation
 */

#include "MeshBuilder.h"
#include "CoordFrame.h"
#include <cmath>

namespace chiplet {

// Get default dimensions for component type (in micrometers)
static Dimensions3D getDefaultDimensions(ComponentType type)
{
    Dimensions3D dims;
    switch (type) {
        case ComponentType::Die:
        case ComponentType::DieArray:
            dims.width = 2000.0;     // 2mm
            dims.height = 2000.0;    // 2mm
            dims.thickness = 200.0;  // 200um
            break;
        case ComponentType::Interposer:
            dims.width = 8000.0;     // 8mm
            dims.height = 8000.0;    // 8mm
            dims.thickness = 100.0;  // 100um
            break;
        case ComponentType::Substrate:
            dims.width = 10000.0;    // 10mm
            dims.height = 10000.0;   // 10mm
            dims.thickness = 500.0;  // 500um
            break;
    }
    return dims;
}

ComponentMesh MeshBuilder::buildComponentMesh(const Component& comp,
                                               const LayerPropertiesFile* lyp)
{
    auto dims = comp.dimensions();
    const auto& pos = comp.position();

    // Use default dimensions if not specified
    if (dims.width <= 0 || dims.height <= 0 || dims.thickness <= 0) {
        dims = getDefaultDimensions(comp.type());
    }

    // Convert micrometers to mm for reasonable scale
    // Coordinate mapping: chiplet X -> 3D X, chiplet Y -> 3D Z, chiplet Z (elevation) -> 3D Y
    // This aligns the EDA X-Y plane with the 3D X-Z plane (horizontal)
    // and maps layer elevation to the Y axis (vertical, matching camera up vector)
    float w = static_cast<float>(dims.width / 1000.0);
    float h = static_cast<float>(dims.height / 1000.0);
    float d = static_cast<float>(dims.thickness / 1000.0);
    const ScenePosition scenePos = sceneFromChiplet(pos);
    float x = scenePos.x;
    float y = scenePos.y;   // Chiplet Z elevation -> 3D Y (vertical)
    float z = scenePos.z;   // Chiplet Y -> 3D -Z (horizontal, negated for correct orientation)

    // Build box with dimensions: (width, thickness, height) for (3D X, 3D Y, 3D Z)
    // w = chiplet width -> 3D X, d = chiplet thickness -> 3D Y (vertical), h = chiplet height -> 3D Z
    ComponentMesh mesh = buildBox(w, d, h, x, y, z);
    mesh.setColor(colorForComponent(comp, lyp));

    return mesh;
}

ComponentMesh MeshBuilder::buildComponentMeshAtOrigin(const Component& comp,
                                                       const LayerPropertiesFile* lyp)
{
    auto dims = comp.dimensions();

    // Use default dimensions if not specified
    if (dims.width <= 0 || dims.height <= 0 || dims.thickness <= 0) {
        dims = getDefaultDimensions(comp.type());
    }

    // Convert micrometers to mm for reasonable scale
    // Position is NOT included - mesh is at origin for instanced rendering
    // Coordinate mapping: chiplet X -> 3D X, chiplet Y -> 3D Z, chiplet Z (elevation) -> 3D Y
    float w = static_cast<float>(dims.width / 1000.0);
    float h = static_cast<float>(dims.height / 1000.0);
    float d = static_cast<float>(dims.thickness / 1000.0);

    // Build box with dimension swap: (width, thickness, height) for (3D X, 3D Y, 3D Z)
    ComponentMesh mesh = buildBox(w, d, h, 0, 0, 0);
    mesh.setColor(colorForComponent(comp, lyp));

    return mesh;
}

ComponentMesh MeshBuilder::buildBox(float width, float height, float depth,
                                     float offsetX, float offsetY, float offsetZ)
{
    ComponentMesh mesh;

    // Half dimensions for centered box
    float hw = width * 0.5f;
    float hh = height * 0.5f;

    // Box vertices with normals
    // Box spans [offsetZ - depth, offsetZ] in Z (depth extends in -Z direction)
    std::vector<Vertex> vertices = {
        // Front face (normal: +Y)
        {{offsetX - hw, offsetY + hh, offsetZ - depth}, {0, 1, 0}},
        {{offsetX + hw, offsetY + hh, offsetZ - depth}, {0, 1, 0}},
        {{offsetX + hw, offsetY + hh, offsetZ}, {0, 1, 0}},
        {{offsetX - hw, offsetY + hh, offsetZ}, {0, 1, 0}},

        // Back face (normal: -Y)
        {{offsetX + hw, offsetY - hh, offsetZ - depth}, {0, -1, 0}},
        {{offsetX - hw, offsetY - hh, offsetZ - depth}, {0, -1, 0}},
        {{offsetX - hw, offsetY - hh, offsetZ}, {0, -1, 0}},
        {{offsetX + hw, offsetY - hh, offsetZ}, {0, -1, 0}},

        // Top face (normal: -Z) - faces outward in -Z direction
        {{offsetX - hw, offsetY - hh, offsetZ - depth}, {0, 0, -1}},
        {{offsetX + hw, offsetY - hh, offsetZ - depth}, {0, 0, -1}},
        {{offsetX + hw, offsetY + hh, offsetZ - depth}, {0, 0, -1}},
        {{offsetX - hw, offsetY + hh, offsetZ - depth}, {0, 0, -1}},

        // Bottom face (normal: +Z) - faces outward in +Z direction
        {{offsetX - hw, offsetY + hh, offsetZ}, {0, 0, 1}},
        {{offsetX + hw, offsetY + hh, offsetZ}, {0, 0, 1}},
        {{offsetX + hw, offsetY - hh, offsetZ}, {0, 0, 1}},
        {{offsetX - hw, offsetY - hh, offsetZ}, {0, 0, 1}},

        // Right face (normal: +X)
        {{offsetX + hw, offsetY - hh, offsetZ - depth}, {1, 0, 0}},
        {{offsetX + hw, offsetY + hh, offsetZ - depth}, {1, 0, 0}},
        {{offsetX + hw, offsetY + hh, offsetZ}, {1, 0, 0}},
        {{offsetX + hw, offsetY - hh, offsetZ}, {1, 0, 0}},

        // Left face (normal: -X)
        {{offsetX - hw, offsetY + hh, offsetZ - depth}, {-1, 0, 0}},
        {{offsetX - hw, offsetY - hh, offsetZ - depth}, {-1, 0, 0}},
        {{offsetX - hw, offsetY - hh, offsetZ}, {-1, 0, 0}},
        {{offsetX - hw, offsetY + hh, offsetZ}, {-1, 0, 0}},
    };

    // Indices for 12 triangles (6 faces, 2 triangles each)
    std::vector<GLuint> indices = {
        0, 1, 2, 0, 2, 3,       // Front
        4, 5, 6, 4, 6, 7,       // Back
        8, 9, 10, 8, 10, 11,    // Top
        12, 13, 14, 12, 14, 15, // Bottom
        16, 17, 18, 16, 18, 19, // Right
        20, 21, 22, 20, 22, 23  // Left
    };

    mesh.setVertices(vertices);
    mesh.setIndices(indices);

    return mesh;
}

ComponentMesh MeshBuilder::buildPlaneMesh(float size, float centerX, float centerZ, float y)
{
    ComponentMesh mesh;
    float halfSize = size * 0.5f;

    // Calculate vertex positions centered at (centerX, y, centerZ)
    float x0 = centerX - halfSize;
    float x1 = centerX + halfSize;
    float z0 = centerZ - halfSize;
    float z1 = centerZ + halfSize;

    // Quad on XZ plane at specified Y position
    // Normal pointing up (+Y)
    std::vector<Vertex> vertices = {
        {{x0, y, z0}, {0, 1, 0}},  // Bottom-left
        {{x1, y, z0}, {0, 1, 0}},  // Bottom-right
        {{x1, y, z1}, {0, 1, 0}},  // Top-right
        {{x0, y, z1}, {0, 1, 0}},  // Top-left
    };

    // Two triangles forming a quad
    std::vector<GLuint> indices = {
        0, 1, 2,  // First triangle
        0, 2, 3   // Second triangle
    };

    mesh.setVertices(vertices);
    mesh.setIndices(indices);
    mesh.setColor(QColor(230, 230, 230, 255));  // Light gray/white

    return mesh;
}

QColor MeshBuilder::colorForComponentType(ComponentType type)
{
    switch (type) {
        case ComponentType::Die:
            return QColor(51, 102, 204);    // Blue
        case ComponentType::DieArray:
            return QColor(102, 153, 230);   // Light Blue
        case ComponentType::Interposer:
            return QColor(51, 178, 77);     // Green
        case ComponentType::Substrate:
            return QColor(153, 102, 51);    // Brown
        default:
            return QColor(128, 128, 128);   // Gray
    }
}

QColor MeshBuilder::colorForComponent(const Component& comp,
                                       const LayerPropertiesFile* lyp)
{
    // Substrate always uses fixed color (no .lyp)
    if (comp.type() == ComponentType::Substrate) {
        return colorForComponentType(ComponentType::Substrate);
    }

    // If no .lyp provided, use fallback colors
    if (!lyp) {
        return colorForComponentType(comp.type());
    }

    // Look up TopMetal2 color from .lyp
    const LayerStyle* style = lyp->find(kTopMetal2Layer, kTopMetal2Datatype);
    if (style) {
        return toQColor(style->fill_color);
    }

    // Fallback to type-based color
    return colorForComponentType(comp.type());
}

QColor MeshBuilder::toQColor(const LayerColor& lc)
{
    return QColor(lc.r, lc.g, lc.b, lc.a);
}

} // namespace chiplet
