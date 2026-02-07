/**
 * Snapper.h - Grid snapping utilities for precision placement
 */

#ifndef CHIPLET_CORE_SNAPPER_H
#define CHIPLET_CORE_SNAPPER_H

#include "Component.h"  // For Position3D
#include <cmath>

namespace chiplet {

/**
 * Snapper provides static utilities for snapping coordinates to a grid.
 */
class Snapper {
public:
    /**
     * Snap a position to the nearest grid point.
     * @param pos The position to snap (in micrometers)
     * @param gridSize The grid spacing (in micrometers)
     * @return The snapped position
     */
    static Position3D snap(const Position3D& pos, double gridSize) {
        if (gridSize <= 0.0) {
            return pos;  // No snapping if grid disabled
        }
        return Position3D{
            std::round(pos.x / gridSize) * gridSize,
            std::round(pos.y / gridSize) * gridSize,
            std::round(pos.z / gridSize) * gridSize
        };
    }

    /**
     * Snap a single coordinate value.
     * @param value The value to snap
     * @param gridSize The grid spacing
     * @return The snapped value
     */
    static double snapValue(double value, double gridSize) {
        if (gridSize <= 0.0) {
            return value;
        }
        return std::round(value / gridSize) * gridSize;
    }
};

} // namespace chiplet

#endif // CHIPLET_CORE_SNAPPER_H
