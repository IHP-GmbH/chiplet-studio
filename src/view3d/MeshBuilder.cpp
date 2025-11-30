/**
 * MeshBuilder.cpp - Geometry generation implementation
 */

#include "MeshBuilder.h"
#include <cmath>

namespace chiplet {

ComponentMesh MeshBuilder::buildComponentMesh(const Component& comp)
{
    const auto& dims = comp.dimensions();
    const auto& pos = comp.position();

    // Convert micrometers to mm for reasonable scale
    float w = static_cast<float>(dims.width / 1000.0);
    float h = static_cast<float>(dims.height / 1000.0);
    float d = static_cast<float>(dims.thickness / 1000.0);
    float x = static_cast<float>(pos.x / 1000.0);
    float y = static_cast<float>(pos.y / 1000.0);
    float z = static_cast<float>(pos.z / 1000.0);

    ComponentMesh mesh = buildBox(w, h, d, x, y, z);
    mesh.setColor(colorForComponentType(comp.type()));

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
    // Bottom is at offsetZ, top is at offsetZ + depth
    std::vector<Vertex> vertices = {
        // Front face (normal: +Y)
        {{offsetX - hw, offsetY + hh, offsetZ + depth}, {0, 1, 0}},
        {{offsetX + hw, offsetY + hh, offsetZ + depth}, {0, 1, 0}},
        {{offsetX + hw, offsetY + hh, offsetZ}, {0, 1, 0}},
        {{offsetX - hw, offsetY + hh, offsetZ}, {0, 1, 0}},

        // Back face (normal: -Y)
        {{offsetX + hw, offsetY - hh, offsetZ + depth}, {0, -1, 0}},
        {{offsetX - hw, offsetY - hh, offsetZ + depth}, {0, -1, 0}},
        {{offsetX - hw, offsetY - hh, offsetZ}, {0, -1, 0}},
        {{offsetX + hw, offsetY - hh, offsetZ}, {0, -1, 0}},

        // Top face (normal: +Z)
        {{offsetX - hw, offsetY - hh, offsetZ + depth}, {0, 0, 1}},
        {{offsetX + hw, offsetY - hh, offsetZ + depth}, {0, 0, 1}},
        {{offsetX + hw, offsetY + hh, offsetZ + depth}, {0, 0, 1}},
        {{offsetX - hw, offsetY + hh, offsetZ + depth}, {0, 0, 1}},

        // Bottom face (normal: -Z)
        {{offsetX - hw, offsetY + hh, offsetZ}, {0, 0, -1}},
        {{offsetX + hw, offsetY + hh, offsetZ}, {0, 0, -1}},
        {{offsetX + hw, offsetY - hh, offsetZ}, {0, 0, -1}},
        {{offsetX - hw, offsetY - hh, offsetZ}, {0, 0, -1}},

        // Right face (normal: +X)
        {{offsetX + hw, offsetY - hh, offsetZ + depth}, {1, 0, 0}},
        {{offsetX + hw, offsetY + hh, offsetZ + depth}, {1, 0, 0}},
        {{offsetX + hw, offsetY + hh, offsetZ}, {1, 0, 0}},
        {{offsetX + hw, offsetY - hh, offsetZ}, {1, 0, 0}},

        // Left face (normal: -X)
        {{offsetX - hw, offsetY + hh, offsetZ + depth}, {-1, 0, 0}},
        {{offsetX - hw, offsetY - hh, offsetZ + depth}, {-1, 0, 0}},
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

ComponentMesh MeshBuilder::buildGridMesh(float size, float spacing)
{
    ComponentMesh mesh;
    std::vector<Vertex> vertices;
    std::vector<GLuint> indices;

    float halfSize = size * 0.5f;
    int numLines = static_cast<int>(size / spacing) + 1;
    GLuint idx = 0;

    // Grid lines parallel to X axis
    for (int i = 0; i < numLines; ++i) {
        float z = -halfSize + i * spacing;

        vertices.push_back({{-halfSize, 0, z}, {0, 1, 0}});
        vertices.push_back({{halfSize, 0, z}, {0, 1, 0}});

        indices.push_back(idx++);
        indices.push_back(idx++);
    }

    // Grid lines parallel to Z axis
    for (int i = 0; i < numLines; ++i) {
        float x = -halfSize + i * spacing;

        vertices.push_back({{x, 0, -halfSize}, {0, 1, 0}});
        vertices.push_back({{x, 0, halfSize}, {0, 1, 0}});

        indices.push_back(idx++);
        indices.push_back(idx++);
    }

    mesh.setVertices(vertices);
    mesh.setIndices(indices);
    mesh.setColor(QColor(100, 100, 100, 100));

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

} // namespace chiplet
