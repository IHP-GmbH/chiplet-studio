/**
 * ConnectionStack.h - Connection stack definition for chiplet-to-interposer bonding
 *
 * Models the physical layers between a chiplet die and its mounting surface
 * (e.g., Cu-pillar + SnAg cap, solder bumps). Used to auto-calculate
 * the chiplet z-position from the interposer stackup height.
 */

#ifndef CHIPLET_CORE_CONNECTIONSTACK_H
#define CHIPLET_CORE_CONNECTIONSTACK_H

#include <string>
#include <vector>

namespace chiplet {

/**
 * A single physical layer within a connection stack
 */
struct ConnectionStackLayer {
    std::string name;       // "CuPillar", "SnAgCap", "SolderBall"
    std::string material;   // "Cu", "SnAg", "SAC305"
    double height = 0.0;    // um
    double diameter = 0.0;  // um
};

/**
 * A connection stack defines the bonding structure between a die and interposer.
 * Each chiplet has exactly one connection type (all pads use the same type).
 */
struct ConnectionStack {
    std::string id;
    std::string description;
    std::vector<ConnectionStackLayer> layers;

    double total_height() const {
        double h = 0.0;
        for (const auto& layer : layers) {
            h += layer.height;
        }
        return h;
    }
};

} // namespace chiplet

#endif // CHIPLET_CORE_CONNECTIONSTACK_H
