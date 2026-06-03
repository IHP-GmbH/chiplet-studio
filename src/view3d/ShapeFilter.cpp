// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * ShapeFilter.cpp - Area-based shape filtering implementation
 */

#include "ShapeFilter.h"
#include <cmath>
#include <algorithm>
#include <limits>

namespace chiplet {

AreaStatistics ShapeFilter::computeStatistics(
    const std::map<LayerKey, LayerPolygons>& polygons)
{
    AreaStatistics stats;
    stats.total_layers = polygons.size();

    if (polygons.empty()) return stats;

    double min_a = std::numeric_limits<double>::max();
    double max_a = 0.0;
    size_t count = 0;

    for (const auto& [key, layerPolys] : polygons) {
        for (const auto& poly : layerPolys.polygons) {
            double a = std::abs(poly.area());
            if (a < 1e-12) continue;  // Skip degenerate polygons
            min_a = std::min(min_a, a);
            max_a = std::max(max_a, a);
            ++count;
        }
    }

    if (count == 0) return stats;

    stats.min_area = min_a;
    stats.max_area = max_a;
    stats.total_polygons = count;
    return stats;
}

double ShapeFilter::thresholdFromPercentage(
    double percentage,
    const AreaStatistics& stats)
{
    if (stats.total_polygons == 0 || stats.max_area <= 0.0)
        return 0.0;

    double pct = std::clamp(percentage, 0.0, 100.0);
    if (pct <= 0.0) return 0.0;

    // Clamp min to avoid log(0) or division by zero
    double min_a = std::max(stats.min_area, 1e-6);
    double max_a = stats.max_area;

    if (max_a <= min_a) {
        // All polygons have the same area -- any filter removes all
        return (pct > 0.0) ? max_a + 1e-6 : 0.0;
    }

    // Logarithmic mapping: geometric interpolation between min and max
    return min_a * std::pow(max_a / min_a, pct / 100.0);
}

std::map<LayerKey, LayerPolygons> ShapeFilter::filter(
    const std::map<LayerKey, LayerPolygons>& polygons,
    double area_threshold)
{
    if (area_threshold <= 0.0) return polygons;

    std::map<LayerKey, LayerPolygons> result;

    for (const auto& [key, layerPolys] : polygons) {
        LayerPolygons filtered;
        filtered.key = layerPolys.key;
        filtered.name = layerPolys.name;

        for (const auto& poly : layerPolys.polygons) {
            if (std::abs(poly.area()) >= area_threshold) {
                filtered.polygons.push_back(poly);
            }
        }

        // Preserve layer even if empty (keeps mesh structure consistent)
        result[key] = std::move(filtered);
    }

    return result;
}

} // namespace chiplet
