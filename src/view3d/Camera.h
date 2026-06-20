// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Camera.h - Orbit camera for 3D assembly visualization
 */

#ifndef CHIPLET_VIEW3D_CAMERA_H
#define CHIPLET_VIEW3D_CAMERA_H

#include "math/Maths.h"

namespace chiplet {

/**
 * Orbit camera that rotates around a target point.
 * Supports orbit (rotate), pan, and zoom operations.
 */
class Camera {
public:
    Camera();
    ~Camera() = default;

    // Camera manipulation
    void orbit(float deltaYaw, float deltaPitch);
    void pan(float deltaX, float deltaY);
    void zoom(float delta);

    // Fit camera to view a bounding box
    void fitToBox(const AA_BOUNDING_BOX& box);

    // Reset to default view
    void reset();

    // Matrix generation
    MATRIX4X4 viewMatrix() const;
    MATRIX4X4 projectionMatrix(float aspectRatio) const;

    // Getters
    VECTOR3D position() const;
    VECTOR3D target() const { return m_target; }
    VECTOR3D up() const { return m_up; }
    float distance() const { return m_distance; }
    float yaw() const { return m_yaw; }
    float pitch() const { return m_pitch; }
    float fov() const { return m_fov; }
    float nearPlane() const { return m_near; }
    float farPlane() const { return m_far; }
    float fcoef() const;

    // Setters
    void setTarget(const VECTOR3D& target);
    void setDistance(float distance);
    void setFov(float fov) { m_fov = fov; }

private:
    void updateClipPlanes();
    void clampPitch();

    VECTOR3D m_target;       // Point the camera orbits around
    VECTOR3D m_up;           // Up vector (typically Y-up)
    float m_distance;        // Distance from target
    float m_yaw;             // Horizontal rotation (degrees)
    float m_pitch;           // Vertical rotation (degrees)
    float m_fov;             // Field of view (degrees)
    float m_near;            // Near clip plane
    float m_far;             // Far clip plane

    // Sensitivity settings
    float m_orbitSensitivity;
    float m_panSensitivity;
    float m_zoomSensitivity;
};

} // namespace chiplet

#endif // CHIPLET_VIEW3D_CAMERA_H
