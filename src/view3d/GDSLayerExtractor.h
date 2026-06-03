// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * GDSLayerExtractor.h - Extract layer polygons from GDS files for 3D rendering
 *
 * Uses KLayout's db library to extract polygon geometry from GDS layers,
 * producing simplified polygon data suitable for 3D mesh generation.
 */

#ifndef CHIPLET_VIEW3D_GDSLAYEREXTRACTOR_H
#define CHIPLET_VIEW3D_GDSLAYEREXTRACTOR_H

#include <vector>
#include <map>
#include <string>
#include "view2d/LayerProperties.h"

#ifdef HAVE_KLAYOUT
namespace db {
    class Layout;
    class Cell;
}
#endif

namespace chiplet {

/**
 * 2D Point in micrometers
 */
struct Point2D {
    double x = 0.0;
    double y = 0.0;

    Point2D() = default;
    Point2D(double x_, double y_) : x(x_), y(y_) {}
};

/**
 * Simple polygon representation (outer boundary only)
 */
struct SimplePolygon {
    std::vector<Point2D> points;

    bool empty() const { return points.size() < 3; }
    size_t size() const { return points.size(); }

    // Calculate bounding box
    void boundingBox(double& minX, double& minY, double& maxX, double& maxY) const;

    // Calculate area (positive = CCW, negative = CW)
    double area() const;

    // Check if polygon is counter-clockwise
    bool isCCW() const { return area() > 0; }
};

/**
 * Collection of polygons for a single layer
 */
struct LayerPolygons {
    LayerKey key;
    std::string name;
    std::vector<SimplePolygon> polygons;

    size_t polygonCount() const { return polygons.size(); }
    size_t totalPoints() const;
};

/**
 * Configuration for polygon extraction
 */
struct ExtractionConfig {
    double simplification_tolerance = 0.0;  // 0 = no simplification
    size_t max_polygons_per_layer = 100000;  // Limit for very complex layouts
    bool merge_polygons = false;             // Merge touching polygons
    bool flatten_hierarchy = true;           // Flatten cell hierarchy
    double min_polygon_area = 0.0;           // Skip tiny polygons (um^2)
};

/**
 * 2D bounding box for a GDS cell (in micrometers)
 */
struct GDSBoundingBox {
    double x_min = 0.0;
    double y_min = 0.0;
    double x_max = 0.0;
    double y_max = 0.0;

    double width() const { return x_max - x_min; }
    double height() const { return y_max - y_min; }
    bool is_valid() const { return x_max > x_min && y_max > y_min; }
};

/**
 * GDSLayerExtractor extracts polygon geometry from GDS layouts
 */
class GDSLayerExtractor {
public:
    GDSLayerExtractor();
    ~GDSLayerExtractor();

    // Set extraction configuration
    void setConfig(const ExtractionConfig& config) { m_config = config; }
    const ExtractionConfig& config() const { return m_config; }

#ifdef HAVE_KLAYOUT
    /**
     * Extract polygons from a KLayout Layout
     * @param layout The layout to extract from
     * @param cell_name The cell to extract (use top cell if empty)
     * @param layers Optional: specific layers to extract (empty = all)
     * @return Map of LayerKey -> LayerPolygons
     */
    std::map<LayerKey, LayerPolygons> extract(
        db::Layout* layout,
        const std::string& cell_name = "",
        const std::vector<LayerKey>& layers = {});

    /**
     * Extract polygons for a single layer
     */
    LayerPolygons extractLayer(
        db::Layout* layout,
        const std::string& cell_name,
        const LayerKey& layer);
#endif

    /**
     * Load GDS file and extract all layers from a single cell
     * @param gds_path Path to GDS file
     * @param cell_name Top cell name (auto-detect if empty)
     * @return Map of LayerKey -> LayerPolygons
     */
    std::map<LayerKey, LayerPolygons> extractFromFile(
        const std::string& gds_path,
        const std::string& cell_name = "");

    /**
     * Load GDS file and extract all layers from multiple cells
     * Results from all cells are merged into one map
     * @param gds_path Path to GDS file
     * @param cell_names List of cell names to extract (auto-detect if empty)
     * @return Map of LayerKey -> LayerPolygons
     */
    std::map<LayerKey, LayerPolygons> extractFromFileCells(
        const std::string& gds_path,
        const std::vector<std::string>& cell_names);

    /**
     * Get the cell bounding box from a GDS file without extracting polygons.
     * Uses KLayout's cached cell bbox (O(1) after file load).
     * @param gds_path Path to GDS file
     * @param cell_name Cell name (auto-detect top cell if empty)
     * @return Bounding box in micrometers, invalid if extraction fails
     */
    static GDSBoundingBox extractBoundingBox(
        const std::string& gds_path,
        const std::string& cell_name = "");

    // Get statistics from last extraction
    size_t lastLayerCount() const { return m_lastLayerCount; }
    size_t lastPolygonCount() const { return m_lastPolygonCount; }
    size_t lastPointCount() const { return m_lastPointCount; }

private:
    ExtractionConfig m_config;

    // Statistics
    size_t m_lastLayerCount = 0;
    size_t m_lastPolygonCount = 0;
    size_t m_lastPointCount = 0;
};

} // namespace chiplet

#endif // CHIPLET_VIEW3D_GDSLAYEREXTRACTOR_H
