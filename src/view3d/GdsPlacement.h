// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "CoordFrame.h"
#include "GDSLayerExtractor.h"
#include "math/Maths.h"
#include <QMatrix4x4>
#include <algorithm>

namespace chiplet {

// Match LayerMeshBuilder's anchor and AssemblyView's detailed X reflection.
// This deliberately retains the existing renderer's unrotated placement scope.
inline ScenePosition gdsBoxCenter(const GDSBoundingBox& bbox,
                                 const Position3D& position, Anchor anchor,
                                 Orientation orientation)
{
    auto center = sceneFromChiplet(position);
    if (anchor == Anchor::GdsOrigin) {
        const double localX = (bbox.x_min + bbox.x_max) * 0.5;
        const double localY = (bbox.y_min + bbox.y_max) * 0.5;
        center.x += static_cast<float>(localX * kUmToMm *
            (orientation == Orientation::FaceDown ? -1.0 : 1.0));
        center.z -= static_cast<float>(localY * kUmToMm);
    }
    return center;
}

// Transform all corners, not only translation: reflection reverses extrema.
inline AA_BOUNDING_BOX transformedMeshBounds(const AA_BOUNDING_BOX& bounds,
                                             const QMatrix4x4& transform)
{
    const QVector3D first = transform.map(QVector3D(bounds.mins.x, bounds.mins.y, bounds.mins.z));
    VECTOR3D lo(first.x(), first.y(), first.z());
    VECTOR3D hi = lo;
    for (int corner = 1; corner < 8; ++corner) {
        const auto p = transform.map(QVector3D(
            corner & 1 ? bounds.maxes.x : bounds.mins.x,
            corner & 2 ? bounds.maxes.y : bounds.mins.y,
            corner & 4 ? bounds.maxes.z : bounds.mins.z));
        lo.x = std::min(lo.x, p.x()); lo.y = std::min(lo.y, p.y()); lo.z = std::min(lo.z, p.z());
        hi.x = std::max(hi.x, p.x()); hi.y = std::max(hi.y, p.y()); hi.z = std::max(hi.z, p.z());
    }
    AA_BOUNDING_BOX result;
    result.SetFromMinsMaxes(lo, hi);
    return result;
}

} // namespace chiplet
