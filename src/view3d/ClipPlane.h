// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * ClipPlane.h - Cross-section clipping plane controller
 *
 * Manages a clipping plane for cross-section visualization.
 * The plane is defined by a normal vector and distance from origin.
 */

#ifndef CHIPLET_VIEW3D_CLIPPLANE_H
#define CHIPLET_VIEW3D_CLIPPLANE_H

#include <QVector3D>
#include <QVector4D>

namespace chiplet {

/**
 * Axis along which the clip plane can be oriented
 */
enum class ClipAxis {
    X,      // YZ plane, looking along +X
    Y,      // XZ plane, looking along +Y
    Z,      // XY plane, looking along +Z
    Custom  // User-defined orientation
};

/**
 * ClipPlane controls a cross-section clipping plane.
 *
 * The plane equation is: dot(position, normal) + distance = 0
 * Points where dot(position, normal) + distance < 0 are clipped.
 */
class ClipPlane {
public:
    ClipPlane();

    // Enable/disable clipping
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }

    // Set plane by axis and position along that axis
    void setAxis(ClipAxis axis);
    void setPosition(float position);

    // Set custom plane
    void setPlane(const QVector3D& normal, float distance);

    // Get plane parameters
    ClipAxis axis() const { return m_axis; }
    QVector3D normal() const { return m_normal; }
    float distance() const { return m_distance; }
    float position() const { return m_position; }

    // Get plane as vec4 for shader uniform (normal.xyz, distance)
    QVector4D planeEquation() const;

    // Flip the clip direction
    void flip();
    bool isFlipped() const { return m_flipped; }

    // Set position range (based on scene bounds)
    void setRange(float minPos, float maxPos);
    float minPosition() const { return m_minPos; }
    float maxPosition() const { return m_maxPos; }

    // Get normalized position (0-1 within range)
    float normalizedPosition() const;
    void setNormalizedPosition(float t);

private:
    void updatePlane();

    bool m_enabled = false;
    bool m_flipped = false;
    ClipAxis m_axis = ClipAxis::Z;

    QVector3D m_normal{0.0f, 0.0f, 1.0f};  // Default: +Z
    float m_distance = 0.0f;
    float m_position = 0.0f;

    float m_minPos = -100.0f;
    float m_maxPos = 100.0f;
};

} // namespace chiplet

#endif // CHIPLET_VIEW3D_CLIPPLANE_H
