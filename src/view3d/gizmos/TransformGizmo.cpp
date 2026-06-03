// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * TransformGizmo.cpp - 3D transformation gizmo implementation
 */

#include "TransformGizmo.h"
#include <cmath>

namespace chiplet {

// Gizmo vertex shader - simple transform with vertex colors
static const char* gizmoVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 color;

uniform mat4 modelViewProjection;

out vec3 fragColor;

void main() {
    fragColor = color;
    gl_Position = modelViewProjection * vec4(position, 1.0);
}
)";

// Gizmo fragment shader - simple color output
static const char* gizmoFragmentShader = R"(
#version 330 core
in vec3 fragColor;

out vec4 FragColor;

void main() {
    FragColor = vec4(fragColor, 1.0);
}
)";

TransformGizmo::TransformGizmo()
    : m_position(0, 0, 0)
{
}

TransformGizmo::~TransformGizmo()
{
    release();
}

void TransformGizmo::initialize()
{
    if (m_initialized) {
        return;
    }

    initializeOpenGLFunctions();

    // Load shader
    if (!m_shader.loadFromSource(gizmoVertexShader, gizmoFragmentShader)) {
        qWarning() << "Failed to load gizmo shader";
        return;
    }

    // Build geometry
    buildAxisGeometry();
    uploadGeometry();

    m_initialized = true;
}

void TransformGizmo::release()
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
    m_initialized = false;
}

void TransformGizmo::setPosition(const QVector3D& pos)
{
    m_position = pos;
}

void TransformGizmo::setScale(float scale)
{
    m_scale = scale;
}

QVector3D TransformGizmo::axisDirection(GizmoAxis axis)
{
    switch (axis) {
        case GizmoAxis::X: return QVector3D(1, 0, 0);
        case GizmoAxis::Y: return QVector3D(0, 1, 0);
        case GizmoAxis::Z: return QVector3D(0, 0, 1);
        default: return QVector3D(0, 0, 0);
    }
}

QColor TransformGizmo::axisColor(GizmoAxis axis, bool highlighted)
{
    if (highlighted) {
        return QColor(255, 255, 0);  // Yellow for highlighted
    }

    switch (axis) {
        case GizmoAxis::X: return QColor(220, 50, 50);   // Red
        case GizmoAxis::Y: return QColor(50, 220, 50);   // Green
        case GizmoAxis::Z: return QColor(50, 100, 220);  // Blue
        default: return QColor(128, 128, 128);           // Gray
    }
}

void TransformGizmo::buildAxisGeometry()
{
    m_axisVertices.clear();
    m_axisIndices.clear();

    // Helper to add a vertex (position + color)
    auto addVertex = [this](float x, float y, float z, const QColor& c) {
        m_axisVertices.push_back(x);
        m_axisVertices.push_back(y);
        m_axisVertices.push_back(z);
        m_axisVertices.push_back(static_cast<float>(c.redF()));
        m_axisVertices.push_back(static_cast<float>(c.greenF()));
        m_axisVertices.push_back(static_cast<float>(c.blueF()));
    };

    // Build each axis as a cylinder + cone
    for (int axisIdx = 0; axisIdx < 3; ++axisIdx) {
        GizmoAxis axis = static_cast<GizmoAxis>(axisIdx + 1);  // X=1, Y=2, Z=3
        QColor color = axisColor(axis);
        QVector3D dir = axisDirection(axis);

        // Rotation matrix to align cylinder with axis
        QMatrix4x4 rotation;
        rotation.setToIdentity();
        if (axis == GizmoAxis::X) {
            rotation.rotate(90, 0, 0, 1);  // Rotate around Z to point along X
            rotation.rotate(90, 0, 1, 0);  // Additional rotation
        } else if (axis == GizmoAxis::Z) {
            rotation.rotate(-90, 1, 0, 0); // Rotate around X to point along Z
        }
        // Y axis is default (pointing up)

        GLuint baseIdx = static_cast<GLuint>(m_axisVertices.size() / 6);

        // Cylinder vertices
        float cylinderLength = AXIS_LENGTH - CONE_LENGTH;
        for (int i = 0; i <= SEGMENTS; ++i) {
            float angle = 2.0f * M_PI * i / SEGMENTS;
            float cx = AXIS_RADIUS * std::cos(angle);
            float cz = AXIS_RADIUS * std::sin(angle);

            // Transform by axis rotation
            QVector3D bottom(cx, 0, cz);
            QVector3D top(cx, cylinderLength, cz);

            if (axis == GizmoAxis::X) {
                bottom = QVector3D(0, cx, cz);
                top = QVector3D(cylinderLength, cx, cz);
            } else if (axis == GizmoAxis::Z) {
                bottom = QVector3D(cx, cz, 0);
                top = QVector3D(cx, cz, cylinderLength);
            }

            addVertex(bottom.x(), bottom.y(), bottom.z(), color);
            addVertex(top.x(), top.y(), top.z(), color);
        }

        // Cylinder indices (triangle strip converted to triangles)
        for (int i = 0; i < SEGMENTS; ++i) {
            GLuint v0 = baseIdx + i * 2;
            GLuint v1 = baseIdx + i * 2 + 1;
            GLuint v2 = baseIdx + (i + 1) * 2;
            GLuint v3 = baseIdx + (i + 1) * 2 + 1;

            m_axisIndices.push_back(v0);
            m_axisIndices.push_back(v1);
            m_axisIndices.push_back(v2);

            m_axisIndices.push_back(v2);
            m_axisIndices.push_back(v1);
            m_axisIndices.push_back(v3);
        }

        // Cone (arrow tip)
        GLuint coneBaseIdx = static_cast<GLuint>(m_axisVertices.size() / 6);

        // Cone base center
        QVector3D coneBase = dir * cylinderLength;
        addVertex(coneBase.x(), coneBase.y(), coneBase.z(), color);
        GLuint coneCenterIdx = coneBaseIdx;

        // Cone tip
        QVector3D coneTip = dir * AXIS_LENGTH;
        addVertex(coneTip.x(), coneTip.y(), coneTip.z(), color);
        GLuint coneTipIdx = coneBaseIdx + 1;

        // Cone base circle vertices
        for (int i = 0; i <= SEGMENTS; ++i) {
            float angle = 2.0f * M_PI * i / SEGMENTS;
            float cx = CONE_RADIUS * std::cos(angle);
            float cz = CONE_RADIUS * std::sin(angle);

            QVector3D baseVertex;
            if (axis == GizmoAxis::X) {
                baseVertex = QVector3D(cylinderLength, cx, cz);
            } else if (axis == GizmoAxis::Y) {
                baseVertex = QVector3D(cx, cylinderLength, cz);
            } else {
                baseVertex = QVector3D(cx, cz, cylinderLength);
            }

            addVertex(baseVertex.x(), baseVertex.y(), baseVertex.z(), color);
        }

        // Cone base triangles (fan from center)
        for (int i = 0; i < SEGMENTS; ++i) {
            GLuint v0 = coneCenterIdx;
            GLuint v1 = coneBaseIdx + 2 + i;
            GLuint v2 = coneBaseIdx + 2 + i + 1;

            m_axisIndices.push_back(v0);
            m_axisIndices.push_back(v2);
            m_axisIndices.push_back(v1);
        }

        // Cone side triangles (fan from tip)
        for (int i = 0; i < SEGMENTS; ++i) {
            GLuint v0 = coneTipIdx;
            GLuint v1 = coneBaseIdx + 2 + i;
            GLuint v2 = coneBaseIdx + 2 + i + 1;

            m_axisIndices.push_back(v0);
            m_axisIndices.push_back(v1);
            m_axisIndices.push_back(v2);
        }
    }
}

void TransformGizmo::uploadGeometry()
{
    if (m_axisVertices.empty()) {
        return;
    }

    // Generate buffers
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);

    // Upload vertex data
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 m_axisVertices.size() * sizeof(float),
                 m_axisVertices.data(),
                 GL_STATIC_DRAW);

    // Upload index data
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 m_axisIndices.size() * sizeof(GLuint),
                 m_axisIndices.data(),
                 GL_STATIC_DRAW);

    // Vertex attributes (position + color, interleaved)
    // Position (location 0)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(0);

    // Color (location 1)
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
}

void TransformGizmo::render(const QMatrix4x4& viewProjection, const QVector3D& cameraPosition)
{
    if (!m_visible || !m_initialized || m_vao == 0) {
        return;
    }

    // Calculate scale based on distance to camera for consistent screen size
    float distance = (m_position - cameraPosition).length();
    float screenScale = distance * 0.1f * m_scale;  // Adjust factor as needed

    // Build model matrix
    QMatrix4x4 model;
    model.setToIdentity();
    model.translate(m_position);
    model.scale(screenScale);

    QMatrix4x4 mvp = viewProjection * model;

    // Disable depth test so gizmo renders on top
    glDisable(GL_DEPTH_TEST);

    // Disable face culling for gizmo
    glDisable(GL_CULL_FACE);

    m_shader.bind();
    m_shader.setUniformMat4("modelViewProjection", mvp);

    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_axisIndices.size()),
                   GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);

    m_shader.release();

    // Re-enable depth test
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
}

GizmoAxis TransformGizmo::pickAxis(const VECTOR3D& rayOrigin, const VECTOR3D& rayDirection) const
{
    if (!m_visible) {
        return GizmoAxis::None;
    }

    // Calculate the same scale used for rendering
    VECTOR3D camPos(rayOrigin.x, rayOrigin.y, rayOrigin.z);
    VECTOR3D gizmoPos(m_position.x(), m_position.y(), m_position.z());
    float distance = (gizmoPos - camPos).GetLength();
    float screenScale = distance * 0.1f * m_scale;

    float closestT = 1e30f;
    GizmoAxis closestAxis = GizmoAxis::None;

    // Test each axis
    for (int axisIdx = 1; axisIdx <= 3; ++axisIdx) {
        GizmoAxis axis = static_cast<GizmoAxis>(axisIdx);
        QVector3D dir = axisDirection(axis);

        // Axis start and end in world space
        VECTOR3D axisStart(
            m_position.x(),
            m_position.y(),
            m_position.z()
        );
        VECTOR3D axisEnd(
            m_position.x() + dir.x() * AXIS_LENGTH * screenScale,
            m_position.y() + dir.y() * AXIS_LENGTH * screenScale,
            m_position.z() + dir.z() * AXIS_LENGTH * screenScale
        );

        float t;
        if (rayIntersectsCylinder(rayOrigin, rayDirection, axisStart, axisEnd,
                                  PICK_RADIUS * screenScale, t)) {
            if (t < closestT && t > 0) {
                closestT = t;
                closestAxis = axis;
            }
        }
    }

    return closestAxis;
}

bool TransformGizmo::rayIntersectsCylinder(const VECTOR3D& rayOrigin, const VECTOR3D& rayDirection,
                                           const VECTOR3D& axisStart, const VECTOR3D& axisEnd,
                                           float radius, float& t) const
{
    // Simplified cylinder test using closest point approach
    // This is an approximation but works well for picking

    VECTOR3D axis = axisEnd - axisStart;
    float axisLength = axis.GetLength();
    if (axisLength < 0.001f) {
        return false;
    }
    axis = axis / axisLength;  // Normalize

    // Find closest point on ray to the axis line
    VECTOR3D w0 = rayOrigin - axisStart;

    float a = rayDirection.DotProduct(rayDirection);
    float b = rayDirection.DotProduct(axis);
    float c = axis.DotProduct(axis);
    float d = rayDirection.DotProduct(w0);
    float e = axis.DotProduct(w0);

    float denom = a * c - b * b;
    if (std::abs(denom) < 0.0001f) {
        // Ray parallel to axis
        return false;
    }

    float sc = (b * e - c * d) / denom;  // Parameter on ray
    float tc = (a * e - b * d) / denom;  // Parameter on axis

    // Clamp tc to axis segment
    if (tc < 0) tc = 0;
    if (tc > axisLength) tc = axisLength;

    // Closest points
    VECTOR3D pointOnRay = rayOrigin + rayDirection * sc;
    VECTOR3D pointOnAxis = axisStart + axis * tc;

    // Distance between closest points
    float dist = (pointOnRay - pointOnAxis).GetLength();

    if (dist < radius && sc > 0) {
        t = sc;
        return true;
    }

    return false;
}

} // namespace chiplet
