// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * AssemblyView.h - QOpenGLWidget for 3D assembly visualization
 */

#ifndef CHIPLET_VIEW3D_ASSEMBLYVIEW_H
#define CHIPLET_VIEW3D_ASSEMBLYVIEW_H

#include <QOpenGLWidget>
#include <QOpenGLExtraFunctions>
#include <QMatrix4x4>
#include <QRubberBand>
#include <QImage>
#include <QSize>
#include <map>
#include <vector>
#include <memory>

#include "Camera.h"
#include "ShaderProgram.h"
#include "ComponentMesh.h"
#include "SceneManager.h"
#include "math/BVH.h"
#include "core/Assembly.h"
#include "core/LayerStackup.h"
#include "view2d/LayerProperties.h"
#include "LayerMeshBuilder.h"
#include "ShapeFilter.h"
#include "SceneOverview.h"

class QTimer;

namespace chiplet {

/**
 * AssemblyView is the main 3D view widget for chiplet assemblies.
 */
class AssemblyView : public QOpenGLWidget, protected QOpenGLExtraFunctions {
    Q_OBJECT

public:
    explicit AssemblyView(QWidget* parent = nullptr);
    ~AssemblyView() override;

    // Set assembly to display
    void setAssembly(Assembly* assembly);
    Assembly* assembly() const { return m_assembly; }

    // Selection
    void selectComponent(const QString& componentId);
    QString selectedComponent() const { return m_selectedComponent; }

    // Visibility control
    void setComponentVisibility(const QString& componentId, bool visible);
    bool isComponentVisible(const QString& componentId) const;

    // Per-layer visibility within a component (drives LayerMode/Detailed rendering).
    // No-op if the component has no built geometry or no layer with the given
    // layer/datatype. Flips the cached LayerMesh.visible flag and repaints; no
    // geometry rebuild.
    void setLayerVisible(const QString& componentId, int layer, int datatype, bool visible);
    bool isLayerVisible(const QString& componentId, int layer, int datatype) const;

    // Per-layer opacity within a component (0.0..1.0; 1.0 = solid, the default).
    // A layer whose opacity is < 1.0 is forced into the depth-sorted transparent
    // pass so it blends instead of writing depth. Mirrors the visibility override:
    // intent is recorded even before geometry is built and survives rebuilds.
    void setLayerOpacity(const QString& componentId, int layer, int datatype, float opacity);
    float layerOpacity(const QString& componentId, int layer, int datatype) const;

    // The built render layers of a component (layer/datatype + name), in stack
    // order. Lets the UI list a die's layers for show/hide and transparency
    // when the technology ships no .lyp (e.g. a supported-PDK Import GDS), where
    // the layers come from the resolved PDK stackup rather than a properties
    // file. Empty if the component has no built layer geometry.
    struct LayerListEntry { int layer; int datatype; QString name; };
    std::vector<LayerListEntry> componentLayers(const QString& componentId) const;

    // Visualization-only Z exaggeration for Detailed-mode stackups: each layer's
    // baked Z is spread by this factor (gaps grow, layer thickness unchanged) so
    // the 3D stackup is easier to inspect. 1.0 = physical/default (no change);
    // larger spreads the layers apart. Does not rebuild geometry or alter the
    // saved model; purely a render-time transform. Reset by setting 1.0.
    //
    // Only meaningful for a single imported die: each layer is spread along its
    // OWN local Z origin, so in a multi-component assembly (chiplets + interposer
    // at different seating heights) the per-component fans interleave and overlap.
    // The render path therefore applies the spread only when layerZSpacingApplicable()
    // is true (exactly one component), and setAssembly resets the factor to 1.0.
    void setLayerZSpacing(float factor);
    float layerZSpacing() const { return m_layerZSpacing; }
    static constexpr float kMaxLayerZSpacing = 50.0f;

    // Whether the Layer-Z spread is meaningful for the current assembly: true
    // only when it holds exactly one component (a single imported GDS). The UI
    // greys the spacing control out otherwise. False when there is no assembly.
    bool layerZSpacingApplicable() const;

    // Camera control
    void fitToAssembly();
    void fitToComponent(const QString& componentId);

    // Top-down mini-map (SceneMiniMap) support. buildSceneOverview() returns the
    // full floor-plan model (per-component footprints projected onto the scene
    // X-Z plane + the camera marker); overviewCameraMarker() is the cheap
    // camera-only refresh for pan/zoom/orbit. navigateFloorTo() moves the look-at
    // target to a floor point (a mini-map click), zoomOverview() scales the
    // camera distance. All are floor-coordinate based (see SceneOverview.h) so no
    // scene/GL type leaks to the widget.
    SceneOverview buildSceneOverview() const;
    OverviewCameraMarker overviewCameraMarker() const;
    void navigateFloorTo(double floorX, double floorY);
    void zoomOverview(double factor);

    // Per-component render mode change notification
    void onComponentRenderModeChanged(const QString& componentId, RenderMode newMode);

    // Shape filter control (area-based polygon filtering for Detailed mode).
    // Per-component: each die / interposer keeps its own threshold so users can
    // declutter a noisy die without flattening the neighbours.
    void setShapeFilterPercent(const QString& componentId, double percent);
    double shapeFilterPercent(const QString& componentId) const;

    // Base plane visibility
    bool basePlaneVisible() const { return m_basePlaneVisible; }
    void setBasePlaneVisible(bool visible);

    // Offscreen high-resolution capture of the current 3D view. Renders the
    // scene into a (optionally multisampled) framebuffer at the requested pixel
    // size, independent of the widget's on-screen size, and returns the resolved
    // image. samples > 0 enables MSAA; transparentBackground clears to a 0-alpha
    // background instead of the usual dark clear color. Returns a null QImage if
    // the GL context is not ready or the framebuffer is unsupported. Must be
    // called from the GUI thread (it makes the widget's GL context current).
    QImage renderToImage(const QSize& size, bool transparentBackground, int samples);

signals:
    void componentClicked(const QString& componentId);
    void componentDoubleClicked(const QString& componentId);
    void selectionChanged(const QString& componentId);
    void shapeFilterChanged(const QString& componentId, double percent);

    // Emitted when the camera moves (orbit/pan/zoom/fit/navigate) so the mini-map
    // can refresh its viewport marker cheaply; and when the displayed content
    // changes (assembly set/cleared, meshes rebuilt) so it can rebuild the
    // footprint floor-plan.
    void cameraChanged();
    void overviewChanged();

protected:
    // OpenGL lifecycle
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    // Mouse events
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    void buildMeshes();
    void loadLayerProperties();
    void renderComponents();
    void renderGrid();
    QString pickComponent(int x, int y);
    void updateSceneBounds();
    void updateBasePlane();

    // Data
    Assembly* m_assembly = nullptr;
    QString m_selectedComponent;

    // Component visibility (true = visible, absent = visible by default)
    std::map<QString, bool> m_componentVisibility;

    // Per-component, per-layer user show/hide intent. Survives geometry rebuilds
    // (shape filter) and is the read-back source for the Properties panel
    // checkboxes. Absent entry = visible by default.
    std::map<QString, std::map<LayerKey, bool>> m_layerVisibilityOverride;

    // Per-component, per-layer user opacity intent (0.0..1.0). Same lifecycle as
    // m_layerVisibilityOverride (records intent, survives rebuilds, seeds the
    // Properties panel). Absent entry = 1.0 (fully solid).
    std::map<QString, std::map<LayerKey, float>> m_layerOpacityOverride;

    // Visualization-only Z exaggeration for Detailed stackups (1.0 = physical).
    float m_layerZSpacing = 1.0f;

    // Layer properties cache (technology_id -> LayerPropertiesFile)
    std::map<std::string, LayerPropertiesFile> m_layerProps;

    // Scene management
    SceneManager m_scene;

    // Rendering
    ShaderProgram m_componentShader;

    // Legacy per-component mesh map (for fallback/transition)
    std::map<QString, ComponentMesh> m_meshes;
    ComponentMesh m_gridMesh;
    bool m_basePlaneVisible = true;

    // Mouse state
    QPoint m_lastMousePos;
    bool m_isDragging = false;
    Qt::MouseButton m_dragButton = Qt::NoButton;

    // Rubber-band zoom (Ctrl+Left drag)
    bool m_isRubberBandZoom = false;
    QPoint m_rubberBandOrigin;
    QRubberBand* m_rubberBand = nullptr;
    void zoomToRect(const QRect& rect);

    // State
    bool m_initialized = false;
    bool m_needsRebuild = false;

    // Offscreen render-size override: when both > 0 the render path uses these
    // dimensions for the aspect ratio and viewport instead of the widget size.
    // Set only for the duration of renderToImage(); 0 means "use the widget".
    int m_renderWidth = 0;
    int m_renderHeight = 0;
    float currentAspect() const;

    // Layer geometry for 2.5D rendering (component_id -> geometry)
    std::map<QString, Component3DGeometry> m_layerGeometry;

    // Layer stackup cache (technology_id -> stackup)
    std::map<std::string, LayerStackup> m_stackups;
    // Whether the cached stackup came from a real PDK source (explicit
    // technology.stackup, BlenderGDS config, techfile, or a predefined PDK) as
    // opposed to the generic Priority-4 fallback. Gates the black-box stackup
    // augmentation: a real PDK stackup must NOT be augmented with stacked slabs.
    std::map<std::string, bool> m_stackupModeled;

    // Shape filter: polygon cache and area statistics per component.
    // m_shapeFilterByComponent stores the user's chosen percent per component
    // (absent = 0% / no filter). m_pendingFilterComponent is the next component
    // whose geometry the debounce timer should rebuild — set by every slider tick,
    // consumed by applyShapeFilter so only the touched component is rebuilt.
    std::map<QString, std::map<LayerKey, LayerPolygons>> m_polygonCache;
    std::map<QString, AreaStatistics> m_areaStats;
    std::map<QString, double> m_shapeFilterByComponent;
    QString m_pendingFilterComponent;
    QTimer* m_filterDebounceTimer = nullptr;

    // Helper to build layer geometry for a component
    void buildLayerGeometry(const Component& comp, const LayerPropertiesFile* lyp);
    void applyLayerOverrides(const QString& componentId);
    void applyShapeFilter();
    void rebuildFilteredGeometry(const QString& compId);

    // Multi-pass rendering helpers
    void renderOpaquePass(const std::vector<QString>& ids);
    void renderTransparentPass(const std::vector<QString>& ids);
    // Per-layer translucent layers (opacity < 1.0) of Detailed components, drawn
    // inside the transparent pass after the component-level transparent meshes.
    void renderTranslucentLayers();
    void renderWireframePass(const std::vector<QString>& ids);
    void sortBackToFront(std::vector<QString>& ids);
    VECTOR3D getComponentCenter(const QString& id) const;
    void setupShaderUniforms();

    // BVH for spatial acceleration
    std::unique_ptr<BVH> m_bvh;
    bool m_bvhDirty = true;
    std::vector<QString> m_meshIndexToId;  // Maps BVH indices to component IDs

    void ensureBVH();
};

} // namespace chiplet

#endif // CHIPLET_VIEW3D_ASSEMBLYVIEW_H
