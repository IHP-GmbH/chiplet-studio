/**
 * AssemblyView.h - QOpenGLWidget for 3D assembly visualization
 */

#ifndef CHIPLET_VIEW3D_ASSEMBLYVIEW_H
#define CHIPLET_VIEW3D_ASSEMBLYVIEW_H

#include <QOpenGLWidget>
#include <QOpenGLExtraFunctions>
#include <QMatrix4x4>
#include <QElapsedTimer>
#include <QRubberBand>
#include <map>
#include <vector>
#include <memory>

#include "Camera.h"
#include "ShaderProgram.h"
#include "ComponentMesh.h"
#include "SceneManager.h"
#include "ClipPlane.h"
#include "DitherPatterns.h"
#include "math/BVH.h"
#include "core/Assembly.h"
#include "core/LayerStackup.h"
#include "view2d/LayerProperties.h"
#include "gizmos/TransformGizmo.h"
#include "LayerMeshBuilder.h"
#include "ShapeFilter.h"

class QTimer;

namespace chiplet {

/**
 * View mode for 3D view (global rendering strategy)
 */
enum class ViewMode {
    BoxMode,      // Simple boxes (fast, schematic)
    LayerMode     // Layer-by-layer 2.5D visualization (like KLayout 2.5D)
};

/**
 * MeshInstanceGroup holds a shared mesh and per-instance data for instanced rendering.
 * Components with identical geometry (same type + dimensions) share a mesh.
 */
struct MeshInstanceGroup {
    ComponentMesh mesh;                        // Shared geometry
    std::vector<QMatrix4x4> transforms;        // Per-instance transforms
    std::vector<QColor> colors;                // Per-instance colors
    std::vector<bool> selected;                // Per-instance selection state
    std::vector<QString> componentIds;         // For picking/selection
    std::vector<AA_BOUNDING_BOX> boundingBoxes; // Per-instance world bounds

    // Update instance data on GPU
    void updateInstanceBuffer();
};

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

    // Camera control
    void fitToAssembly();
    void fitToComponent(const QString& componentId);
    void resetCamera();

    // Clip plane control
    ClipPlane& clipPlane() { return m_clipPlane; }
    const ClipPlane& clipPlane() const { return m_clipPlane; }
    void setClipEnabled(bool enabled);
    void setClipPosition(float position);
    void setClipAxis(ClipAxis axis);

    // View mode control (BoxMode vs LayerMode)
    ViewMode viewMode() const { return m_viewMode; }
    void setViewMode(ViewMode mode);

    // Per-component render mode change notification
    void onComponentRenderModeChanged(const QString& componentId, RenderMode newMode);

    // Shape filter control (area-based polygon filtering for Detailed mode)
    void setShapeFilterPercent(double percent);
    double shapeFilterPercent() const { return m_shapeFilterPercent; }

    // Base plane visibility
    bool basePlaneVisible() const { return m_basePlaneVisible; }
    void setBasePlaneVisible(bool visible);

signals:
    void viewModeChanged(ViewMode mode);
    void clipPlaneChanged();
    void componentClicked(const QString& componentId);
    void componentDoubleClicked(const QString& componentId);
    void selectionChanged(const QString& componentId);
    void shapeFilterChanged(double percent);

    // Command system signals (emitted when user requests actions)
    void moveComponentRequested(const QString& componentId, double dx, double dy, double dz);

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

    // Generate mesh signature for grouping identical components
    static QString getMeshSignature(const Component* comp);

    // Data
    Assembly* m_assembly = nullptr;
    QString m_selectedComponent;

    // Component visibility (true = visible, absent = visible by default)
    std::map<QString, bool> m_componentVisibility;

    // Per-component, per-layer user show/hide intent. Survives geometry rebuilds
    // (shape filter) and is the read-back source for the Properties panel
    // checkboxes. Absent entry = visible by default.
    std::map<QString, std::map<LayerKey, bool>> m_layerVisibilityOverride;

    // Layer properties cache (technology_id -> LayerPropertiesFile)
    std::map<std::string, LayerPropertiesFile> m_layerProps;

    // Scene management
    SceneManager m_scene;

    // Rendering
    ShaderProgram m_componentShader;
    ShaderProgram m_componentShaderInstanced;  // Instanced version
    ShaderProgram m_gridShader;

    // Instance groups for instanced rendering (signature -> group)
    std::map<QString, MeshInstanceGroup> m_instanceGroups;

    // Legacy per-component mesh map (for fallback/transition)
    std::map<QString, ComponentMesh> m_meshes;
    ComponentMesh m_gridMesh;
    bool m_basePlaneVisible = true;

    // Instancing enabled flag
    bool m_useInstancing = true;

    // Mouse state
    QPoint m_lastMousePos;
    bool m_isDragging = false;
    Qt::MouseButton m_dragButton = Qt::NoButton;

    // Rubber-band zoom (Ctrl+Left drag)
    bool m_isRubberBandZoom = false;
    QPoint m_rubberBandOrigin;
    QRubberBand* m_rubberBand = nullptr;
    void zoomToRect(const QRect& rect);

    // Gizmo
    TransformGizmo m_gizmo;
    GizmoAxis m_activeGizmoAxis = GizmoAxis::None;
    bool m_isDraggingGizmo = false;
    QVector3D m_gizmoDragStart;      // Component position at drag start
    QPoint m_gizmoDragMouseStart;    // Mouse position at drag start

    // State
    bool m_initialized = false;
    bool m_needsRebuild = false;

    // Clip plane
    ClipPlane m_clipPlane;

    // Dither patterns for layer fill styles
    DitherPatterns m_ditherPatterns;

    // View mode (BoxMode = simple boxes, LayerMode = 2.5D layer extrusion)
    ViewMode m_viewMode = ViewMode::LayerMode;  // Default to layer mode

    // Layer geometry for 2.5D rendering (component_id -> geometry)
    std::map<QString, Component3DGeometry> m_layerGeometry;

    // Layer stackup cache (technology_id -> stackup)
    std::map<std::string, LayerStackup> m_stackups;

    // Shape filter: polygon cache and area statistics per component
    std::map<QString, std::map<LayerKey, LayerPolygons>> m_polygonCache;
    std::map<QString, AreaStatistics> m_areaStats;
    double m_shapeFilterPercent = 0.0;
    QTimer* m_filterDebounceTimer = nullptr;

    // Helper to build layer geometry for a component
    void buildLayerGeometry(const Component& comp, const LayerPropertiesFile* lyp);
    void applyLayerVisibilityOverrides(const QString& componentId);
    void applyShapeFilter();
    void rebuildFilteredGeometry(const QString& compId);

    // Render layers for a component
    void renderLayerGeometry();

    // Multi-pass rendering helpers
    void renderOpaquePass(const std::vector<QString>& ids);
    void renderTransparentPass(const std::vector<QString>& ids);
    void renderWireframePass(const std::vector<QString>& ids);
    void sortBackToFront(std::vector<QString>& ids);
    VECTOR3D getComponentCenter(const QString& id) const;
    void setupShaderUniforms();

    // BVH for spatial acceleration
    std::unique_ptr<BVH> m_bvh;
    bool m_bvhDirty = true;
    std::vector<QString> m_meshIndexToId;  // Maps BVH indices to component IDs

    void ensureBVH();

    // Performance metrics
    QElapsedTimer m_frameTimer;
    int m_frameCount = 0;
    float m_fps = 0.0f;
    int m_drawCallCount = 0;
    bool m_showDebugStats = false;
    bool m_debugPrinted = false;  // Reset per assembly load

public:
    // Performance accessors
    float fps() const { return m_fps; }
    int drawCallCount() const { return m_drawCallCount; }
    void setShowDebugStats(bool show) { m_showDebugStats = show; }

    // Global Z offset for all components (in micrometers)
    void setGlobalZOffset(double offset_um);
    double globalZOffset() const { return m_globalZOffset; }

private:
    void updateTransforms();
    double m_globalZOffset = 0.0;
};

} // namespace chiplet

#endif // CHIPLET_VIEW3D_ASSEMBLYVIEW_H
