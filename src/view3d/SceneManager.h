// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * SceneManager.h - Manages 3D scene (camera, lighting, picking)
 */

#ifndef CHIPLET_VIEW3D_SCENEMANAGER_H
#define CHIPLET_VIEW3D_SCENEMANAGER_H

#include "Camera.h"
#include "math/Maths.h"

#include <QMatrix4x4>
#include <QVector3D>

namespace chiplet {

// The scene's light as the shaders take it. They light a face in view space,
// so a light fixed in the scene is turned by the view's rotation. mapVector
// leaves out the view's translation, which would make the direction a point.
inline QVector3D lightInView(const QMatrix4x4& view, const QVector3D& light)
{
    return view.mapVector(light).normalized();
}

/**
 * SceneManager orchestrates the 3D scene: camera, lighting, and object picking.
 */
class SceneManager {
public:
    SceneManager();
    ~SceneManager();

    // Camera access
    Camera& camera() { return m_camera; }
    const Camera& camera() const { return m_camera; }

    // The light, fixed in the scene: a direction in world space, from above,
    // set at construction. The shaders take it through lightInView.
    VECTOR3D lightDirection() const { return m_lightDirection; }

    // Scene bounds
    void setSceneBounds(const AA_BOUNDING_BOX& bounds);
    AA_BOUNDING_BOX sceneBounds() const { return m_sceneBounds; }

    // Picking (ray casting)
    struct Ray {
        VECTOR3D origin;
        VECTOR3D direction;
    };

    Ray screenToRay(int screenX, int screenY, int viewportWidth, int viewportHeight) const;

    // Fit camera to scene
    void fitToScene(float aspectRatio = 1.0f);

private:
    Camera m_camera;
    VECTOR3D m_lightDirection;
    AA_BOUNDING_BOX m_sceneBounds;
};

} // namespace chiplet

#endif // CHIPLET_VIEW3D_SCENEMANAGER_H
