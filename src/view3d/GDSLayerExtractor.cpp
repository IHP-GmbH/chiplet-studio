// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * GDSLayerExtractor.cpp - Implementation
 */

#include "GDSLayerExtractor.h"
#include "core/Logger.h"
#include <QDebug>
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
            qCWarning(lcGds) << "No cells in layout";
            return result;
        }
        cell_index = *top_it;
    } else {
        auto found = layout->cell_by_name(cell_name.c_str());
        if (!found.first) {
            qCWarning(lcGds) << "Cell not found:" << cell_name.c_str();
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

    // Draw a via/contact layer's INDIVIDUAL cuts, exempt from min_polygon_area.
    // The cuts are sub-micron (interposer Via4 = 0.036 um^2) and would be culled
    // by the area filter, dropping the whole connector. Emitting the real cuts
    // (not a fused body) keeps the 3D view faithful to the physical via array --
    // pillars, as fab builds them. Dense layers are skipped by count so a
    // standard-cell die (~5e5 cuts per via layer) cannot stall load or swamp the
    // view; those internal vias are not the targeted connector and drew nothing
    // before this path existed.
    auto emitViaCuts = [&](unsigned int layer_index, LayerPolygons& out) {
        if (m_config.via_max_cuts > 0) {
            // Pre-count with an early-out (bounded to cap+1 iterations).
            size_t n = 0;
            for (db::RecursiveShapeIterator ci(*layout, cell, layer_index);
                 !ci.at_end(); ++ci) {
                if (++n > m_config.via_max_cuts) break;
            }
            if (n > m_config.via_max_cuts) {
                const db::LayerProperties& vp = layout->get_properties(layer_index);
                qCInfo(lcGds) << "via layer" << vp.layer << "/" << vp.datatype
                              << "skipped:" << (int)m_config.via_max_cuts
                              << "+ cuts exceed the via cap; not drawn (as before)";
                return;
            }
        }
        // No bound needed here: the pre-count above already skipped layers
        // over the cap, so a layer that reaches this loop draws all its cuts
        // (or the cap is disabled -> via_max_cuts == 0 -> draw all).
        for (db::RecursiveShapeIterator it(*layout, cell, layer_index);
             !it.at_end(); ++it) {
            const db::Shape& shape = it.shape();
            if (!(shape.is_polygon() || shape.is_box() || shape.is_path())) {
                continue;
            }
            db::Polygon db_poly;
            shape.polygon(db_poly);
            db_poly = db_poly.transformed(it.trans());
            SimplePolygon poly;
            poly.points.reserve(db_poly.hull().size());
            for (const auto& pt : db_poly.hull()) {
                poly.points.emplace_back(pt.x() * dbu, pt.y() * dbu);
            }
            if (poly.points.size() >= 3) {
                out.polygons.push_back(std::move(poly));
            }
        }
    };

    // Extract each layer
    for (unsigned int li : layer_indices) {
        const db::LayerProperties& lp = layout->get_properties(li);
        LayerKey key(lp.layer, lp.datatype);

        LayerPolygons layer_data;
        layer_data.key = key;
        layer_data.name = lp.name;

        // Via/contact layers: draw the individual cuts (physical pillars),
        // exempt from the area cull. Their cuts are sub-micron (interposer
        // Via4 = 0.19um = 0.036 um^2) and fall below min_polygon_area, so a
        // normal extract would delete the whole connector -- the M4<->M5
        // "no pillars" bug. The caller marks these from the stackup by name
        // (contains "via"/"con").
        if (m_config.via_layers.count(key) > 0) {
            emitViaCuts(li, layer_data);
            if (!layer_data.polygons.empty()) {
                result[key] = std::move(layer_data);
                ++m_lastLayerCount;
                m_lastPolygonCount += result[key].polygons.size();
                m_lastPointCount += result[key].totalPoints();
            }
            continue;
        }

        // Use recursive shape iterator to flatten hierarchy
        db::RecursiveShapeIterator shapes(*layout, cell, li);

        size_t poly_count = 0;
        bool saw_shape = false;

        for (; !shapes.at_end() && poly_count < m_config.max_polygons_per_layer; ++shapes) {
            const db::Shape& shape = shapes.shape();

            // Convert shape to simple polygon
            if (shape.is_polygon() || shape.is_box() || shape.is_path()) {
                saw_shape = true;
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

        // Backstop: the area cull must never fully empty a real (stackup-
        // modeled) layer. If a modeled layer had shapes but every one fell
        // below min_polygon_area, it is a cut array the name rule did not
        // recognize as a via (e.g. a PDK whose contact is named neither
        // "via" nor "con"). Draw its cuts anyway, exactly like a via, so no
        // modeled connector can silently vanish -- whatever the PDK names it.
        // Non-modeled layers (fill/noise not in the stackup) keep the plain
        // cull; they carry no z and would not render anyway.
        if (saw_shape && layer_data.polygons.empty() &&
            m_config.modeled_layers.count(key) > 0) {
            emitViaCuts(li, layer_data);
        }

        if (!layer_data.polygons.empty()) {
            result[key] = std::move(layer_data);
            ++m_lastLayerCount;
            m_lastPolygonCount += result[key].polygons.size();
            m_lastPointCount += result[key].totalPoints();
        }
    }

    qCDebug(lcGds) << "Extracted" << m_lastLayerCount << "layers,"
                   << m_lastPolygonCount << "polygons,"
                   << m_lastPointCount << "points";

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
        qCWarning(lcGds) << "Error loading" << gds_path.c_str() << ":" << e.msg().c_str();
        return result;
    } catch (const std::exception& e) {
        qCWarning(lcGds) << "Error loading" << gds_path.c_str() << ":" << e.what();
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

        qCDebug(lcGds) << "Extracted from" << cell_names.size() << "cells:"
                       << m_lastLayerCount << "layers,"
                       << m_lastPolygonCount << "polygons,"
                       << m_lastPointCount << "points";

        return result;

    } catch (const tl::Exception& e) {
        qCWarning(lcGds) << "Error loading" << gds_path.c_str() << ":" << e.msg().c_str();
        return result;
    } catch (const std::exception& e) {
        qCWarning(lcGds) << "Error loading" << gds_path.c_str() << ":" << e.what();
        return result;
    }
}

GDSBoundingBox GDSLayerExtractor::extractBoundingBox(
    const std::string& gds_path,
    const std::string& cell_name)
{
    GDSBoundingBox bbox;

    try {
        tl::InputStream stream(gds_path);
        db::Reader reader(stream);

        db::Layout layout;
        reader.read(layout);

        // Find the cell
        db::cell_index_type cell_index;
        if (cell_name.empty()) {
            auto top_it = layout.begin_top_down();
            if (top_it == layout.end_top_cells()) {
                return bbox;
            }
            cell_index = *top_it;
        } else {
            auto found = layout.cell_by_name(cell_name.c_str());
            if (!found.first) {
                return bbox;
            }
            cell_index = found.second;
        }

        const db::Cell& cell = layout.cell(cell_index);
        db::Box db_bbox = cell.bbox();

        if (!db_bbox.empty()) {
            double dbu = layout.dbu();
            bbox.x_min = db_bbox.left() * dbu;
            bbox.y_min = db_bbox.bottom() * dbu;
            bbox.x_max = db_bbox.right() * dbu;
            bbox.y_max = db_bbox.top() * dbu;
        }

    } catch (const tl::Exception& e) {
        qCWarning(lcGds) << "extractBoundingBox:" << e.msg().c_str();
    } catch (const std::exception& e) {
        qCWarning(lcGds) << "extractBoundingBox:" << e.what();
    }

    return bbox;
}

#else
// Stub implementations when KLayout is not available

GDSBoundingBox GDSLayerExtractor::extractBoundingBox(
    const std::string& /*gds_path*/,
    const std::string& /*cell_name*/)
{
    return {};
}

std::map<LayerKey, LayerPolygons> GDSLayerExtractor::extractFromFile(
    const std::string& /*gds_path*/,
    const std::string& /*cell_name*/)
{
    qCWarning(lcGds) << "KLayout not available";
    return {};
}

std::map<LayerKey, LayerPolygons> GDSLayerExtractor::extractFromFileCells(
    const std::string& /*gds_path*/,
    const std::vector<std::string>& /*cell_names*/)
{
    qCWarning(lcGds) << "KLayout not available";
    return {};
}

#endif // HAVE_KLAYOUT

} // namespace chiplet
