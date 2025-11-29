/**
 * SceneManager.cpp - Implementation
 */

#include "SceneManager.h"

namespace chiplet {

SceneManager::SceneManager() = default;
SceneManager::~SceneManager() = default;

void SceneManager::setCamera(double x, double y, double z)
{
    m_cameraX = x;
    m_cameraY = y;
    m_cameraZ = z;
}

void SceneManager::setTarget(double x, double y, double z)
{
    m_targetX = x;
    m_targetY = y;
    m_targetZ = z;
}

void SceneManager::rotate(double dx, double dy)
{
    // TODO: Implement camera rotation
}

void SceneManager::zoom(double delta)
{
    // TODO: Implement zoom
}

void SceneManager::pan(double dx, double dy)
{
    // TODO: Implement pan
}

} // namespace chiplet
