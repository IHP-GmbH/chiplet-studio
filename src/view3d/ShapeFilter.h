/**
 * ShapeFilter.h - Area-based shape filtering for detailed 3D rendering
 *
 * Provides utilities to progressively hide small GDS polygons based on
 * area threshold, enabling interactive filtering via a slider control.
 * Uses logarithmic mapping to handle IC polygon areas spanning multiple
 * orders of magnitude (vias to power planes).
 */

#ifndef CHIPLET_VIEW3D_SHAPEFILTER_H
#define CHIPLET_VIEW3D_SHAPEFILTER_H

#include "GDSLayerExtractor.h"
#include <map>

namespace chiplet {

/**
 * Area statistics for a set of polygons across all layers
 */
struct AreaStatistics {
    double min_area = 0.0;      // Smallest polygon absolute area (um^2)
    double max_area = 0.0;      // Largest polygon absolute area (um^2)
    size_t total_polygons = 0;
    size_t total_layers = 0;
};

/**
 * ShapeFilter provides area-based polygon filtering.
 * All methods are static -- this is a stateless utility class.
 */
class ShapeFilter {
public:
    /**
     * Compute area statistics from polygon data.
     * Iterates all polygons across all layers, collecting absolute areas.
     */
    static AreaStatistics computeStatistics(
        const std::map<LayerKey, LayerPolygons>& polygons);

    /**
     * Compute absolute area threshold from slider percentage.
     * Uses logarithmic mapping: threshold = min * pow(max/min, pct/100).
     * @param percentage 0.0 = show all (returns min_area), 100.0 = show none (returns max_area)
     * @param stats Area statistics from computeStatistics()
     * @return Absolute area threshold in um^2
     */
    static double thresholdFromPercentage(
        double percentage,
        const AreaStatistics& stats);

    /**
     * Filter polygons by area threshold.
     * Returns a new map with only polygons whose |area| >= threshold.
     * Empty layers are preserved to maintain layer mesh structure.
     */
    static std::map<LayerKey, LayerPolygons> filter(
        const std::map<LayerKey, LayerPolygons>& polygons,
        double area_threshold);
};

} // namespace chiplet

#endif // CHIPLET_VIEW3D_SHAPEFILTER_H
