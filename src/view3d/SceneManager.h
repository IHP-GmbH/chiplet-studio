/**
 * SceneManager.h - Manages 3D scene (camera, lighting, picking)
 */

#ifndef CHIPLET_VIEW3D_SCENEMANAGER_H
#define CHIPLET_VIEW3D_SCENEMANAGER_H

namespace chiplet {

/**
 * SceneManager handles camera, lighting, and object picking.
 */
class SceneManager {
public:
    SceneManager();
    ~SceneManager();

    void setCamera(double x, double y, double z);
    void setTarget(double x, double y, double z);
    void rotate(double dx, double dy);
    void zoom(double delta);
    void pan(double dx, double dy);

private:
    double m_cameraX = 0.0, m_cameraY = 0.0, m_cameraZ = 1000.0;
    double m_targetX = 0.0, m_targetY = 0.0, m_targetZ = 0.0;
};

} // namespace chiplet

#endif // CHIPLET_VIEW3D_SCENEMANAGER_H
