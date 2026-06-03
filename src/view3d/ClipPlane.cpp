// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * ClipPlane.cpp - Cross-section clipping plane implementation
 */

#include "ClipPlane.h"
#include <algorithm>

namespace chiplet {

ClipPlane::ClipPlane()
{
    setAxis(ClipAxis::Z);
}

void ClipPlane::setAxis(ClipAxis axis)
{
    m_axis = axis;

    switch (axis) {
    case ClipAxis::X:
        m_normal = QVector3D(1.0f, 0.0f, 0.0f);
        break;
    case ClipAxis::Y:
        m_normal = QVector3D(0.0f, 1.0f, 0.0f);
        break;
    case ClipAxis::Z:
        m_normal = QVector3D(0.0f, 0.0f, 1.0f);
        break;
    case ClipAxis::Custom:
        // Keep current normal
        break;
    }

    updatePlane();
}

void ClipPlane::setPosition(float position)
{
    m_position = std::clamp(position, m_minPos, m_maxPos);
    updatePlane();
}

void ClipPlane::setPlane(const QVector3D& normal, float distance)
{
    m_axis = ClipAxis::Custom;
    m_normal = normal.normalized();
    m_distance = distance;
}

QVector4D ClipPlane::planeEquation() const
{
    QVector3D n = m_flipped ? -m_normal : m_normal;
    float d = m_flipped ? -m_distance : m_distance;
    return QVector4D(n.x(), n.y(), n.z(), d);
}

void ClipPlane::flip()
{
    m_flipped = !m_flipped;
}

void ClipPlane::setRange(float minPos, float maxPos)
{
    m_minPos = minPos;
    m_maxPos = maxPos;

    // Clamp current position to new range
    m_position = std::clamp(m_position, m_minPos, m_maxPos);
    updatePlane();
}

float ClipPlane::normalizedPosition() const
{
    if (m_maxPos <= m_minPos) {
        return 0.5f;
    }
    return (m_position - m_minPos) / (m_maxPos - m_minPos);
}

void ClipPlane::setNormalizedPosition(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    setPosition(m_minPos + t * (m_maxPos - m_minPos));
}

void ClipPlane::updatePlane()
{
    // Distance is negative of position along normal
    // This makes the plane pass through (position * normal)
    m_distance = -m_position;
}

} // namespace chiplet
