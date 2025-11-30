/**
 * ComponentMesh.cpp - GPU mesh implementation
 */

#include "ComponentMesh.h"

namespace chiplet {

ComponentMesh::ComponentMesh()
    : m_color(Qt::gray)
{
}

ComponentMesh::~ComponentMesh()
{
    release();
}

ComponentMesh::ComponentMesh(ComponentMesh&& other) noexcept
    : m_vertices(std::move(other.m_vertices))
    , m_indices(std::move(other.m_indices))
    , m_boundingBox(other.m_boundingBox)
    , m_color(other.m_color)
    , m_selected(other.m_selected)
    , m_vao(other.m_vao)
    , m_vbo(other.m_vbo)
    , m_ebo(other.m_ebo)
    , m_initialized(other.m_initialized)
{
    other.m_vao = 0;
    other.m_vbo = 0;
    other.m_ebo = 0;
    other.m_initialized = false;
}

ComponentMesh& ComponentMesh::operator=(ComponentMesh&& other) noexcept
{
    if (this != &other) {
        release();

        m_vertices = std::move(other.m_vertices);
        m_indices = std::move(other.m_indices);
        m_boundingBox = other.m_boundingBox;
        m_color = other.m_color;
        m_selected = other.m_selected;
        m_vao = other.m_vao;
        m_vbo = other.m_vbo;
        m_ebo = other.m_ebo;
        m_initialized = other.m_initialized;

        other.m_vao = 0;
        other.m_vbo = 0;
        other.m_ebo = 0;
        other.m_initialized = false;
    }
    return *this;
}

void ComponentMesh::setVertices(const std::vector<Vertex>& vertices)
{
    m_vertices = vertices;
    calculateBoundingBox();
}

void ComponentMesh::setIndices(const std::vector<GLuint>& indices)
{
    m_indices = indices;
}

void ComponentMesh::upload()
{
    if (m_vertices.empty()) {
        return;
    }

    if (!m_initialized) {
        initializeOpenGLFunctions();
        m_initialized = true;
    }

    if (!m_initialized) {
        return;
    }

    // Release existing buffers
    if (m_vao != 0) {
        release();
    }

    // Generate buffers
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);

    // Upload vertex data
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 m_vertices.size() * sizeof(Vertex),
                 m_vertices.data(),
                 GL_STATIC_DRAW);

    // Upload index data
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 m_indices.size() * sizeof(GLuint),
                 m_indices.data(),
                 GL_STATIC_DRAW);

    // Set up vertex attributes
    // Position attribute (location 0)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, position)));
    glEnableVertexAttribArray(0);

    // Normal attribute (location 1)
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, normal)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
}

void ComponentMesh::render()
{
    if (m_vao == 0 || m_indices.empty()) {
        return;
    }

    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_indices.size()),
                   GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void ComponentMesh::release()
{
    if (m_vao != 0) {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_vbo != 0) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_ebo != 0) {
        glDeleteBuffers(1, &m_ebo);
        m_ebo = 0;
    }
}

void ComponentMesh::calculateBoundingBox()
{
    if (m_vertices.empty()) {
        return;
    }

    VECTOR3D mins(m_vertices[0].position[0],
                  m_vertices[0].position[1],
                  m_vertices[0].position[2]);
    VECTOR3D maxes = mins;

    for (const auto& v : m_vertices) {
        if (v.position[0] < mins.x) mins.x = v.position[0];
        if (v.position[1] < mins.y) mins.y = v.position[1];
        if (v.position[2] < mins.z) mins.z = v.position[2];

        if (v.position[0] > maxes.x) maxes.x = v.position[0];
        if (v.position[1] > maxes.y) maxes.y = v.position[1];
        if (v.position[2] > maxes.z) maxes.z = v.position[2];
    }

    m_boundingBox.SetFromMinsMaxes(mins, maxes);
}

} // namespace chiplet
