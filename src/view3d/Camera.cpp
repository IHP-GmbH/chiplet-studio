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
    m_followFit = false;
    m_yaw += deltaYaw * m_orbitSensitivity;
    m_pitch += deltaPitch * m_orbitSensitivity;
    clampPitch();

    // Normalize yaw to [0, 360)
    while (m_yaw < 0.0f) m_yaw += 360.0f;
    while (m_yaw >= 360.0f) m_yaw -= 360.0f;
}

void Camera::pan(float deltaX, float deltaY)
{
    m_followFit = false;
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
    m_followFit = false;
    // Exponential zoom for smooth feel at all distances
    float factor = 1.0f - delta * m_zoomSensitivity;

    // Cap factor so each scroll step changes distance by at most 30%
    factor = std::max(0.7f, std::min(factor, 1.43f));

    m_distance *= factor;

    // Min 0.00001 (10nm) for inspecting nanoscale features, max 10M for large assemblies
    m_distance = std::max(0.00001f, std::min(m_distance, 10000000.0f));

    updateClipPlanes();
}

void Camera::fitToBox(const AA_BOUNDING_BOX& box, float aspectRatio)
{
    m_followFit = false;
    const auto finite = [](const VECTOR3D& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    };
    const VECTOR3D size = box.maxes - box.mins;
    if (!finite(box.mins) || !finite(box.maxes) || !finite(size)
            || size.x < 0 || size.y < 0 || size.z < 0
            || !(std::max({size.x, size.y, size.z}) > 0)
            || !std::isfinite(aspectRatio) || aspectRatio <= 0
            || !std::isfinite(m_fov) || m_fov <= 0 || m_fov >= 180) {
        reset();
        return;
    }
    const VECTOR3D center = box.mins * 0.5f + box.maxes * 0.5f;
    // Same basis as viewMatrix; preserve the user's viewing direction.
    VECTOR3D forward = m_target - position();
    forward.Normalize();
    VECTOR3D right = forward.CrossProduct(m_up);
    right.Normalize();
    VECTOR3D up = right.CrossProduct(forward);
    up.Normalize();
    const double tanY = std::tan(m_fov * M_PI / 360.0);
    const double tanX = tanY * aspectRatio;
    double distance = 0;
    for (int i = 0; i < 8; ++i) {
        const VECTOR3D corner((i & 1) ? box.maxes.x : box.mins.x,
                              (i & 2) ? box.maxes.y : box.mins.y,
                              (i & 4) ? box.maxes.z : box.mins.z);
        const VECTOR3D relative = corner - center;
        const double towardEye = -relative.DotProduct(forward);
        // 10% screen-space margin, including depth of each individual corner.
        distance = std::max(distance, 1.1 * std::max(0.0, towardEye) + 1.1 * std::max(
            std::abs(relative.DotProduct(right)) / tanX,
            std::abs(relative.DotProduct(up)) / tanY));
    }
    if (!std::isfinite(distance) || distance <= 0 || distance > 1e7) {
        reset();
        return;
    }
    m_target = center;
    m_distance = static_cast<float>(distance);
    m_viewportAspect = aspectRatio;
    m_fittedBox = box;
    m_followFit = true;
    updateClipPlanes();
}

void Camera::setViewportAspect(float aspectRatio)
{
    // Transient zero-sized widgets must not destroy either fit or user state.
    if (!std::isfinite(aspectRatio) || aspectRatio <= 0) return;
    if (m_viewportAspect == aspectRatio) return;
    m_viewportAspect = aspectRatio;
    if (m_followFit) fitToBox(m_fittedBox, aspectRatio);
}

void Camera::reset()
{
    m_followFit = false;
    m_viewportAspect = 1.0f;
    m_target = VECTOR3D(0.0f, 0.0f, 0.0f);
    m_distance = 1000.0f;
    m_yaw = 0.0f;      // Face forward for top-down view
    m_pitch = 89.0f;   // Near-vertical for top-down view
    m_fov = 45.0f;
    updateClipPlanes();
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
    m_followFit = false;
    m_target = target;
}

void Camera::setDistance(float distance)
{
    m_followFit = false;
    // Match zoom() range: 0.00001 (10nm) to 10,000,000
    m_distance = std::max(0.00001f, std::min(distance, 10000000.0f));
    updateClipPlanes();
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

void Camera::setOrientation(float yaw, float pitch)
{
    m_followFit = false;
    m_yaw = yaw;
    m_pitch = pitch;
    clampPitch();
    while (m_yaw < 0.0f) m_yaw += 360.0f;
    while (m_yaw >= 360.0f) m_yaw -= 360.0f;
}

void Camera::clampPitch()
{
    // Prevent gimbal lock by limiting pitch
    m_pitch = std::max(-89.0f, std::min(m_pitch, 89.0f));
}

} // namespace chiplet
