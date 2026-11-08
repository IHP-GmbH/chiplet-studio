// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

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
#include "core/Component.h"   // Anchor enum
#include "core/LayerStackup.h"
#include "core/TechResolve.h"   // ShapeCount
#include "view2d/LayerProperties.h"
#include <algorithm>
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
    bool fill = false;      // the stackup marks this exact key `purpose: filler`
    size_t shapes = 0;      // polygons extruded into the mesh
    float opacity = 1.0f;   // 1.0 = solid; < 1.0 routes the layer into the transparent pass
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

    // GDS layers with shapes that the stackup places at no height, so no mesh
    // was built for them. Sorted by key.
    std::vector<LayerKey> unmodeled;

    // Component transform (position + rotation)
    QMatrix4x4 transform;

    // Centering offset (GDS origin to centered geometry), in mm
    // This is the offset that was applied to center the GDS geometry
    QVector2D centeringOffset;

    size_t layerCount() const { return layers.size(); }
    size_t totalTriangles() const;
};

/**
 * Show or hide every fill layer of `geometry` (LayerMesh::fill). The per-layer
 * user overrides are applied after this, so they still win.
 */
void setFillVisible(Component3DGeometry& geometry, bool visible);

/**
 * Whether the standard view draws a layer's mesh. Never a fill mesh: it holds
 * the first shapes in read order up to the read limit, which are one region,
 * not a sample. Fill is drawn from a read of every fill shape instead, as
 * coverage tiles (fillSource in SceneLoad.h).
 */
bool standardMeshDrawn(const LayerMesh& mesh);

/**
 * Where and how build() places one GDS layer: its height from the stackup
 * (exact key, else the layer number), its colour and material, and whether
 * it is fill. `placed` is false when the stackup has no height for it.
 */
struct LayerPlacement {
    bool placed = false;
    std::string name;
    double zBottom = 0.0;    // um, after the flip-chip inversion
    double thickness = 0.0;  // um
    QColor color;
    float metallic = 0.0f;
    float roughness = 0.5f;
    bool visible = true;
    bool fill = false;
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
    /**
     * @param anchor Mesh anchor convention. See coord_frame_contract.md
     *               §2 (BboxCenter centers the mesh on its own GDS bbox;
     *               GdsOrigin keeps centering at GDS (0,0)).
     */
    Component3DGeometry build(
        const std::map<LayerKey, LayerPolygons>& polygons,
        const LayerStackup& stackup,
        const LayerPropertiesFile* lyp = nullptr,
        const LayerColorScheme* colorScheme = nullptr,
        double scale = 1.0,
        bool flipZ = false,
        double beolTop = 0.0,
        Anchor anchor = Anchor::BboxCenter);

    /**
     * The placement build() gives `key`. The fast view places its layers with
     * it too, so both views put a layer at the same height in the same colour.
     */
    static LayerPlacement placeLayer(const LayerKey& key,
                                     const LayerStackup& stackup,
                                     const LayerPropertiesFile* lyp,
                                     const LayerColorScheme* colorScheme,
                                     bool flipZ,
                                     double beolTop);

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
    size_t maxPolygonsPerLayer() const { return m_maxPolygonsPerLayer; }
    // How many of `given` polygons buildLayerMesh extrudes: it stops at the
    // per-layer limit.
    size_t builtOf(size_t given) const { return std::min(given, m_maxPolygonsPerLayer); }
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

/**
 * What 3D draws of each layer the stackup places, against what the GDS holds:
 * the extractor's tally under `config`, then the builder's own limit. A layer
 * is placed as placeLayer places it (LayerStackup::heightFor), so a datatype
 * drawn at its layer number's height is counted too. Layers the stackup does
 * not place are left out; the unmodeled note names them. Sorted by key.
 */
std::vector<ShapeCount> shapeCounts(const std::map<LayerKey, LayerShapeTally>& tally,
                                    const ExtractionConfig& config,
                                    const LayerStackup& stackup,
                                    const LayerMeshBuilder& builder);

/**
 * One row of a component's layer list: each built mesh, then each placed
 * layer 3D built no mesh for but the GDS has shapes on. `count` and
 * `countNote` are empty when 3D draws every shape of the layer. A fill
 * layer's mesh is never drawn, so its count is the GDS's and it has no note.
 */
struct LayerRow {
    LayerKey key;
    std::string name;
    bool fill = false;
    bool meshed = true;
    std::string count;      // shapeCountLabel
    std::string countNote;  // shapeCountNote
};
std::vector<LayerRow> layerRows(const std::string& componentId,
                                const Component3DGeometry& geometry,
                                const std::vector<ShapeCount>& counts);

} // namespace chiplet

#endif // CHIPLET_VIEW3D_LAYERMESHBUILDER_H
