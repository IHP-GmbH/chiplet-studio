// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Camera.cpp - Orbit camera implementation
 */

#include "Camera.h"
#include <algorithm>
#include <cmath>

namespace chiplet {

Camera::Camera()
    : m_target(0.0f, 0.0f, 0.0f)
    , m_up(0.0f, 1.0f, 0.0f)
    , m_distance(1000.0f)
    , m_yaw(0.0f)      // Face forward for top-down view
    , m_pitch(89.0f)   // Near-vertical for top-down view (like KLayout 2.5D)
    , m_fov(45.0f)
    , m_near(1.0f)
    , m_far(100000.0f)
    , m_orbitSensitivity(0.5f)
    , m_panSensitivity(1.0f)
    , m_zoomSensitivity(0.1f)
{
}

void Camera::orbit(float deltaYaw, float deltaPitch)
{
    m_yaw += deltaYaw * m_orbitSensitivity;
    m_pitch += deltaPitch * m_orbitSensitivity;
    clampPitch();

    // Normalize yaw to [0, 360)
    while (m_yaw < 0.0f) m_yaw += 360.0f;
    while (m_yaw >= 360.0f) m_yaw -= 360.0f;
}

void Camera::pan(float deltaX, float deltaY)
{
    // Calculate camera's right and up vectors in world space
    VECTOR3D pos = position();
    VECTOR3D forward = m_target - pos;
    forward.Normalize();

    VECTOR3D right = forward.CrossProduct(m_up);
    right.Normalize();

    VECTOR3D camUp = right.CrossProduct(forward);
    camUp.Normalize();

    // Scale pan by distance for consistent feel
    float panScale = m_distance * m_panSensitivity * 0.001f;
    m_target = m_target - right * deltaX * panScale + camUp * deltaY * panScale;
}

void Camera::zoom(float delta)
{
    // Exponential zoom for smooth feel at all distances
    float factor = 1.0f - delta * m_zoomSensitivity;

    // Cap factor so each scroll step changes distance by at most 30%
    factor = std::max(0.7f, std::min(factor, 1.43f));

    m_distance *= factor;

    // Min 0.00001 (10nm) for inspecting nanoscale features, max 10M for large assemblies
    m_distance = std::max(0.00001f, std::min(m_distance, 10000000.0f));

    updateClipPlanes();
}

void Camera::fitToBox(const AA_BOUNDING_BOX& box)
{
    // Set target to box center
    VECTOR3D center = (box.mins + box.maxes) * 0.5f;
    m_target = center;

    // Calculate distance to fit box in view
    VECTOR3D size = box.maxes - box.mins;

    float maxDim = std::max({size.x, size.y, size.z});

    // Guard against a degenerate or empty box (maxDim <= 0, e.g. an assembly
    // with no geometry, or an uninitialized bbox where mins > maxes). Without
    // this m_distance becomes 0 (or negative/NaN), yielding a singular view
    // matrix and a blank, unrecoverable 3D view.
    if (!(maxDim > 0.0f)) {  // also catches NaN
        m_distance = 1000.0f;  // same sane default as reset()
        updateClipPlanes();
        return;
    }

    // Distance to fit object in view based on FOV
    float fovRad = static_cast<float>(m_fov * M_PI / 180.0);
    m_distance = (maxDim * 0.5f) / std::tan(fovRad * 0.5f) * 1.5f;

    updateClipPlanes();
}

void Camera::reset()
{
    m_target = VECTOR3D(0.0f, 0.0f, 0.0f);
    m_distance = 1000.0f;
    m_yaw = 0.0f;      // Face forward for top-down view
    m_pitch = 89.0f;   // Near-vertical for top-down view
}

VECTOR3D Camera::position() const
{
    // Convert spherical coordinates to Cartesian
    float yawRad = static_cast<float>(m_yaw * M_PI / 180.0);
    float pitchRad = static_cast<float>(m_pitch * M_PI / 180.0);

    float cosPitch = std::cos(pitchRad);
    float sinPitch = std::sin(pitchRad);
    float cosYaw = std::cos(yawRad);
    float sinYaw = std::sin(yawRad);

    VECTOR3D offset(
        m_distance * cosPitch * sinYaw,
        m_distance * sinPitch,
        m_distance * cosPitch * cosYaw
    );

    return m_target + offset;
}

MATRIX4X4 Camera::viewMatrix() const
{
    VECTOR3D eye = position();
    VECTOR3D forward = m_target - eye;
    forward.Normalize();

    VECTOR3D right = forward.CrossProduct(m_up);
    right.Normalize();

    VECTOR3D up = right.CrossProduct(forward);
    up.Normalize();

    // Build look-at matrix (column-major for OpenGL)
    MATRIX4X4 view;
    view.LoadIdentity();

    // Rotation part
    view.SetEntry(0, right.x);
    view.SetEntry(4, right.y);
    view.SetEntry(8, right.z);

    view.SetEntry(1, up.x);
    view.SetEntry(5, up.y);
    view.SetEntry(9, up.z);

    view.SetEntry(2, -forward.x);
    view.SetEntry(6, -forward.y);
    view.SetEntry(10, -forward.z);

    // Translation part
    view.SetEntry(12, -right.DotProduct(eye));
    view.SetEntry(13, -up.DotProduct(eye));
    view.SetEntry(14, forward.DotProduct(eye));

    return view;
}

MATRIX4X4 Camera::projectionMatrix(float aspectRatio) const
{
    MATRIX4X4 proj;
    proj.SetPerspective(m_fov, aspectRatio, m_near, m_far);
    return proj;
}

void Camera::setTarget(const VECTOR3D& target)
{
    m_target = target;
}

void Camera::setDistance(float distance)
{
    // Match zoom() range: 0.00001 (10nm) to 10,000,000
    m_distance = std::max(0.00001f, std::min(distance, 10000000.0f));
    updateClipPlanes();
}

void Camera::setClipPlanes(float nearPlane, float farPlane)
{
    m_near = nearPlane;
    m_far = farPlane;
}

void Camera::updateClipPlanes()
{
    // With logarithmic depth buffer, precision is uniform regardless of
    // near/far ratio, so we can safely use a very small near plane.
    // Scale near plane with camera distance to prevent geometry at the
    // target from being clipped when zoomed in very close.
    m_near = std::max(1e-7f, m_distance * 0.001f);
    m_far = 100000.0f;    // 100m
}

float Camera::fcoef() const
{
    return 2.0f / log2f(m_far + 1.0f);
}

void Camera::clampPitch()
{
    // Prevent gimbal lock by limiting pitch
    m_pitch = std::max(-89.0f, std::min(m_pitch, 89.0f));
}

} // namespace chiplet
