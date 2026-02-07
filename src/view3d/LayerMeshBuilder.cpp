/**
 * LayerMeshBuilder.cpp - Implementation
 */

#include "LayerMeshBuilder.h"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <QDebug>

// GDS3D tessellation engine - must include in this order
#include "gdselements.h"  // Point2D, Edge, Point3D classes
#include "gdspolygon.h"   // GDSPolygon with Tesselate()
#include "process_cfg.h"  // GDSUnits, ProcessLayer for mock context

namespace chiplet {

// =============================================================================
// Static member initialization
// =============================================================================

// GDS3D is the default tessellation engine (more robust for concave polygons)
// Legacy ear-clipping is available as fallback via setTessellatorMode()
LayerMeshBuilder::TessellatorMode LayerMeshBuilder::s_tessellatorMode =
    TessellatorMode::GDS3D;

void LayerMeshBuilder::setTessellatorMode(TessellatorMode mode)
{
    s_tessellatorMode = mode;
}

LayerMeshBuilder::TessellatorMode LayerMeshBuilder::tessellatorMode()
{
    return s_tessellatorMode;
}

// =============================================================================
// GDS3D tessellation bridge function
// =============================================================================

namespace {
    // Mock context for GDS3D tessellation (only used for epsilon/tolerance)
    // Units: 1nm precision, 1um user unit
    static GDSUnits s_mockUnits = {
        1e-9,   // Unitu: 1nm precision
        1e-6    // UserUnit: 1um
    };

    // Minimal ProcessLayer with valid Units pointer
    static ProcessLayer s_mockLayer = {
        nullptr,       // Next
        nullptr,       // Name
        nullptr,       // ProcessName
        nullptr,       // Virtual
        nullptr,       // Material
        nullptr,       // OutMaterial
        0,             // Layer
        0,             // Datatype
        0.0,           // Height
        1.0,           // Thickness
        &s_mockUnits,  // Units - CRITICAL: must point to valid GDSUnits
        false,         // Top
        false,         // Bottom
        1,             // Show
        0.5f, 0.5f, 0.5f, // Red, Green, Blue
        1.0f,          // Filter
        0,             // Metal
        0.0,           // MinSpace
        0.0,           // UnitSize
        0,             // Index
        0,             // LegendIndex
        false, false, false, // Alt, Ctrl, Shift
        0              // ShortKey
    };

    std::vector<int> triangulateWithGDS3D(const SimplePolygon& poly)
    {
        std::vector<int> result;
        size_t n = poly.points.size();
        if (n < 3) return result;

        // Create GDSPolygon with mock layer (needed for epsilon/tolerance in Tesselate())
        GDSPolygon gdsPoly(&s_mockLayer);

        // Check winding order: GDS3D expects counter-clockwise
        bool needsReverse = (poly.area() < 0);
        if (needsReverse) {
            for (size_t i = n; i > 0; --i) {
                gdsPoly.AddPoint(poly.points[i-1].x, poly.points[i-1].y);
            }
        } else {
            for (const auto& pt : poly.points) {
                gdsPoly.AddPoint(pt.x, pt.y);
            }
        }

        // Tessellate
        gdsPoly.Tesselate();

        // Extract triangle indices
        std::vector<size_t>* indices = gdsPoly.GetIndices();
        if (!indices || indices->empty()) return result;

        result.reserve(indices->size());

        // Map indices back to original polygon vertex order
        if (needsReverse) {
            // We reversed the points, so we need to map back
            // Original index i -> reversed index (n-1-i)
            // So reversed index j -> original index (n-1-j)
            for (size_t idx : *indices) {
                result.push_back(static_cast<int>(n - 1 - idx));
            }
            // Also need to reverse triangle winding for each triangle
            for (size_t i = 0; i + 2 < result.size(); i += 3) {
                std::swap(result[i + 1], result[i + 2]);
            }
        } else {
            for (size_t idx : *indices) {
                result.push_back(static_cast<int>(idx));
            }
        }

        return result;
    }
} // anonymous namespace

// =============================================================================
// LayerMesh / Component3DGeometry helpers
// =============================================================================

size_t LayerMesh::triangleCount() const
{
    return mesh.indexCount() / 3;
}

size_t Component3DGeometry::totalTriangles() const
{
    size_t total = 0;
    for (const auto& layer : layers) {
        total += layer.triangleCount();
    }
    return total;
}

// =============================================================================
// LayerMeshBuilder implementation
// =============================================================================

LayerMeshBuilder::LayerMeshBuilder() = default;

Component3DGeometry LayerMeshBuilder::build(
    const std::map<LayerKey, LayerPolygons>& polygons,
    const LayerStackup& stackup,
    const LayerPropertiesFile* lyp,
    double scale)
{
    Component3DGeometry result;

    // Convert um to mm for OpenGL (consistent with existing code)
    double unit_scale = scale / 1000.0;

    double max_z = 0.0;

    // Track the next auto z-position for layers not in stackup
    double auto_z = 0.0;
    const double default_layer_thickness = 1.0;  // 1um default thickness

    // =============================================================================
    // Calculate global bounding box to center the geometry at origin
    // =============================================================================
    double global_min_x = std::numeric_limits<double>::max();
    double global_min_y = std::numeric_limits<double>::max();
    double global_max_x = std::numeric_limits<double>::lowest();
    double global_max_y = std::numeric_limits<double>::lowest();
    bool has_polygons = false;

    for (const auto& [key, layer_polys] : polygons) {
        for (const auto& poly : layer_polys.polygons) {
            for (const auto& pt : poly.points) {
                if (pt.x < global_min_x) global_min_x = pt.x;
                if (pt.x > global_max_x) global_max_x = pt.x;
                if (pt.y < global_min_y) global_min_y = pt.y;
                if (pt.y > global_max_y) global_max_y = pt.y;
                has_polygons = true;
            }
        }
    }

    // Calculate center offset (to center GDS at origin)
    double center_offset_x = 0.0;
    double center_offset_y = 0.0;

    if (has_polygons) {
        center_offset_x = (global_min_x + global_max_x) / 2.0;
        center_offset_y = (global_min_y + global_max_y) / 2.0;

        std::cerr << "LayerMeshBuilder: GDS bounding box (um): "
                  << "X=[" << global_min_x << ", " << global_max_x << "], "
                  << "Y=[" << global_min_y << ", " << global_max_y << "]" << std::endl;
        std::cerr << "LayerMeshBuilder: Centering offset (um): ("
                  << center_offset_x << ", " << center_offset_y << ")" << std::endl;
    }

    // Store centering offset in result for reference (convert to mm)
    result.centeringOffset = QVector2D(
        static_cast<float>(center_offset_x / 1000.0),
        static_cast<float>(center_offset_y / 1000.0));

    for (const auto& [key, layer_polys] : polygons) {
        // Find layer elevation
        const LayerElevation* elev = stackup.find(key);

        // Create fallback elevation if not in stackup
        LayerElevation fallback_elev;
        if (!elev) {
            // Auto-assign z position based on layer number to spread them out
            fallback_elev.layer = key.layer;
            fallback_elev.datatype = key.datatype;
            fallback_elev.z_bottom = auto_z;
            fallback_elev.thickness = default_layer_thickness;
            fallback_elev.name = "Layer " + std::to_string(key.layer) + "/" + std::to_string(key.datatype);
            fallback_elev.visible = true;
            elev = &fallback_elev;
            auto_z += default_layer_thickness + 0.1;  // Small gap between layers

            std::cerr << "LayerMeshBuilder: Using auto-elevation for layer "
                      << key.layer << "/" << key.datatype
                      << " at z=" << fallback_elev.z_bottom << std::endl;
        }

        // Get color from .lyp if available
        QColor color(128, 128, 128, 200);  // Default gray
        if (lyp) {
            const LayerStyle* style = lyp->find(key);
            if (style) {
                color = QColor(style->fill_color.r, style->fill_color.g,
                               style->fill_color.b, style->fill_color.a);
            }
        } else {
            // Generate a unique color based on layer number if no .lyp
            int hue = (key.layer * 47 + key.datatype * 31) % 360;
            color = QColor::fromHsv(hue, 200, 200, 200);
        }

        // Build mesh for this layer (pass centering offset in um, before unit_scale)
        LayerMesh layer_mesh = buildLayerMesh(
            layer_polys,
            elev->z_bottom,
            elev->thickness,
            color,
            unit_scale,
            center_offset_x,
            center_offset_y);

        layer_mesh.key = key;
        layer_mesh.name = elev->name;
        layer_mesh.visible = elev->visible;

        if (layer_mesh.z_top > max_z) {
            max_z = layer_mesh.z_top;
        }

        result.layers.push_back(std::move(layer_mesh));
    }

    result.total_height = max_z;

    std::cerr << "LayerMeshBuilder: Built " << result.layers.size()
              << " layer meshes, " << result.totalTriangles() << " triangles"
              << std::endl;

    return result;
}

LayerMesh LayerMeshBuilder::buildLayerMesh(
    const LayerPolygons& polygons,
    double z_bottom,
    double thickness,
    const QColor& color,
    double scale,
    double center_offset_x,
    double center_offset_y)
{
    LayerMesh result;
    result.key = polygons.key;
    result.name = polygons.name;
    result.z_bottom = z_bottom * scale;
    result.z_top = (z_bottom + thickness) * scale;
    result.color = color;

    std::vector<Vertex> vertices;
    std::vector<GLuint> indices;

    size_t poly_count = 0;
    for (const auto& poly : polygons.polygons) {
        if (poly_count >= m_maxPolygonsPerLayer) break;
        if (poly.size() < 3) continue;

        addExtrudedPolygon(vertices, indices, poly,
                          z_bottom, z_bottom + thickness, scale,
                          center_offset_x, center_offset_y);
        ++poly_count;
    }

    result.mesh.setVertices(vertices);
    result.mesh.setIndices(indices);
    result.mesh.setColor(color);

    return result;
}

void LayerMeshBuilder::addExtrudedPolygon(
    std::vector<Vertex>& vertices,
    std::vector<GLuint>& indices,
    const SimplePolygon& poly,
    double z_bottom,
    double z_top,
    double scale,
    double center_offset_x,
    double center_offset_y)
{
    // Add top face
    addPolygonFace(vertices, indices, poly, z_top, false, scale,
                   center_offset_x, center_offset_y);

    // Add bottom face
    addPolygonFace(vertices, indices, poly, z_bottom, true, scale,
                   center_offset_x, center_offset_y);

    // Add side walls
    if (m_generateSideWalls) {
        addSideWalls(vertices, indices, poly, z_bottom, z_top, scale,
                     center_offset_x, center_offset_y);
    }
}

void LayerMeshBuilder::addPolygonFace(
    std::vector<Vertex>& vertices,
    std::vector<GLuint>& indices,
    const SimplePolygon& poly,
    double z,
    bool flip_normal,
    double scale,
    double center_offset_x,
    double center_offset_y)
{
    if (poly.size() < 3) return;

    // Triangulate polygon
    std::vector<int> tri_indices = triangulatePolygon(poly);
    if (tri_indices.empty()) return;

    GLuint base_idx = static_cast<GLuint>(vertices.size());

    // Normal direction (Y is now up, so flip_normal affects Y component)
    float ny = flip_normal ? -1.0f : 1.0f;

    // Add vertices
    // Coordinate mapping: GDS X -> 3D X, GDS Y -> 3D Z, Layer Z -> 3D Y
    // Apply centering offset to center GDS geometry at origin
    for (const auto& pt : poly.points) {
        Vertex v;
        v.position[0] = static_cast<float>((pt.x - center_offset_x) * scale);  // GDS X -> 3D X (centered)
        v.position[1] = static_cast<float>(z * scale);                          // Layer Z -> 3D Y (vertical)
        v.position[2] = static_cast<float>((pt.y - center_offset_y) * scale);  // GDS Y -> 3D Z (centered)
        v.normal[0] = 0.0f;
        v.normal[1] = ny;   // Normal points up/down in Y
        v.normal[2] = 0.0f;
        vertices.push_back(v);
    }

    // Add triangles
    if (flip_normal) {
        // Reverse winding for bottom face
        for (size_t i = 0; i < tri_indices.size(); i += 3) {
            indices.push_back(base_idx + tri_indices[i]);
            indices.push_back(base_idx + tri_indices[i + 2]);
            indices.push_back(base_idx + tri_indices[i + 1]);
        }
    } else {
        for (int idx : tri_indices) {
            indices.push_back(base_idx + idx);
        }
    }
}

void LayerMeshBuilder::addSideWalls(
    std::vector<Vertex>& vertices,
    std::vector<GLuint>& indices,
    const SimplePolygon& poly,
    double z_bottom,
    double z_top,
    double scale,
    double center_offset_x,
    double center_offset_y)
{
    size_t n = poly.points.size();
    if (n < 3) return;

    for (size_t i = 0; i < n; ++i) {
        size_t j = (i + 1) % n;

        const Point2D& p0 = poly.points[i];
        const Point2D& p1 = poly.points[j];

        // Calculate outward normal (perpendicular to edge in X-Z plane)
        // Edge direction: (dx, dy) in GDS coords -> (dx, dz) in 3D coords
        double dx = p1.x - p0.x;
        double dy = p1.y - p0.y;
        double len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-10) continue;

        // Normal perpendicular to edge in X-Z plane (Y is up)
        float nx = static_cast<float>(-dy / len);
        float nz = static_cast<float>(dx / len);

        GLuint base_idx = static_cast<GLuint>(vertices.size());

        // Four corners of the side wall quad
        // Coordinate mapping: GDS X -> 3D X, GDS Y -> 3D Z, Layer Z -> 3D Y
        // Apply centering offset to center GDS geometry at origin
        Vertex v0, v1, v2, v3;

        // Bottom-left corner
        v0.position[0] = static_cast<float>((p0.x - center_offset_x) * scale);  // GDS X -> 3D X (centered)
        v0.position[1] = static_cast<float>(z_bottom * scale);                   // Layer Z -> 3D Y (vertical)
        v0.position[2] = static_cast<float>((p0.y - center_offset_y) * scale);  // GDS Y -> 3D Z (centered)
        v0.normal[0] = nx;
        v0.normal[1] = 0.0f;
        v0.normal[2] = nz;

        // Bottom-right corner
        v1.position[0] = static_cast<float>((p1.x - center_offset_x) * scale);
        v1.position[1] = static_cast<float>(z_bottom * scale);
        v1.position[2] = static_cast<float>((p1.y - center_offset_y) * scale);
        v1.normal[0] = nx;
        v1.normal[1] = 0.0f;
        v1.normal[2] = nz;

        // Top-right corner
        v2.position[0] = static_cast<float>((p1.x - center_offset_x) * scale);
        v2.position[1] = static_cast<float>(z_top * scale);
        v2.position[2] = static_cast<float>((p1.y - center_offset_y) * scale);
        v2.normal[0] = nx;
        v2.normal[1] = 0.0f;
        v2.normal[2] = nz;

        // Top-left corner
        v3.position[0] = static_cast<float>((p0.x - center_offset_x) * scale);
        v3.position[1] = static_cast<float>(z_top * scale);
        v3.position[2] = static_cast<float>((p0.y - center_offset_y) * scale);
        v3.normal[0] = nx;
        v3.normal[1] = 0.0f;
        v3.normal[2] = nz;

        vertices.push_back(v0);
        vertices.push_back(v1);
        vertices.push_back(v2);
        vertices.push_back(v3);

        // Two triangles for the quad
        indices.push_back(base_idx + 0);
        indices.push_back(base_idx + 1);
        indices.push_back(base_idx + 2);

        indices.push_back(base_idx + 0);
        indices.push_back(base_idx + 2);
        indices.push_back(base_idx + 3);
    }
}

// =============================================================================
// Tessellation - GDS3D with automatic fallback to legacy ear-clipping
// =============================================================================

std::vector<int> LayerMeshBuilder::triangulatePolygon(const SimplePolygon& poly)
{
    // Use GDS3D tessellation if enabled
    if (s_tessellatorMode == TessellatorMode::GDS3D) {
        auto result = triangulateWithGDS3D(poly);

        // Fallback if GDS3D returns empty result for valid polygon
        if (result.empty() && poly.points.size() >= 3) {
            qWarning() << "GDS3D tessellation failed for polygon with"
                       << poly.points.size() << "vertices, falling back to legacy ear-clipping";
            return triangulatePolygonLegacy(poly);
        }
        return result;
    }

    // Legacy mode
    return triangulatePolygonLegacy(poly);
}

// =============================================================================
// Legacy ear-clipping triangulation (fallback)
// =============================================================================

std::vector<int> LayerMeshBuilder::triangulatePolygonLegacy(const SimplePolygon& poly)
{
    std::vector<int> result;

    size_t n = poly.points.size();
    if (n < 3) return result;

    // Copy points to working array
    std::vector<Point2D> pts = poly.points;

    // Ensure counter-clockwise winding
    if (poly.area() < 0) {
        std::reverse(pts.begin(), pts.end());
    }

    // Simple case: triangle
    if (n == 3) {
        result = {0, 1, 2};
        return result;
    }

    // Simple case: quad
    if (n == 4) {
        result = {0, 1, 2, 0, 2, 3};
        return result;
    }

    // Build active vertex list
    std::vector<int> active;
    active.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        active.push_back(static_cast<int>(i));
    }

    // Identify reflex vertices
    std::vector<bool> reflex(n, false);
    for (size_t i = 0; i < n; ++i) {
        int prev = (i + n - 1) % n;
        int next = (i + 1) % n;
        reflex[i] = !isConvex(pts[prev], pts[i], pts[next]);
    }

    // Ear clipping loop
    int max_iterations = static_cast<int>(n * n);
    int iterations = 0;

    while (active.size() > 3 && iterations < max_iterations) {
        bool found_ear = false;

        for (size_t i = 0; i < active.size(); ++i) {
            int curr = active[i];
            int prev = active[(i + active.size() - 1) % active.size()];
            int next = active[(i + 1) % active.size()];

            if (isEar(pts, curr, prev, next, reflex)) {
                // Add triangle
                result.push_back(prev);
                result.push_back(curr);
                result.push_back(next);

                // Remove ear vertex
                active.erase(active.begin() + i);
                found_ear = true;
                break;
            }
        }

        if (!found_ear) {
            // Failed to find ear - use fallback triangulation
            break;
        }

        ++iterations;
    }

    // Add remaining triangle
    if (active.size() == 3) {
        result.push_back(active[0]);
        result.push_back(active[1]);
        result.push_back(active[2]);
    }

    return result;
}

bool LayerMeshBuilder::isEar(const std::vector<Point2D>& pts, int i, int prev, int next,
                              const std::vector<bool>& reflex)
{
    // Ear must be convex
    if (!isConvex(pts[prev], pts[i], pts[next])) {
        return false;
    }

    // Check that no reflex vertex is inside the ear triangle
    for (size_t k = 0; k < pts.size(); ++k) {
        int ki = static_cast<int>(k);
        if (ki == prev || ki == i || ki == next) continue;
        if (!reflex[k]) continue;

        if (pointInTriangle(pts[k], pts[prev], pts[i], pts[next])) {
            return false;
        }
    }

    return true;
}

bool LayerMeshBuilder::isConvex(const Point2D& prev, const Point2D& curr, const Point2D& next)
{
    return cross2D(prev, curr, next) > 0;
}

bool LayerMeshBuilder::pointInTriangle(const Point2D& p, const Point2D& a,
                                         const Point2D& b, const Point2D& c)
{
    double d1 = cross2D(a, b, p);
    double d2 = cross2D(b, c, p);
    double d3 = cross2D(c, a, p);

    bool has_neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    bool has_pos = (d1 > 0) || (d2 > 0) || (d3 > 0);

    return !(has_neg && has_pos);
}

double LayerMeshBuilder::cross2D(const Point2D& o, const Point2D& a, const Point2D& b)
{
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

} // namespace chiplet
