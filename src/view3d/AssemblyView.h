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

    // Camera control
    void fitToAssembly();
    void fitToComponent(const QString& componentId);

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

signals:
    void componentClicked(const QString& componentId);
    void componentDoubleClicked(const QString& componentId);
    void selectionChanged(const QString& componentId);
    void shapeFilterChanged(const QString& componentId, double percent);

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

    // Layer geometry for 2.5D rendering (component_id -> geometry)
    std::map<QString, Component3DGeometry> m_layerGeometry;

    // Layer stackup cache (technology_id -> stackup)
    std::map<std::string, LayerStackup> m_stackups;

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
    void applyLayerVisibilityOverrides(const QString& componentId);
    void applyShapeFilter();
    void rebuildFilteredGeometry(const QString& compId);

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
};

} // namespace chiplet

#endif // CHIPLET_VIEW3D_ASSEMBLYVIEW_H
