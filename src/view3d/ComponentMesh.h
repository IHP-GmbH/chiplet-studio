/**
 * ComponentMesh.h - GPU-ready mesh data for 3D rendering
 *
 * Supports both single-instance and instanced rendering for performance.
 * Instanced rendering allows drawing multiple copies of the same geometry
 * with different transforms/colors in a single draw call.
 */

#ifndef CHIPLET_VIEW3D_COMPONENTMESH_H
#define CHIPLET_VIEW3D_COMPONENTMESH_H

#include <QOpenGLExtraFunctions>
#include <QColor>
#include <QMatrix4x4>
#include <QVector4D>
#include <vector>
#include "math/Maths.h"

namespace chiplet {

/**
 * Vertex data for rendering
 */
struct Vertex {
    float position[3];
    float normal[3];
};

/**
 * ComponentMesh holds GPU buffers for rendering a component.
 */
class ComponentMesh : protected QOpenGLExtraFunctions {
public:
    ComponentMesh();
    ~ComponentMesh();

    // Move semantics
    ComponentMesh(ComponentMesh&& other) noexcept;
    ComponentMesh& operator=(ComponentMesh&& other) noexcept;

    // Non-copyable
    ComponentMesh(const ComponentMesh&) = delete;
    ComponentMesh& operator=(const ComponentMesh&) = delete;

    // Set mesh data (call before upload)
    void setVertices(const std::vector<Vertex>& vertices);
    void setIndices(const std::vector<GLuint>& indices);

    // GPU operations
    void upload();
    void render();
    void release();

    // Check if mesh is ready
    bool isUploaded() const { return m_vao != 0; }
    bool hasData() const { return !m_vertices.empty(); }

    // Debug getters
    size_t vertexCount() const { return m_vertices.size(); }
    size_t indexCount() const { return m_indices.size(); }
    GLuint vao() const { return m_vao; }

    // Bounding box
    const AA_BOUNDING_BOX& boundingBox() const { return m_boundingBox; }
    void setBoundingBox(const AA_BOUNDING_BOX& box) { m_boundingBox = box; }

    // Visual properties
    void setColor(const QColor& color) { m_color = color; }
    QColor color() const { return m_color; }

    void setSelected(bool selected) { m_selected = selected; }
    bool isSelected() const { return m_selected; }

private:
    void calculateBoundingBox();

    std::vector<Vertex> m_vertices;
    std::vector<GLuint> m_indices;
    AA_BOUNDING_BOX m_boundingBox;
    QColor m_color;
    bool m_selected = false;

    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_ebo = 0;
    bool m_initialized = false;
};

} // namespace chiplet

#endif // CHIPLET_VIEW3D_COMPONENTMESH_H
