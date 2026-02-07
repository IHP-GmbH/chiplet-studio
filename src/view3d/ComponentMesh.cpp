/**
 * ComponentMesh.cpp - GPU mesh implementation
 */

#include "ComponentMesh.h"
#include <QOpenGLContext>
#include <QDebug>

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
    , m_initialized(false)  // Force re-initialization of GL functions after move
    , m_instances(std::move(other.m_instances))
    , m_instanceVBO(other.m_instanceVBO)
    , m_instanceCount(other.m_instanceCount)
{
    other.m_vao = 0;
    other.m_vbo = 0;
    other.m_ebo = 0;
    other.m_instanceVBO = 0;
    other.m_instanceCount = 0;
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
        m_initialized = false;  // Force re-initialization of GL functions after move
        m_instances = std::move(other.m_instances);
        m_instanceVBO = other.m_instanceVBO;
        m_instanceCount = other.m_instanceCount;

        other.m_vao = 0;
        other.m_vbo = 0;
        other.m_ebo = 0;
        other.m_instanceVBO = 0;
        other.m_instanceCount = 0;
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

    // Verify OpenGL context is available before any GL operations
    QOpenGLContext* context = QOpenGLContext::currentContext();
    if (!context) {
        qCritical() << "ComponentMesh::upload: No OpenGL context available!";
        return;
    }

    if (!context->isValid()) {
        qCritical() << "ComponentMesh::upload: OpenGL context is invalid!";
        return;
    }

    if (!m_initialized) {
        initializeOpenGLFunctions();
        m_initialized = true;
    }

    if (!m_initialized) {
        qWarning() << "ComponentMesh::upload: Failed to initialize OpenGL functions";
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

    // Verify OpenGL context is available
    QOpenGLContext* context = QOpenGLContext::currentContext();
    if (!context || !context->isValid()) {
        return;
    }

    // Lazy initialization of GL functions (needed after move)
    if (!m_initialized) {
        initializeOpenGLFunctions();
        m_initialized = true;
    }

    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_indices.size()),
                   GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void ComponentMesh::release()
{
    // Verify OpenGL context is available before deleting GL resources
    QOpenGLContext* context = QOpenGLContext::currentContext();
    if (!context || !context->isValid()) {
        // Reset handles without calling GL functions
        m_vao = 0;
        m_vbo = 0;
        m_ebo = 0;
        m_instanceVBO = 0;
        m_instanceCount = 0;
        m_initialized = false;
        return;
    }

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
    if (m_instanceVBO != 0) {
        glDeleteBuffers(1, &m_instanceVBO);
        m_instanceVBO = 0;
    }
    m_instanceCount = 0;
}

void ComponentMesh::setInstanceData(const std::vector<InstanceData>& instances)
{
    m_instances = instances;
    m_instanceCount = static_cast<int>(instances.size());
}

void ComponentMesh::uploadInstanceData()
{
    if (!m_initialized || m_vao == 0 || m_instances.empty()) {
        return;
    }

    // Verify OpenGL context is available
    QOpenGLContext* context = QOpenGLContext::currentContext();
    if (!context || !context->isValid()) {
        qCritical() << "ComponentMesh::uploadInstanceData: No valid OpenGL context!";
        return;
    }

    glBindVertexArray(m_vao);

    // Create instance VBO if needed
    if (m_instanceVBO == 0) {
        glGenBuffers(1, &m_instanceVBO);
    }

    // Upload instance data
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);
    glBufferData(GL_ARRAY_BUFFER,
                 m_instances.size() * sizeof(InstanceData),
                 m_instances.data(),
                 GL_DYNAMIC_DRAW);

    setupInstanceAttributes();

    glBindVertexArray(0);
}

void ComponentMesh::setupInstanceAttributes()
{
    // Instance data layout (96 bytes per instance):
    // - modelMatrix: 4 vec4s at locations 2,3,4,5 (64 bytes)
    // - color: vec4 at location 6 (16 bytes)
    // - selected: float at location 7 (4 bytes)
    // - padding: 12 bytes

    const GLsizei stride = sizeof(InstanceData);

    // Model matrix (4 vec4 attributes for mat4)
    for (int i = 0; i < 4; ++i) {
        GLuint loc = 2 + i;
        glEnableVertexAttribArray(loc);
        glVertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<void*>(i * 4 * sizeof(float)));
        glVertexAttribDivisor(loc, 1);  // Per-instance
    }

    // Color (vec4)
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offsetof(InstanceData, color)));
    glVertexAttribDivisor(6, 1);  // Per-instance

    // Selected (float)
    glEnableVertexAttribArray(7);
    glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(offsetof(InstanceData, selected)));
    glVertexAttribDivisor(7, 1);  // Per-instance
}

void ComponentMesh::renderInstanced()
{
    if (m_vao == 0 || m_indices.empty() || m_instanceCount == 0) {
        return;
    }

    // Verify OpenGL context is available
    QOpenGLContext* context = QOpenGLContext::currentContext();
    if (!context || !context->isValid()) {
        return;
    }

    // Lazy initialization of GL functions (needed after move)
    if (!m_initialized) {
        initializeOpenGLFunctions();
        m_initialized = true;
    }

    glBindVertexArray(m_vao);

    // Always use instanced draw - even for 1 instance, because the shader
    // expects instance attributes (transform, color) at locations 2-7
    glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(m_indices.size()),
                            GL_UNSIGNED_INT, nullptr, m_instanceCount);

    glBindVertexArray(0);
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
