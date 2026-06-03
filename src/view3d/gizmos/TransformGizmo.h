// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * TransformGizmo.h - 3D transformation gizmo for component manipulation
 *
 * Renders X/Y/Z axis handles that can be dragged to move components.
 * Red=X, Green=Y, Blue=Z (standard convention).
 */

#ifndef CHIPLET_VIEW3D_GIZMOS_TRANSFORMGIZMO_H
#define CHIPLET_VIEW3D_GIZMOS_TRANSFORMGIZMO_H

#include <QOpenGLExtraFunctions>
#include <QVector3D>
#include <QMatrix4x4>
#include <QColor>
#include "view3d/ShaderProgram.h"
#include "view3d/math/VECTOR3D.h"

namespace chiplet {

/**
 * Axis enumeration for gizmo interaction
 */
enum class GizmoAxis {
    None,
    X,
    Y,
    Z
};

/**
 * TransformGizmo renders movable axis handles at a given position.
 * Each axis is a cylinder + cone (arrow) that can be picked and dragged.
 */
class TransformGizmo : protected QOpenGLExtraFunctions {
public:
    TransformGizmo();
    ~TransformGizmo();

    // Initialize OpenGL resources (call after GL context is current)
    void initialize();

    // Release OpenGL resources
    void release();

    // Set/get gizmo position (world coordinates)
    void setPosition(const QVector3D& pos);
    QVector3D position() const { return m_position; }

    // Set/get gizmo scale (for consistent screen-space size)
    void setScale(float scale);
    float scale() const { return m_scale; }

    // Visibility
    void setVisible(bool visible) { m_visible = visible; }
    bool isVisible() const { return m_visible; }

    // Highlight an axis (when hovering or dragging)
    void setHighlightedAxis(GizmoAxis axis) { m_highlightedAxis = axis; }
    GizmoAxis highlightedAxis() const { return m_highlightedAxis; }

    // Render the gizmo
    // viewProjection: combined view*projection matrix
    // cameraPosition: for consistent sizing
    void render(const QMatrix4x4& viewProjection, const QVector3D& cameraPosition);

    // Pick test: returns which axis (if any) is hit by the ray
    // ray origin and direction in world space
    GizmoAxis pickAxis(const VECTOR3D& rayOrigin, const VECTOR3D& rayDirection) const;

    // Get the axis direction vector
    static QVector3D axisDirection(GizmoAxis axis);

    // Get the axis color
    static QColor axisColor(GizmoAxis axis, bool highlighted = false);

    // Check if initialized
    bool isInitialized() const { return m_initialized; }

private:
    void buildAxisGeometry();
    void uploadGeometry();

    // Cylinder ray intersection for picking
    bool rayIntersectsCylinder(const VECTOR3D& rayOrigin, const VECTOR3D& rayDirection,
                               const VECTOR3D& axisStart, const VECTOR3D& axisEnd,
                               float radius, float& t) const;

    // State
    QVector3D m_position;
    float m_scale = 1.0f;
    bool m_visible = false;
    bool m_initialized = false;
    GizmoAxis m_highlightedAxis = GizmoAxis::None;

    // Geometry
    std::vector<float> m_axisVertices;  // Position + color per vertex
    std::vector<GLuint> m_axisIndices;

    // OpenGL objects
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_ebo = 0;

    // Shader
    ShaderProgram m_shader;

    // Axis parameters
    static constexpr float AXIS_LENGTH = 2.0f;      // Length of each axis arrow
    static constexpr float AXIS_RADIUS = 0.05f;     // Cylinder radius
    static constexpr float CONE_LENGTH = 0.3f;      // Arrow cone length
    static constexpr float CONE_RADIUS = 0.12f;     // Arrow cone base radius
    static constexpr float PICK_RADIUS = 0.15f;     // Picking tolerance
    static constexpr int SEGMENTS = 12;             // Circle segments for cylinder
};

} // namespace chiplet

#endif // CHIPLET_VIEW3D_GIZMOS_TRANSFORMGIZMO_H
