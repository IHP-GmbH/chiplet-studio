// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * SceneManager.cpp - Scene management implementation
 */

#include "SceneManager.h"
#include <cmath>

namespace chiplet {

SceneManager::SceneManager()
    : m_lightDirection(-0.5f, -1.0f, -0.5f)
{
    m_lightDirection.Normalize();
}

SceneManager::~SceneManager() = default;

void SceneManager::setLightDirection(const VECTOR3D& dir)
{
    m_lightDirection = dir;
    m_lightDirection.Normalize();
}

void SceneManager::setSceneBounds(const AA_BOUNDING_BOX& bounds)
{
    m_sceneBounds = bounds;
}

void SceneManager::fitToScene()
{
    m_camera.fitToBox(m_sceneBounds);
}

SceneManager::Ray SceneManager::screenToRay(int screenX, int screenY,
                                             int viewportWidth, int viewportHeight) const
{
    Ray ray;

    // Convert screen coordinates to normalized device coordinates [-1, 1]
    float ndcX = (2.0f * screenX / viewportWidth) - 1.0f;
    float ndcY = 1.0f - (2.0f * screenY / viewportHeight);

    // Get inverse matrices
    float aspect = static_cast<float>(viewportWidth) / viewportHeight;
    MATRIX4X4 proj = m_camera.projectionMatrix(aspect);
    MATRIX4X4 view = m_camera.viewMatrix();

    MATRIX4X4 invProj = proj.GetInverse();
    MATRIX4X4 invView = view.GetInverse();

    // Ray in clip space
    VECTOR4D rayClipNear(ndcX, ndcY, -1.0f, 1.0f);
    VECTOR4D rayClipFar(ndcX, ndcY, 1.0f, 1.0f);

    // Transform to eye space
    VECTOR4D rayEyeNear = invProj * rayClipNear;
    VECTOR4D rayEyeFar = invProj * rayClipFar;

    // Perspective divide
    rayEyeNear = rayEyeNear / rayEyeNear.w;
    rayEyeFar = rayEyeFar / rayEyeFar.w;

    // Transform to world space
    VECTOR4D rayWorldNear = invView * rayEyeNear;
    VECTOR4D rayWorldFar = invView * rayEyeFar;

    ray.origin = VECTOR3D(rayWorldNear.x, rayWorldNear.y, rayWorldNear.z);
    VECTOR3D rayEnd(rayWorldFar.x, rayWorldFar.y, rayWorldFar.z);

    ray.direction = rayEnd - ray.origin;
    ray.direction.Normalize();

    return ray;
}

} // namespace chiplet
