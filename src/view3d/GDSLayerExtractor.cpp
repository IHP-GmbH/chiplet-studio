/**
 * GDSLayerExtractor.cpp - Implementation
 */

#include "GDSLayerExtractor.h"
#include <iostream>
#include <cmath>

#ifdef HAVE_KLAYOUT
#include "dbLayout.h"
#include "dbReader.h"
#include "dbCell.h"
#include "dbRecursiveShapeIterator.h"
#include "dbLayerProperties.h"
#include "dbRegion.h"
#include "tlStream.h"
#include "tlException.h"
#endif

namespace chiplet {

// =============================================================================
// SimplePolygon implementation
// =============================================================================

void SimplePolygon::boundingBox(double& minX, double& minY,
                                  double& maxX, double& maxY) const
{
    if (points.empty()) {
        minX = minY = maxX = maxY = 0.0;
        return;
    }

    minX = maxX = points[0].x;
    minY = maxY = points[0].y;

    for (size_t i = 1; i < points.size(); ++i) {
        if (points[i].x < minX) minX = points[i].x;
        if (points[i].x > maxX) maxX = points[i].x;
        if (points[i].y < minY) minY = points[i].y;
        if (points[i].y > maxY) maxY = points[i].y;
    }
}

double SimplePolygon::area() const
{
    if (points.size() < 3) return 0.0;

    double sum = 0.0;
    size_t n = points.size();

    for (size_t i = 0; i < n; ++i) {
        size_t j = (i + 1) % n;
        sum += points[i].x * points[j].y;
        sum -= points[j].x * points[i].y;
    }

    return sum * 0.5;
}

size_t LayerPolygons::totalPoints() const
{
    size_t total = 0;
    for (const auto& poly : polygons) {
        total += poly.points.size();
    }
    return total;
}

// =============================================================================
// GDSLayerExtractor implementation
// =============================================================================

GDSLayerExtractor::GDSLayerExtractor() = default;
GDSLayerExtractor::~GDSLayerExtractor() = default;

#ifdef HAVE_KLAYOUT

std::map<LayerKey, LayerPolygons> GDSLayerExtractor::extract(
    db::Layout* layout,
    const std::string& cell_name,
    const std::vector<LayerKey>& layers)
{
    std::map<LayerKey, LayerPolygons> result;
    m_lastLayerCount = 0;
    m_lastPolygonCount = 0;
    m_lastPointCount = 0;

    if (!layout) {
        return result;
    }

    // Find the cell to extract
    db::cell_index_type cell_index;
    if (cell_name.empty()) {
        // Use first top cell
        auto top_it = layout->begin_top_down();
        if (top_it == layout->end_top_cells()) {
            std::cerr << "GDSLayerExtractor: No cells in layout" << std::endl;
            return result;
        }
        cell_index = *top_it;
    } else {
        auto found = layout->cell_by_name(cell_name.c_str());
        if (!found.first) {
            std::cerr << "GDSLayerExtractor: Cell not found: " << cell_name << std::endl;
            return result;
        }
        cell_index = found.second;
    }

    const db::Cell& cell = layout->cell(cell_index);
    double dbu = layout->dbu();

    // Determine which layers to extract
    std::set<unsigned int> layer_indices;

    if (layers.empty()) {
        // Extract all layers
        for (unsigned int li = 0; li < layout->layers(); ++li) {
            if (layout->is_valid_layer(li)) {
                layer_indices.insert(li);
            }
        }
    } else {
        // Extract specified layers
        for (const auto& key : layers) {
            db::LayerProperties lp(key.layer, key.datatype);
            int li = layout->get_layer_maybe(lp);
            if (li >= 0) {
                layer_indices.insert(static_cast<unsigned int>(li));
            }
        }
    }

    // Extract each layer
    for (unsigned int li : layer_indices) {
        const db::LayerProperties& lp = layout->get_properties(li);
        LayerKey key(lp.layer, lp.datatype);

        LayerPolygons layer_data;
        layer_data.key = key;
        layer_data.name = lp.name;

        // Use recursive shape iterator to flatten hierarchy
        db::RecursiveShapeIterator shapes(*layout, cell, li);

        size_t poly_count = 0;

        for (; !shapes.at_end() && poly_count < m_config.max_polygons_per_layer; ++shapes) {
            const db::Shape& shape = shapes.shape();

            // Convert shape to simple polygon
            if (shape.is_polygon() || shape.is_box() || shape.is_path()) {
                db::Polygon db_poly;
                shape.polygon(db_poly);

                // Apply transformation from hierarchy
                db::ICplxTrans trans = shapes.trans();
                db_poly = db_poly.transformed(trans);

                // Convert to SimplePolygon
                SimplePolygon poly;
                poly.points.reserve(db_poly.hull().size());

                for (const auto& pt : db_poly.hull()) {
                    poly.points.emplace_back(
                        pt.x() * dbu,
                        pt.y() * dbu
                    );
                }

                // Skip tiny polygons
                if (m_config.min_polygon_area > 0) {
                    double area = std::abs(poly.area());
                    if (area < m_config.min_polygon_area) {
                        continue;
                    }
                }

                layer_data.polygons.push_back(std::move(poly));
                ++poly_count;
            }
        }

        if (!layer_data.polygons.empty()) {
            result[key] = std::move(layer_data);
            ++m_lastLayerCount;
            m_lastPolygonCount += result[key].polygons.size();
            m_lastPointCount += result[key].totalPoints();
        }
    }

    std::cerr << "GDSLayerExtractor: Extracted " << m_lastLayerCount << " layers, "
              << m_lastPolygonCount << " polygons, "
              << m_lastPointCount << " points" << std::endl;

    return result;
}

LayerPolygons GDSLayerExtractor::extractLayer(
    db::Layout* layout,
    const std::string& cell_name,
    const LayerKey& layer)
{
    std::vector<LayerKey> layers = { layer };
    auto result = extract(layout, cell_name, layers);

    if (result.find(layer) != result.end()) {
        return std::move(result[layer]);
    }

    return LayerPolygons();
}

std::map<LayerKey, LayerPolygons> GDSLayerExtractor::extractFromFile(
    const std::string& gds_path,
    const std::string& cell_name)
{
    std::map<LayerKey, LayerPolygons> result;

    try {
        tl::InputStream stream(gds_path);
        db::Reader reader(stream);

        db::Layout layout;
        reader.read(layout);

        return extract(&layout, cell_name, {});

    } catch (const tl::Exception& e) {
        std::cerr << "GDSLayerExtractor: Error loading " << gds_path
                  << ": " << e.msg() << std::endl;
        return result;
    } catch (const std::exception& e) {
        std::cerr << "GDSLayerExtractor: Error loading " << gds_path
                  << ": " << e.what() << std::endl;
        return result;
    }
}

std::map<LayerKey, LayerPolygons> GDSLayerExtractor::extractFromFileCells(
    const std::string& gds_path,
    const std::vector<std::string>& cell_names)
{
    std::map<LayerKey, LayerPolygons> result;

    // If no cells specified, fall back to default (auto-detect top cell)
    if (cell_names.empty()) {
        return extractFromFile(gds_path, "");
    }

    // If only one cell, use simpler method
    if (cell_names.size() == 1) {
        return extractFromFile(gds_path, cell_names[0]);
    }

    try {
        tl::InputStream stream(gds_path);
        db::Reader reader(stream);

        db::Layout layout;
        reader.read(layout);

        m_lastLayerCount = 0;
        m_lastPolygonCount = 0;
        m_lastPointCount = 0;

        // Extract from each cell and merge results
        for (const auto& cell_name : cell_names) {
            auto cell_result = extract(&layout, cell_name, {});

            // Merge into combined result
            for (auto& [key, layer_data] : cell_result) {
                if (result.find(key) == result.end()) {
                    // New layer - just add it
                    result[key] = std::move(layer_data);
                } else {
                    // Existing layer - append polygons
                    auto& existing = result[key];
                    existing.polygons.insert(
                        existing.polygons.end(),
                        std::make_move_iterator(layer_data.polygons.begin()),
                        std::make_move_iterator(layer_data.polygons.end())
                    );
                }
            }
        }

        // Update statistics
        m_lastLayerCount = result.size();
        m_lastPolygonCount = 0;
        m_lastPointCount = 0;
        for (const auto& [key, layer_data] : result) {
            m_lastPolygonCount += layer_data.polygons.size();
            m_lastPointCount += layer_data.totalPoints();
        }

        std::cerr << "GDSLayerExtractor: Extracted from " << cell_names.size() << " cells: "
                  << m_lastLayerCount << " layers, "
                  << m_lastPolygonCount << " polygons, "
                  << m_lastPointCount << " points" << std::endl;

        return result;

    } catch (const tl::Exception& e) {
        std::cerr << "GDSLayerExtractor: Error loading " << gds_path
                  << ": " << e.msg() << std::endl;
        return result;
    } catch (const std::exception& e) {
        std::cerr << "GDSLayerExtractor: Error loading " << gds_path
                  << ": " << e.what() << std::endl;
        return result;
    }
}

#else
// Stub implementations when KLayout is not available

std::map<LayerKey, LayerPolygons> GDSLayerExtractor::extractFromFile(
    const std::string& /*gds_path*/,
    const std::string& /*cell_name*/)
{
    std::cerr << "GDSLayerExtractor: KLayout not available" << std::endl;
    return {};
}

std::map<LayerKey, LayerPolygons> GDSLayerExtractor::extractFromFileCells(
    const std::string& /*gds_path*/,
    const std::vector<std::string>& /*cell_names*/)
{
    std::cerr << "GDSLayerExtractor: KLayout not available" << std::endl;
    return {};
}

#endif // HAVE_KLAYOUT

} // namespace chiplet
