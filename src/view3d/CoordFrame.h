// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * CoordFrame.h - Single source of truth for the chiplet(um) -> 3D scene(mm)
 * base transform.
 *
 * Mapping (see docs/coord_frame_contract.md): chiplet X -> scene X,
 * chiplet Z (elevation) -> scene Y (up), chiplet Y -> scene -Z; micrometres
 * to millimetres. Several render paths used to restate this by hand, which is
 * how it could silently drift between them; they are now all routed through
 * sceneFromChiplet(). The contract test keeps its OWN literal restatement of
 * the mapping so this helper stays pinned to the spec rather than checked
 * against itself.
 */

#ifndef CHIPLET_VIEW3D_COORDFRAME_H
#define CHIPLET_VIEW3D_COORDFRAME_H

#include "core/Component.h"  // Position3D

namespace chiplet {

inline constexpr double kUmToMm = 1.0 / 1000.0;

struct ScenePosition {
    float x;
    float y;
    float z;
};

// Base position transform only. Callers that need a centre or a face offset
// (e.g. + h/2) add it to the result; this covers the shared um->mm mapping.
inline ScenePosition sceneFromChiplet(const Position3D& p)
{
    return ScenePosition{
        static_cast<float>(p.x * kUmToMm),
        static_cast<float>(p.z * kUmToMm),
        static_cast<float>(-p.y * kUmToMm)
    };
}

} // namespace chiplet

#endif // CHIPLET_VIEW3D_COORDFRAME_H
