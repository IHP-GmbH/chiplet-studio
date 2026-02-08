/**
 * LayerMeshBuilder.h - Build 3D meshes from layer polygons
 *
 * Converts 2D polygon data into extruded 3D meshes suitable for OpenGL rendering.
 * Uses ear-clipping triangulation for polygon surfaces.
 */

#ifndef CHIPLET_VIEW3D_LAYERMESHBUILDER_H
#define CHIPLET_VIEW3D_LAYERMESHBUILDER_H

#include "ComponentMesh.h"
#include "GDSLayerExtractor.h"
#include "core/LayerStackup.h"
#include "view2d/LayerProperties.h"
#include <map>
#include <vector>

namespace chiplet {

/**
 * Mesh data for a single layer (all polygons combined)
 */
struct LayerMesh {
    LayerKey key;
    std::string name;
    ComponentMesh mesh;
    QColor color;
    double z_bottom = 0.0;
    double z_top = 0.0;
    bool visible = true;
    float metallic = 0.0f;
    float roughness = 0.5f;

    size_t triangleCount() const;
};

/**
 * Complete 3D mesh data for a component's layers
 */
struct Component3DGeometry {
    QString componentId;
    std::vector<LayerMesh> layers;
    double total_height = 0.0;

    // Component transform (position + rotation)
    QMatrix4x4 transform;

    // Centering offset (GDS origin to centered geometry), in mm
    // This is the offset that was applied to center the GDS geometry
    QVector2D centeringOffset;

    size_t layerCount() const { return layers.size(); }
    size_t totalTriangles() const;
};

/**
 * LayerMeshBuilder creates 3D meshes from 2D polygons
 */
class LayerMeshBuilder {
public:
    LayerMeshBuilder();

    /**
     * Tessellation engine mode
     */
    enum class TessellatorMode {
        GDS3D,   // Use GDS3D's robust tessellation (default)
        Legacy   // Use legacy ear-clipping implementation
    };

    /**
     * Set the tessellation engine mode
     */
    static void setTessellatorMode(TessellatorMode mode);

    /**
     * Get the current tessellation engine mode
     */
    static TessellatorMode tessellatorMode();

    /**
     * Build 3D geometry for a component from its layer polygons
     * @param polygons Map of layer -> polygons
     * @param stackup Layer stackup for Z positions
     * @param lyp Layer properties for colors
     * @param scale Scale factor (usually 1.0 for um)
     * @return Complete 3D geometry
     */
    Component3DGeometry build(
        const std::map<LayerKey, LayerPolygons>& polygons,
        const LayerStackup& stackup,
        const LayerPropertiesFile* lyp = nullptr,
        const LayerColorScheme* colorScheme = nullptr,
        double scale = 1.0);

    /**
     * Build a single layer mesh
     * @param center_offset_x X offset to subtract (um) to center geometry at origin
     * @param center_offset_y Y offset to subtract (um) to center geometry at origin
     */
    LayerMesh buildLayerMesh(
        const LayerPolygons& polygons,
        double z_bottom,
        double thickness,
        const QColor& color,
        double scale = 1.0,
        double center_offset_x = 0.0,
        double center_offset_y = 0.0);

    /**
     * Triangulate a simple polygon
     * Uses GDS3D tessellation by default with automatic fallback to legacy
     * Returns list of triangle indices (3 per triangle)
     */
    static std::vector<int> triangulatePolygon(const SimplePolygon& poly);

    /**
     * Legacy ear-clipping triangulation (fallback)
     * Returns list of triangle indices (3 per triangle)
     */
    static std::vector<int> triangulatePolygonLegacy(const SimplePolygon& poly);

    // Configuration
    void setMaxPolygonsPerLayer(size_t max) { m_maxPolygonsPerLayer = max; }
    void setGenerateSideWalls(bool gen) { m_generateSideWalls = gen; }

private:
    // Add extruded polygon to mesh
    void addExtrudedPolygon(
        std::vector<Vertex>& vertices,
        std::vector<GLuint>& indices,
        const SimplePolygon& poly,
        double z_bottom,
        double z_top,
        double scale,
        double center_offset_x,
        double center_offset_y);

    // Add top/bottom face
    void addPolygonFace(
        std::vector<Vertex>& vertices,
        std::vector<GLuint>& indices,
        const SimplePolygon& poly,
        double z,
        bool flip_normal,
        double scale,
        double center_offset_x,
        double center_offset_y);

    // Add side walls
    void addSideWalls(
        std::vector<Vertex>& vertices,
        std::vector<GLuint>& indices,
        const SimplePolygon& poly,
        double z_bottom,
        double z_top,
        double scale,
        double center_offset_x,
        double center_offset_y);

    // Ear clipping helpers
    static bool isEar(const std::vector<Point2D>& pts, int i, int prev, int next,
                      const std::vector<bool>& reflex);
    static bool isConvex(const Point2D& prev, const Point2D& curr, const Point2D& next);
    static bool pointInTriangle(const Point2D& p, const Point2D& a,
                                 const Point2D& b, const Point2D& c);
    static double cross2D(const Point2D& o, const Point2D& a, const Point2D& b);

    size_t m_maxPolygonsPerLayer = 50000;
    bool m_generateSideWalls = true;

    // Tessellation mode (default: GDS3D)
    static TessellatorMode s_tessellatorMode;
};

} // namespace chiplet

#endif // CHIPLET_VIEW3D_LAYERMESHBUILDER_H
