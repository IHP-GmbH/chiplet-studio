// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * AssemblyView.cpp - 3D assembly view implementation
 */

#include "AssemblyView.h"
#include "MeshBuilder.h"
#include "CoordFrame.h"
#include "GdsPlacement.h"
#include "GDSLayerExtractor.h"
#include "LayerMeshBuilder.h"
#include "SceneLoad.h"
#include "ShapeFilter.h"
#include "ViewCount.h"
#include "core/TechResolve.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <QTimer>
#include <QDebug>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFramebufferObjectFormat>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>
#include <set>
#include <cmath>
#include <algorithm>

namespace chiplet {

// Shader source embedded in code (loaded from resources in production)
// Non-instanced vertex shader (legacy)
static const char* componentVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;

uniform mat4 modelViewProjection;
uniform mat4 modelView;
uniform mat4 model;
uniform mat3 normalMatrix;

out vec3 fragNormal;
out vec3 fragPosition;
out float flogz;

void main() {
    fragNormal = normalMatrix * normal;
    fragPosition = vec3(modelView * vec4(position, 1.0));
    gl_Position = modelViewProjection * vec4(position, 1.0);
    flogz = 1.0 + gl_Position.w;
}
)";

// Non-instanced fragment shader (legacy)
static const char* componentFragmentShader = R"(
#version 330 core
in vec3 fragNormal;
in vec3 fragPosition;
in float flogz;

uniform vec4 objectColor;
uniform vec3 lightDirection;
uniform bool selected;
uniform float Fcoef_half;

out vec4 FragColor;

void main() {
    vec3 norm = normalize(fragNormal);
    float ambient = 0.3;
    float diffuse = max(dot(norm, -lightDirection), 0.0) * 0.6;
    float lighting = ambient + diffuse;

    vec3 color = objectColor.rgb * lighting;

    if (selected) {
        color = mix(color, vec3(1.0, 0.8, 0.0), 0.3);
    }

    FragColor = vec4(color, objectColor.a);
    gl_FragDepth = log2(max(1e-6, flogz)) * Fcoef_half;
}
)";

AssemblyView::AssemblyView(QWidget* parent)
    : QOpenGLWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    // Shape filter debounce timer (re-tessellation is expensive)
    m_filterDebounceTimer = new QTimer(this);
    m_filterDebounceTimer->setSingleShot(true);
    m_filterDebounceTimer->setInterval(150);
    connect(m_filterDebounceTimer, &QTimer::timeout, this, &AssemblyView::applyShapeFilter);

    m_fastWatcher = new QFutureWatcher<std::shared_ptr<FastRead>>(this);
    connect(m_fastWatcher, &QFutureWatcherBase::finished, this, &AssemblyView::onFastRead);
    // The rows and the notes follow the camera once it has stopped, not every
    // frame, so the text does not churn while dragging.
    m_settleTimer = new QTimer(this);
    m_settleTimer->setSingleShot(true);
    m_settleTimer->setInterval(250);
    connect(m_settleTimer, &QTimer::timeout, this, &AssemblyView::settleFast);
}

AssemblyView::~AssemblyView()
{
    // makeCurrent() needs a valid context and surface; during application
    // shutdown the surface may already be gone, in which case the driver has
    // freed our GL objects with the context and calling glDelete* is unsafe.
    // Guard the GL teardown on a valid context.
    const bool haveGL = context() && context()->isValid();
    if (haveGL) {
        makeCurrent();
    }
    m_meshes.clear();
    m_layerGeometry.clear();
    m_polygonCache.clear();
    m_areaStats.clear();
    if (haveGL) {
        m_fastGL.release(this);
        m_gridMesh.release();
        // Delete the GL program while the context is still current. The member
        // ShaderProgram destructor otherwise runs after doneCurrent() and calls
        // glDeleteProgram with no current context.
        m_componentShader.destroy();
        doneCurrent();
    }
}


void AssemblyView::setAssembly(Assembly* assembly)
{
    m_assembly = assembly;
    m_needsRebuild = true;
    m_selectedComponent.clear();
    m_layerProps.clear();
    m_polygonCache.clear();
    m_areaStats.clear();
    m_layerVisibilityOverride.clear();
    m_layerOpacityOverride.clear();
    m_shapeFilterByComponent.clear();
    m_pendingFilterComponent.clear();
    m_layerZSpacing = 1.0f;  // Z spread is per-import; never carry it across loads.
    m_showFill = false;      // fill is off at every open
    m_sheetNotes.clear();
    m_componentNotes.clear();
    m_shapeCounts.clear();
    // The setting stays; what it read was this assembly's.
    ++m_fastGeneration;
    dropFastFrame();
    m_fastSources.clear();
    m_fast.clear();
    m_fastKeys.clear();
    m_fastBudget = kMaxPreparedRecords;
    m_fastUploaded = false;

    if (!assembly) {
        if (m_initialized) {
            makeCurrent();
            m_meshes.clear();
            m_fastGL.upload(this, {});
            doneCurrent();
        }
        emit overviewChanged();
        emit notesChanged();
        update();
        return;
    }

    if (m_initialized) {
        makeCurrent();
        m_fastGL.upload(this, {});
        try {
            loadLayerProperties();
            buildMeshes();
            updateSceneBounds();
            updateBasePlane();
            fitToAssembly();
        } catch (const std::exception& e) {
            qWarning() << "Exception during mesh building:" << e.what();
        } catch (...) {
            qWarning() << "Unknown exception during mesh building";
        }
        doneCurrent();
        readFastShapes();
    }

    emit overviewChanged();
    emit notesChanged();
    update();
}

void AssemblyView::selectComponent(const QString& componentId)
{
    if (m_selectedComponent != componentId) {
        QString oldSelection = m_selectedComponent;
        m_selectedComponent = componentId;

            // Non-instanced: update mesh selection state
            if (!oldSelection.isEmpty() && m_meshes.count(oldSelection)) {
                m_meshes[oldSelection].setSelected(false);
            }
            if (!componentId.isEmpty() && m_meshes.count(componentId)) {
                m_meshes[componentId].setSelected(true);
            }

        emit selectionChanged(componentId);
        update();
    }
}

void AssemblyView::setComponentVisibility(const QString& componentId, bool visible)
{
    if (componentId.isEmpty()) {
        return;
    }

    m_componentVisibility[componentId] = visible;
    update();
    emit notesChanged();  // whether fill is coming depends on it (layersDrawn)
}

bool AssemblyView::isComponentVisible(const QString& componentId) const
{
    auto it = m_componentVisibility.find(componentId);
    if (it != m_componentVisibility.end()) {
        return it->second;
    }
    return true;  // Default to visible
}

bool AssemblyView::layersDrawn(const Component& comp) const
{
    const RenderMode mode = comp.render_mode();
    return (mode == RenderMode::Detailed || mode == RenderMode::DetailedNoSubstrate)
        && isComponentVisible(QString::fromStdString(comp.id()));
}

void AssemblyView::setLayerVisible(const QString& componentId, int layer, int datatype, bool visible)
{
    if (componentId.isEmpty()) {
        return;
    }

    // Record the intent first so it survives geometry rebuilds (e.g. shape filter)
    // and can be read back by the Properties panel, even if no geometry exists yet.
    m_layerVisibilityOverride[componentId][LayerKey(layer, datatype)] = visible;

    auto it = m_layerGeometry.find(componentId);
    if (it == m_layerGeometry.end()) {
        return;  // No built geometry yet; override applies when it is built
    }

    bool changed = false;
    for (LayerMesh& mesh : it->second.layers) {
        if (mesh.key.layer == layer && mesh.key.datatype == datatype) {
            if (mesh.visible != visible) {
                mesh.visible = visible;
                changed = true;
            }
        }
    }

    // The fast view draws layers the standard view has no mesh for.
    if (changed || m_shapeMode != ShapeMode::Standard) {
        update();
    }
}

bool AssemblyView::isLayerVisible(const QString& componentId, int layer, int datatype) const
{
    // User intent wins (set even when the component has no built geometry).
    auto co = m_layerVisibilityOverride.find(componentId);
    if (co != m_layerVisibilityOverride.end()) {
        auto lo = co->second.find(LayerKey(layer, datatype));
        if (lo != co->second.end()) {
            return lo->second;
        }
    }

    auto it = m_layerGeometry.find(componentId);
    if (it != m_layerGeometry.end()) {
        for (const LayerMesh& mesh : it->second.layers) {
            if (mesh.key.layer == layer && mesh.key.datatype == datatype) {
                return mesh.visible;
            }
        }
    }
    // A layer only the fast view draws: as its mesh would start.
    auto f = m_fast.find(componentId);
    if (f != m_fast.end()) {
        for (const FastLayer& l : f->second.layers) {
            if (l.key.layer == layer && l.key.datatype == datatype) {
                return l.place.fill ? m_showFill : l.place.visible;
            }
        }
    }
    return true;  // No override, no geometry: visible by default
}

void AssemblyView::setLayerOpacity(const QString& componentId, int layer, int datatype, float opacity)
{
    if (componentId.isEmpty()) {
        return;
    }

    opacity = std::clamp(opacity, 0.0f, 1.0f);

    // Record the intent first so it survives geometry rebuilds (e.g. shape filter)
    // and can be read back by the Properties panel, even if no geometry exists yet.
    m_layerOpacityOverride[componentId][LayerKey(layer, datatype)] = opacity;

    auto it = m_layerGeometry.find(componentId);
    if (it == m_layerGeometry.end()) {
        return;  // No built geometry yet; override applies when it is built
    }

    bool changed = false;
    for (LayerMesh& mesh : it->second.layers) {
        if (mesh.key.layer == layer && mesh.key.datatype == datatype) {
            if (mesh.opacity != opacity) {
                mesh.opacity = opacity;
                changed = true;
            }
        }
    }

    if (changed || m_shapeMode != ShapeMode::Standard) {
        update();
    }
}

float AssemblyView::layerOpacity(const QString& componentId, int layer, int datatype) const
{
    // User intent wins (set even when the component has no built geometry).
    auto co = m_layerOpacityOverride.find(componentId);
    if (co != m_layerOpacityOverride.end()) {
        auto lo = co->second.find(LayerKey(layer, datatype));
        if (lo != co->second.end()) {
            return lo->second;
        }
    }

    auto it = m_layerGeometry.find(componentId);
    if (it != m_layerGeometry.end()) {
        for (const LayerMesh& mesh : it->second.layers) {
            if (mesh.key.layer == layer && mesh.key.datatype == datatype) {
                return mesh.opacity;
            }
        }
    }
    return 1.0f;  // No override, no geometry: fully solid by default
}

std::vector<AssemblyView::LayerListEntry>
AssemblyView::componentLayers(const QString& componentId) const
{
    std::vector<LayerListEntry> entries;
    auto it = m_layerGeometry.find(componentId);
    if (it == m_layerGeometry.end()) {
        return entries;
    }
    static const std::vector<ShapeCount> none;
    auto counts = m_shapeCounts.find(componentId);
    std::vector<LayerRow> rows = layerRows(componentId.toStdString(), it->second,
                                           counts == m_shapeCounts.end() ? none
                                                                         : counts->second);
    if (auto f = m_fast.find(componentId); f != m_fast.end()) {
        static const std::map<LayerKey, LayerBuckets> unsettled;
        auto b = m_settledBuckets.find(componentId);
        const auto& buckets = b == m_settledBuckets.end() ? unsettled : b->second;
        if (shapesPrepared()) {
            rows = fastLayerRows(std::move(rows), f->second, buckets);
        } else if (m_shapeMode == ShapeMode::Standard && m_showFill && fillPrepared()) {
            rows = fastLayerRows(std::move(rows), f->second, buckets, true);
        }
    }
    for (const LayerRow& row : rows) {
        entries.push_back({row.key.layer, row.key.datatype,
                           QString::fromStdString(row.name), row.fill,
                           QString::fromStdString(row.count),
                           QString::fromStdString(row.countNote), row.meshed});
    }
    return entries;
}

std::optional<AssemblyView::DrawnLayer>
AssemblyView::drawnLayer(const QString& componentId, int layer, int datatype) const
{
    auto it = m_layerGeometry.find(componentId);
    if (it == m_layerGeometry.end()) {
        return std::nullopt;
    }
    for (const LayerMesh& mesh : it->second.layers) {
        if (mesh.key.layer == layer && mesh.key.datatype == datatype) {
            return DrawnLayer{mesh.z_bottom, mesh.z_top, mesh.shapes};
        }
    }
    return std::nullopt;
}

void AssemblyView::setLayerZSpacing(float factor)
{
    factor = std::clamp(factor, 1.0f, kMaxLayerZSpacing);
    if (factor == m_layerZSpacing) {
        return;
    }
    m_layerZSpacing = factor;
    update();
}

bool AssemblyView::layerZSpacingApplicable() const
{
    // Spreading each layer about its own local Z origin only reads correctly for
    // a lone die; with several components seated at different heights the fans
    // interleave. A single imported GDS synthesizes exactly one component.
    return m_assembly && m_assembly->components().size() == 1;
}

void AssemblyView::setShowFill(bool show)
{
    m_showFill = show;
    for (auto& [compId, geometry] : m_layerGeometry) {
        auto vis = m_layerVisibilityOverride.find(compId);
        if (vis != m_layerVisibilityOverride.end()) {
            for (const LayerMesh& mesh : geometry.layers) {
                if (mesh.fill) {
                    vis->second.erase(mesh.key);
                }
            }
        }
        setFillVisible(geometry, show);
    }
    for (const auto& [compId, fast] : m_fast) {
        auto vis = m_layerVisibilityOverride.find(compId);
        if (vis == m_layerVisibilityOverride.end()) continue;
        for (const FastLayer& l : fast.layers) {
            if (l.place.fill) vis->second.erase(l.key);
        }
    }
    if (m_shapeMode == ShapeMode::Standard) {
        // Standard's fill frame follows the switch: read when on, dropped when off.
        dropFastFrame();
        readFastShapes();
        emit layerRowsChanged();
    }
    update();
    emit notesChanged();  // the strip names the dies fill does not show on
}

bool AssemblyView::hasFill() const
{
    for (const auto& [compId, geometry] : m_layerGeometry) {
        for (const LayerMesh& mesh : geometry.layers) {
            if (mesh.fill) {
                return true;
            }
        }
    }
    for (const auto& [compId, fast] : m_fast) {
        for (const FastLayer& l : fast.layers) {
            if (l.place.fill) return true;
        }
    }
    return false;
}

QStringList AssemblyView::notes() const
{
    QStringList out;
    if (!m_assembly) {
        for (const auto& n : m_sheetNotes) {
            out << QString::fromStdString(n);
        }
        return out;
    }
    ViewNotesState state{m_shapeMode, shapesPrepared(), m_everyFrameSeconds};
    state.showFill = m_showFill;
    state.fillPrepared = fillPrepared();
    state.undetailed = undetailedParts(*m_assembly, m_layerGeometry);
    for (const auto& n : viewNotes(state, *m_assembly, m_sheetNotes, m_componentNotes,
                                   m_shapeCounts, m_fast, m_settledCoverage)) {
        out << QString::fromStdString(n);
    }
    return out;
}

void AssemblyView::applyLayerOverrides(const QString& componentId)
{
    auto it = m_layerGeometry.find(componentId);
    if (it == m_layerGeometry.end()) {
        return;
    }

    // Fill's default comes first so a per-layer choice on a fill layer wins.
    setFillVisible(it->second, m_showFill);

    // Visibility and opacity are independent overrides; apply whichever exist.
    auto vis = m_layerVisibilityOverride.find(componentId);
    auto opa = m_layerOpacityOverride.find(componentId);
    if (vis == m_layerVisibilityOverride.end() && opa == m_layerOpacityOverride.end()) {
        return;
    }

    for (LayerMesh& mesh : it->second.layers) {
        if (vis != m_layerVisibilityOverride.end()) {
            auto lo = vis->second.find(mesh.key);
            if (lo != vis->second.end()) {
                mesh.visible = lo->second;
            }
        }
        if (opa != m_layerOpacityOverride.end()) {
            auto lo = opa->second.find(mesh.key);
            if (lo != opa->second.end()) {
                mesh.opacity = lo->second;
            }
        }
    }
}

void AssemblyView::fitToAssembly()
{
    m_scene.fitToScene(currentAspect());
    emit cameraChanged();
    update();
}

void AssemblyView::fitToComponent(const QString& componentId)
{
    if (componentId.isEmpty()) {
        fitToAssembly();
        return;
    }

    // Search layer geometry first (LayerMode is the default)
    auto lgIt = m_layerGeometry.find(componentId);
    if (lgIt != m_layerGeometry.end()) {
        const Component3DGeometry& geom = lgIt->second;

        AA_BOUNDING_BOX compBounds;
        bool first = true;
        for (const auto& layer : geom.layers) {
            const auto wb = transformedMeshBounds(layer.mesh.boundingBox(), geom.transform);
            if (first) {
                compBounds = wb;
                first = false;
            } else {
                compBounds.AddBounds(wb);
            }
        }

        if (!first) {
            m_scene.camera().fitToBox(compBounds, currentAspect());
            emit cameraChanged();
            update();
            return;
        }
    }

    // Fallback: non-instanced meshes
    auto it = m_meshes.find(componentId);
    if (it != m_meshes.end()) {
        m_scene.camera().fitToBox(it->second.boundingBox(), currentAspect());
        emit cameraChanged();
        update();
        return;
    }

    // Last resort: compute from component data
    if (m_assembly) {
        Component* comp = m_assembly->component(componentId.toStdString());
        if (comp) {
            const auto& pos = comp->position();
            const auto& dims = comp->dimensions();
            AA_BOUNDING_BOX box;
            VECTOR3D mins(static_cast<float>(pos.x),
                          static_cast<float>(pos.y),
                          static_cast<float>(pos.z));
            VECTOR3D maxes(static_cast<float>(pos.x + dims.width),
                           static_cast<float>(pos.y + dims.height),
                           static_cast<float>(pos.z + dims.thickness));
            box.SetFromMinsMaxes(mins, maxes);
            m_scene.camera().fitToBox(box, currentAspect());
            emit cameraChanged();
            update();
            return;
        }
    }
}

OverviewCameraMarker AssemblyView::overviewCameraMarker() const
{
    const Camera& cam = m_scene.camera();
    const VECTOR3D t = cam.target();
    return computeCameraMarker(t.x, t.z, cam.distance(), cam.fov(),
                               cam.yaw(), cam.pitch(),
                               static_cast<double>(currentAspect()));
}

SceneOverview AssemblyView::buildSceneOverview() const
{
    SceneOverview ov;

    // Floor extent from the current scene bounds (geometry only; the base plane
    // is excluded on purpose so the map frames the actual assembly).
    const AA_BOUNDING_BOX& sb = m_scene.sceneBounds();
    ov.floorBounds = floorRectFromSceneXZ(sb.mins.x, sb.mins.z, sb.maxes.x, sb.maxes.z);

    // One footprint per component. Every component has a colored box mesh in
    // world/scene coordinates (buildMeshes), which already carries the GDS
    // footprint extent, so the box bounding box is the right source here.
    for (const auto& [id, mesh] : m_meshes) {
        const AA_BOUNDING_BOX& b = mesh.boundingBox();
        OverviewFootprint fp;
        fp.id = id.toStdString();
        fp.rect = floorRectFromSceneXZ(b.mins.x, b.mins.z, b.maxes.x, b.maxes.z);
        fp.rgb = static_cast<unsigned int>(mesh.color().rgb() & 0x00FFFFFFu);
        fp.selected = (id == m_selectedComponent);
        ov.footprints.push_back(fp);
    }

    ov.camera = overviewCameraMarker();
    return ov;
}

void AssemblyView::navigateFloorTo(double floorX, double floorY)
{
    Camera& cam = m_scene.camera();
    double sceneX = 0.0, sceneZ = 0.0;
    sceneFromFloor(floorX, floorY, sceneX, sceneZ);
    const VECTOR3D t = cam.target();
    // Keep the current elevation (target Y) and orbit/zoom; only slide the look-at
    // across the floor so the user flies to a region without changing the angle.
    cam.setTarget(VECTOR3D(static_cast<float>(sceneX), t.y, static_cast<float>(sceneZ)));
    emit cameraChanged();
    update();
}

void AssemblyView::zoomOverview(double factor)
{
    if (!(factor > 0.0)) {
        return;
    }
    Camera& cam = m_scene.camera();
    cam.setDistance(cam.distance() * static_cast<float>(factor));
    emit cameraChanged();
    update();
}

void AssemblyView::initializeGL()
{
    initializeOpenGLFunctions();

    // Set clear color (dark gray background)
    glClearColor(0.15f, 0.15f, 0.18f, 1.0f);

    // Enable depth testing
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    // Enable blending for transparency
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Enable face culling
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    // Load shaders
    if (!m_componentShader.loadFromSource(componentVertexShader, componentFragmentShader)) {
        qWarning() << "Failed to load component shader";
    }
    if (!m_fastGL.init(this)) {
        qWarning() << "Fast 3D shapes are not available: their shaders did not build";
    }

    // Build solid base plane mesh (instead of grid lines)
    m_gridMesh = MeshBuilder::buildPlaneMesh(100.0f);
    m_gridMesh.upload();

    m_initialized = true;

    // Build meshes if assembly was set before initialization
    if (m_assembly && m_needsRebuild) {
        try {
            loadLayerProperties();
            buildMeshes();
            updateSceneBounds();
            fitToAssembly();
            emit overviewChanged();
            emit notesChanged();
        } catch (const std::exception& e) {
            qCritical() << "AssemblyView::initializeGL: Exception during mesh building:" << e.what();
        } catch (...) {
            qCritical() << "AssemblyView::initializeGL: Unknown exception during mesh building";
        }
        readFastShapes();
    }
}

void AssemblyView::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
    if (w > 0 && h > 0) {
        m_scene.camera().setViewportAspect(static_cast<float>(w) / h);
        emit cameraChanged();
    }
}

void AssemblyView::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (!m_initialized) {
        return;
    }

    // renderGrid/renderComponents build their own matrices from the camera
    // internally.
    QElapsedTimer frame;
    frame.start();
    renderGrid();
    renderComponents();
    if (m_fastActive && m_shapeMode == ShapeMode::Every) {
        // Every shape says what its last frame cost, measured, not quoted.
        glFinish();
        m_everyFrameSeconds = frame.nsecsElapsed() * 1e-9;
    }
    if (m_fastActive) {
        m_settleTimer->start();
    }
}

QImage AssemblyView::renderToImage(const QSize& size, bool transparentBackground,
                                   int samples)
{
    if (!m_initialized || size.width() <= 0 || size.height() <= 0) {
        return QImage();
    }

    makeCurrent();

    QOpenGLFramebufferObjectFormat format;
    format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
    if (samples > 0) {
        format.setSamples(samples);
    }

    auto fbo = std::make_unique<QOpenGLFramebufferObject>(size, format);
    if (!fbo->isValid() && samples > 0) {
        // Multisampling may be unsupported at this resolution; fall back to a
        // plain framebuffer so the capture still succeeds (supersampling from a
        // large size already gives most of the quality).
        QOpenGLFramebufferObjectFormat plain;
        plain.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        fbo = std::make_unique<QOpenGLFramebufferObject>(size, plain);
    }
    if (!fbo->isValid() || !fbo->bind()) {
        qWarning() << "AssemblyView::renderToImage: framebuffer unsupported at"
                   << size << "with" << samples << "samples";
        doneCurrent();
        return QImage();
    }

    // Drive the projection aspect and the viewport from the target size so the
    // capture is correct at any resolution, not just multiples of the widget.
    m_renderWidth = size.width();
    m_renderHeight = size.height();
    glViewport(0, 0, size.width(), size.height());

    if (transparentBackground) {
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    try {
        renderGrid();
        renderComponents();
    } catch (const std::exception& e) {
        qWarning() << "AssemblyView::renderToImage: render exception:" << e.what();
    } catch (...) {
        qWarning() << "AssemblyView::renderToImage: unknown render exception";
    }

    QImage image = fbo->toImage();  // resolves MSAA and flips vertically

    fbo->release();

    // Restore everything the offscreen pass changed before the normal on-screen
    // paint path runs again.
    m_renderWidth = 0;
    m_renderHeight = 0;
    if (transparentBackground) {
        glClearColor(0.15f, 0.15f, 0.18f, 1.0f);
    }
    const qreal dpr = devicePixelRatioF();
    glViewport(0, 0, static_cast<int>(width() * dpr),
               static_cast<int>(height() * dpr));

    doneCurrent();

    // The widget's own framebuffer was not the bound target; force a clean
    // repaint of the on-screen view.
    update();

    if (!image.isNull()) {
        image = image.convertToFormat(transparentBackground
                                          ? QImage::Format_ARGB32
                                          : QImage::Format_RGB32);
    }
    return image;
}

void AssemblyView::loadLayerProperties()
{
    m_layerProps.clear();
    m_sheetNotes.clear();

    if (!m_assembly) {
        return;
    }

    LayerSheets sheets = loadLayerSheets(*m_assembly);
    m_layerProps = std::move(sheets.props);
    m_sheetNotes = std::move(sheets.notes);
}

void AssemblyView::buildMeshes()
{
    m_meshes.clear();
    m_layerGeometry.clear();
    m_stackups.clear();
    m_componentNotes.clear();
    m_shapeCounts.clear();

    if (!m_assembly) {
        m_needsRebuild = false;
        return;
    }

    if (m_assembly->components().empty()) {
        m_needsRebuild = false;
        return;
    }

    StandardScene scene = loadStandardScene(*m_assembly, m_layerProps, m_shapeFilterByComponent);
    for (auto& [compId, mesh] : scene.boxes) {
        mesh.upload();
        m_meshes.emplace(compId, std::move(mesh));
    }
    m_stackups = std::move(scene.stackups);
    for (auto& [compId, loaded] : scene.components) {
        adoptStandardComponent(compId, std::move(loaded));
    }

    m_needsRebuild = false;
    m_bvhDirty = true;
}

void AssemblyView::ensureBVH()
{
    if (!m_bvhDirty && m_bvh) {
        return;
    }

    // Build index mapping and collect bounding boxes
    m_meshIndexToId.clear();
    std::vector<AA_BOUNDING_BOX> boxes;
    std::vector<int> indices;

    int idx = 0;

        // Non-instanced: one entry per mesh
        for (const auto& [id, mesh] : m_meshes) {
            m_meshIndexToId.push_back(id);
            boxes.push_back(mesh.boundingBox());
            indices.push_back(idx);
            ++idx;
        }

    // Build or rebuild the BVH
    if (!m_bvh) {
        m_bvh = std::make_unique<BVH>();
    }
    m_bvh->build(boxes, indices);
    m_bvhDirty = false;
}

void AssemblyView::renderComponents()
{
    if (!m_assembly || m_meshes.empty() || !m_componentShader.isValid()) {
        return;
    }
    prepareFastFrame();

    // Classify components by render mode
    std::vector<QString> opaqueIds;
    std::vector<QString> transparentIds;
    std::vector<QString> wireframeIds;

    for (auto& [id, mesh] : m_meshes) {
        if (!isComponentVisible(id)) continue;

        Component* comp = m_assembly->component(id.toStdString());
        if (!comp) continue;

        RenderMode mode = comp->render_mode();
        if (mode == RenderMode::Hidden) continue;

        switch (mode) {
            case RenderMode::Wireframe:
                wireframeIds.push_back(id);
                break;
            case RenderMode::Transparent:
                transparentIds.push_back(id);
                break;
            case RenderMode::Solid:
            case RenderMode::Detailed:
            case RenderMode::DetailedNoSubstrate:
                opaqueIds.push_back(id);
                break;
            default:
                break;
        }
    }

    // Pass 1: Opaque objects (depth write ON)
    glDepthMask(GL_TRUE);
    renderOpaquePass(opaqueIds);
    drawFastLayers(false);

    // Pass 2: Transparent objects (depth write OFF, sorted back-to-front)
    glDepthMask(GL_FALSE);
    sortBackToFront(transparentIds);
    renderTransparentPass(transparentIds);
    drawFastLayers(true);

    // Pass 3: Wireframe overlays (depth write OFF)
    renderWireframePass(wireframeIds);

    // Restore depth write
    glDepthMask(GL_TRUE);
}

float AssemblyView::currentAspect() const
{
    // Offscreen capture overrides the size so the projection aspect matches the
    // target framebuffer; otherwise use the on-screen widget size (unchanged
    // behavior). Guard against a zero height during early layout.
    const int w = (m_renderWidth > 0) ? m_renderWidth : width();
    const int h = (m_renderHeight > 0) ? m_renderHeight : height();
    return (h > 0) ? static_cast<float>(w) / static_cast<float>(h) : 1.0f;
}

void AssemblyView::setupShaderUniforms()
{
    float aspect = currentAspect();
    MATRIX4X4 viewMat = m_scene.camera().viewMatrix();
    MATRIX4X4 projMat = m_scene.camera().projectionMatrix(aspect);
    VECTOR3D lightDir = m_scene.lightDirection();

    QMatrix4x4 view, projection;
    for (int i = 0; i < 16; ++i) {
        view.data()[i] = viewMat.GetEntry(i);
        projection.data()[i] = projMat.GetEntry(i);
    }

    QMatrix4x4 mvp = projection * view;
    QMatrix3x3 normalMat = view.normalMatrix();
    QMatrix4x4 model;
    model.setToIdentity();

    m_componentShader.setUniformMat4("modelViewProjection", mvp);
    m_componentShader.setUniformMat4("modelView", view);
    m_componentShader.setUniformMat4("model", model);
    m_componentShader.setUniformMat3("normalMatrix", normalMat);
    m_componentShader.setUniformVec3("lightDirection",
                                     lightInView(view, QVector3D(lightDir.x, lightDir.y, lightDir.z)));
    m_componentShader.setUniformFloat("Fcoef_half", m_scene.camera().fcoef() * 0.5f);
}

void AssemblyView::renderOpaquePass(const std::vector<QString>& ids)
{
    if (ids.empty()) return;

    m_componentShader.bind();
    setupShaderUniforms();

    // setupShaderUniforms() leaves the shader with the pass-level identity model
    // (box meshes bake world positions into their vertices). A Detailed
    // component overwrites model/modelView/mvp with its own transform below;
    // track that so the next box mesh restores the identity matrices instead of
    // inheriting the previous component's transform.
    bool baseMatricesActive = true;

    for (const QString& id : ids) {
        Component* comp = m_assembly->component(id.toStdString());
        if (!comp) continue;

        bool isSelected = (id == m_selectedComponent);

        // If Detailed (with or without substrate) and has layer geometry, render that
        bool isDetailedMode = (comp->render_mode() == RenderMode::Detailed)
                           || (comp->render_mode() == RenderMode::DetailedNoSubstrate);
        bool hideSubstrate  = (comp->render_mode() == RenderMode::DetailedNoSubstrate);
        const bool drawsLayers = isDetailedMode && m_layerGeometry.count(id);

        // A face-down die's layer geometry is mirrored (componentTransform),
        // which reverses its triangles' winding. Its box is built in place,
        // not mirrored, and is culled as any other box.
        bool isFlipped = drawsLayers && comp->orientation() == Orientation::FaceDown;
        if (isFlipped) glFrontFace(GL_CW);

        if (drawsLayers) {
            auto& geometry = m_layerGeometry[id];

            float aspect = currentAspect();
            MATRIX4X4 viewMat = m_scene.camera().viewMatrix();
            MATRIX4X4 projMat = m_scene.camera().projectionMatrix(aspect);
            QMatrix4x4 view, projection;
            for (int i = 0; i < 16; ++i) {
                view.data()[i] = viewMat.GetEntry(i);
                projection.data()[i] = projMat.GetEntry(i);
            }

            QMatrix4x4 model = geometry.transform;
            QMatrix4x4 modelView = view * model;
            QMatrix4x4 mvp = projection * modelView;
            QMatrix3x3 normalMat = modelView.normalMatrix();

            m_componentShader.setUniformMat4("modelViewProjection", mvp);
            m_componentShader.setUniformMat4("modelView", modelView);
            m_componentShader.setUniformMat4("model", model);
            m_componentShader.setUniformMat3("normalMatrix", normalMat);
            m_componentShader.setUniformBool("selected", isSelected);
            baseMatricesActive = false;  // per-component transform now bound

            const bool spreadZ = (m_layerZSpacing != 1.0f) && layerZSpacingApplicable();
            const auto fastKeys = m_fastActive && m_shapeMode != ShapeMode::Standard
                                      ? m_fastKeys.find(id)
                                      : m_fastKeys.end();
            for (auto& layer : geometry.layers) {
                if (!standardMeshDrawn(layer)) continue;
                if (hideSubstrate && layer.name == "Substrate") continue;
                if (layer.opacity < 1.0f) continue;  // deferred to the transparent pass
                if (fastKeys != m_fastKeys.end() && fastKeys->second.count(layer.key)) {
                    continue;  // drawn by the fast view
                }
                if (spreadZ) {
                    // Spread this layer along world Y by (z_bottom * (factor-1)):
                    // gaps grow, thickness is preserved. factor==1 leaves the
                    // per-component matrices above untouched (byte-identical).
                    QMatrix4x4 lmodel = model;
                    lmodel.translate(0.0f, layer.z_bottom * (m_layerZSpacing - 1.0f), 0.0f);
                    QMatrix4x4 lmv = view * lmodel;
                    m_componentShader.setUniformMat4("modelViewProjection", projection * lmv);
                    m_componentShader.setUniformMat4("modelView", lmv);
                    m_componentShader.setUniformMat4("model", lmodel);
                    m_componentShader.setUniformMat3("normalMatrix", lmv.normalMatrix());
                }
                const QColor& lc = layer.color;
                m_componentShader.setUniformVec4("objectColor",
                    QVector4D(lc.redF(), lc.greenF(), lc.blueF(), 1.0f));
                layer.mesh.render();
            }
        } else {
            // Solid mode or Detailed without GDS: render box mesh.
            // Restore the pass-level identity matrices if a prior Detailed
            // component overwrote them, otherwise this box renders with the
            // wrong transform.
            if (!baseMatricesActive) {
                float aspect = currentAspect();
                MATRIX4X4 viewMat = m_scene.camera().viewMatrix();
                MATRIX4X4 projMat = m_scene.camera().projectionMatrix(aspect);
                QMatrix4x4 view, projection;
                for (int i = 0; i < 16; ++i) {
                    view.data()[i] = viewMat.GetEntry(i);
                    projection.data()[i] = projMat.GetEntry(i);
                }
                QMatrix4x4 model;
                model.setToIdentity();
                m_componentShader.setUniformMat4("modelViewProjection", projection * view);
                m_componentShader.setUniformMat4("modelView", view);
                m_componentShader.setUniformMat4("model", model);
                m_componentShader.setUniformMat3("normalMatrix", view.normalMatrix());
                baseMatricesActive = true;
            }
            auto meshIt = m_meshes.find(id);
            if (meshIt == m_meshes.end()) {
                if (isFlipped) glFrontFace(GL_CCW);
                continue;
            }

            QColor color = meshIt->second.color();
            m_componentShader.setUniformVec4("objectColor",
                QVector4D(color.redF(), color.greenF(), color.blueF(), 1.0f));
            m_componentShader.setUniformBool("selected", isSelected);

            meshIt->second.render();
        }

        if (isFlipped) glFrontFace(GL_CCW);
    }

    m_componentShader.release();
}

void AssemblyView::renderTransparentPass(const std::vector<QString>& ids)
{
    // Two sources of transparent geometry:
    //   (1) whole components in Transparent render mode (box meshes, fixed alpha);
    //   (2) individual layers of Detailed components whose per-layer opacity the
    //       user dropped below 1.0 (see setLayerOpacity).
    // Nothing to draw if there are no transparent components and no built layer
    // geometry that could hold a translucent layer.
    if (ids.empty() && m_layerGeometry.empty()) {
        return;
    }

    // Disable face culling so both sides of transparent geometry are visible
    glDisable(GL_CULL_FACE);

    m_componentShader.bind();
    setupShaderUniforms();

    // (1) Component-level transparent box meshes (unchanged behavior).
    for (const QString& id : ids) {
        auto meshIt = m_meshes.find(id);
        if (meshIt == m_meshes.end()) continue;

        bool isSelected = (id == m_selectedComponent);
        QColor color = meshIt->second.color();
        m_componentShader.setUniformVec4("objectColor",
            QVector4D(color.redF(), color.greenF(), color.blueF(), 0.35f));
        m_componentShader.setUniformBool("selected", isSelected);

        meshIt->second.render();
    }

    // (2) Per-layer translucent layers of Detailed components.
    renderTranslucentLayers();

    m_componentShader.release();

    // Restore face culling
    glEnable(GL_CULL_FACE);
}

void AssemblyView::renderTranslucentLayers()
{
    if (!m_assembly) return;

    // The shader is already bound, cull is disabled and depth write is OFF
    // (set by renderComponents before the transparent pass). Solid (opacity 1.0)
    // layers of these same components already drew in the opaque pass and laid
    // down depth, so they correctly occlude the translucent layers behind them.
    for (auto& [id, geometry] : m_layerGeometry) {
        if (!isComponentVisible(id)) continue;

        Component* comp = m_assembly->component(id.toStdString());
        if (!comp) continue;

        RenderMode mode = comp->render_mode();
        bool isDetailed = (mode == RenderMode::Detailed)
                       || (mode == RenderMode::DetailedNoSubstrate);
        if (!isDetailed) continue;
        bool hideSubstrate = (mode == RenderMode::DetailedNoSubstrate);

        // Bind this component's transform (same math as the opaque Detailed path).
        float aspect = currentAspect();
        MATRIX4X4 viewMat = m_scene.camera().viewMatrix();
        MATRIX4X4 projMat = m_scene.camera().projectionMatrix(aspect);
        QMatrix4x4 view, projection;
        for (int i = 0; i < 16; ++i) {
            view.data()[i] = viewMat.GetEntry(i);
            projection.data()[i] = projMat.GetEntry(i);
        }
        QMatrix4x4 model = geometry.transform;
        QMatrix4x4 modelView = view * model;

        m_componentShader.setUniformMat4("modelViewProjection", projection * modelView);
        m_componentShader.setUniformMat4("modelView", modelView);
        m_componentShader.setUniformMat4("model", model);
        m_componentShader.setUniformMat3("normalMatrix", modelView.normalMatrix());
        m_componentShader.setUniformBool("selected", id == m_selectedComponent);

        const bool spreadZ = (m_layerZSpacing != 1.0f) && layerZSpacingApplicable();
        const auto fastKeys = m_fastActive && m_shapeMode != ShapeMode::Standard
                                  ? m_fastKeys.find(id)
                                  : m_fastKeys.end();
        for (auto& layer : geometry.layers) {
            if (!standardMeshDrawn(layer)) continue;
            if (layer.opacity >= 1.0f) continue;  // solid layers stayed in the opaque pass
            if (hideSubstrate && layer.name == "Substrate") continue;
            if (fastKeys != m_fastKeys.end() && fastKeys->second.count(layer.key)) {
                continue;  // drawn by the fast view
            }
            if (spreadZ) {
                // Match the opaque path's spread so a layer keeps its place
                // whether it is solid or translucent.
                QMatrix4x4 lmodel = model;
                lmodel.translate(0.0f, layer.z_bottom * (m_layerZSpacing - 1.0f), 0.0f);
                QMatrix4x4 lmv = view * lmodel;
                m_componentShader.setUniformMat4("modelViewProjection", projection * lmv);
                m_componentShader.setUniformMat4("modelView", lmv);
                m_componentShader.setUniformMat4("model", lmodel);
                m_componentShader.setUniformMat3("normalMatrix", lmv.normalMatrix());
            }
            const QColor& lc = layer.color;
            m_componentShader.setUniformVec4("objectColor",
                QVector4D(lc.redF(), lc.greenF(), lc.blueF(), layer.opacity));
            layer.mesh.render();
        }
    }
}

void AssemblyView::renderWireframePass(const std::vector<QString>& ids)
{
    if (ids.empty()) return;

    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glLineWidth(1.5f);

    m_componentShader.bind();
    setupShaderUniforms();

    for (const QString& id : ids) {
        auto meshIt = m_meshes.find(id);
        if (meshIt == m_meshes.end()) continue;

        bool isSelected = (id == m_selectedComponent);
        QColor color = meshIt->second.color();
        m_componentShader.setUniformVec4("objectColor",
            QVector4D(color.redF(), color.greenF(), color.blueF(), 1.0f));
        m_componentShader.setUniformBool("selected", isSelected);

        meshIt->second.render();
    }

    m_componentShader.release();

    // Restore fill mode
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

VECTOR3D AssemblyView::getComponentCenter(const QString& id) const
{
    // This is used by the transparent box pass: sort the consumed box, not
    // a centre guessed from metadata whose anchor may be a GDS origin.
    const auto mesh = m_meshes.find(id);
    if (mesh != m_meshes.end()) {
        const auto bounds = mesh->second.boundingBox();
        return (bounds.mins + bounds.maxes) * 0.5f;
    }
    if (!m_assembly) return VECTOR3D(0, 0, 0);

    Component* comp = m_assembly->component(id.toStdString());
    if (!comp) return VECTOR3D(0, 0, 0);

    const auto& pos = comp->position();
    const auto& dims = comp->dimensions();

    // Centre of the component: transform (pos + dims/2) through the shared
    // chiplet->scene mapping (see CoordFrame.h / coord_frame_contract.md).
    Position3D center;
    center.x = pos.x + dims.width / 2.0;
    center.y = pos.y + dims.height / 2.0;
    center.z = pos.z + dims.thickness / 2.0;
    const ScenePosition scenePos = sceneFromChiplet(center);
    // The vertical (elevation) center must track the stack-anchored box mesh
    // (bottom-anchored die/substrate, top-anchored interposer) so the
    // transparent back-to-front painter sort uses the mesh's real center; for a
    // die this equals pos.z+thickness/2 (unchanged), for an interposer it drops
    // to attachment_surface_z-thickness/2.
    const float sceneY = MeshBuilder::anchoredOffsetY(
        *comp, sceneFromChiplet(pos).y,
        static_cast<float>(dims.thickness * kUmToMm));
    return VECTOR3D(scenePos.x, sceneY, scenePos.z);
}

void AssemblyView::sortBackToFront(std::vector<QString>& ids)
{
    if (ids.size() <= 1) return;

    VECTOR3D camPos = m_scene.camera().position();
    std::sort(ids.begin(), ids.end(), [&](const QString& a, const QString& b) {
        VECTOR3D centerA = getComponentCenter(a);
        VECTOR3D centerB = getComponentCenter(b);
        float distA = (centerA - camPos).GetSquaredLength();
        float distB = (centerB - camPos).GetSquaredLength();
        return distA > distB;  // Farthest first
    });
}

void AssemblyView::onComponentRenderModeChanged(const QString& componentId, RenderMode newMode)
{
    // The strip names the components that are not Detailed. The command has
    // already set the mode; the geometry built below is for a Detailed one,
    // which the strip does not name.
    emit notesChanged();
    if (!m_initialized || !m_assembly) return;

    if (newMode == RenderMode::Detailed || newMode == RenderMode::DetailedNoSubstrate) {
        // Build layer geometry for this component if not already present
        if (!m_layerGeometry.count(componentId)) {
            Component* comp = m_assembly->component(componentId.toStdString());
            if (comp) {
                makeCurrent();
                try {
                    const LayerPropertiesFile* lyp = nullptr;
                    const std::string& techId = comp->technology();
                    if (!techId.empty()) {
                        auto it = m_layerProps.find(techId);
                        if (it != m_layerProps.end()) {
                            lyp = &(it->second);
                        }
                    }
                    buildLayerGeometry(*comp, lyp);
                    readFastShapes();
                } catch (const std::exception& e) {
                    // Fired from a HierarchyPanel signal; keep an exception from
                    // tessellation out of the Qt event loop. Also ensures
                    // doneCurrent() runs so the context is not left current.
                    qWarning() << "Exception building layer geometry:" << e.what();
                } catch (...) {
                    qWarning() << "Unknown exception building layer geometry";
                }
                doneCurrent();
            }
        }
    }

    update();
}

void AssemblyView::setBasePlaneVisible(bool visible)
{
    m_basePlaneVisible = visible;
    update();
}

void AssemblyView::renderGrid()
{
    if (!m_basePlaneVisible || !m_componentShader.isValid() || !m_gridMesh.hasData()) {
        return;
    }

    float aspect = currentAspect();
    MATRIX4X4 viewMat = m_scene.camera().viewMatrix();
    MATRIX4X4 projMat = m_scene.camera().projectionMatrix(aspect);
    VECTOR3D lightDir = m_scene.lightDirection();

    QMatrix4x4 view, projection;
    for (int i = 0; i < 16; ++i) {
        view.data()[i] = viewMat.GetEntry(i);
        projection.data()[i] = projMat.GetEntry(i);
    }

    QMatrix4x4 model;
    model.setToIdentity();

    QMatrix4x4 mvp = projection * view * model;
    QMatrix3x3 normalMat = view.normalMatrix();

    // Disable face culling so plane is visible from both sides
    glDisable(GL_CULL_FACE);

    // Use component shader for solid plane with lighting
    m_componentShader.bind();
    m_componentShader.setUniformMat4("modelViewProjection", mvp);
    m_componentShader.setUniformMat4("modelView", view * model);
    m_componentShader.setUniformMat4("model", model);
    m_componentShader.setUniformMat3("normalMatrix", normalMat);
    m_componentShader.setUniformVec3("lightDirection",
                                     lightInView(view, QVector3D(lightDir.x, lightDir.y, lightDir.z)));
    m_componentShader.setUniformFloat("Fcoef_half", m_scene.camera().fcoef() * 0.5f);

    // Solid white/light gray color for base plane
    m_componentShader.setUniformVec4("objectColor", QVector4D(0.95f, 0.95f, 0.95f, 1.0f));
    m_componentShader.setUniformBool("selected", false);

    m_gridMesh.render();

    m_componentShader.release();

    glEnable(GL_CULL_FACE);
}

QString AssemblyView::pickComponent(int x, int y)
{
    // Ensure BVH is up to date
    ensureBVH();

    if (!m_bvh || m_meshIndexToId.empty()) {
        return QString();
    }

    // Get ray from screen coordinates
    auto ray = m_scene.screenToRay(x, y, width(), height());

    // Use BVH for O(log N) query
    // Note: With instancing, m_meshIndexToId maps to individual instances (not groups)
    // and ensureBVH() already populates boundingBoxes correctly per-instance
    int closestIdx = m_bvh->rayQueryClosest(
        ray.origin, ray.direction,
        [this, &ray](int idx) -> float {
            // Get the component ID and find its bounding box
            if (idx < 0 || idx >= static_cast<int>(m_meshIndexToId.size())) {
                return -1.0f;
            }

            // The BVH was built with per-instance bounding boxes already
            // Just do the ray intersection with the stored bounding box
            const QString& id = m_meshIndexToId[idx];

                // Non-instanced path
                auto it = m_meshes.find(id);
                if (it == m_meshes.end()) {
                    return -1.0f;
                }
                float tMin, tMax;
                if (it->second.boundingBox().rayIntersect(ray.origin, ray.direction, tMin, tMax)) {
                    return tMin > 0 ? tMin : -1.0f;
                }
                return -1.0f;
        });

    if (closestIdx >= 0 && closestIdx < static_cast<int>(m_meshIndexToId.size())) {
        return m_meshIndexToId[closestIdx];
    }

    return QString();
}

void AssemblyView::buildLayerGeometry(const Component& comp, const LayerPropertiesFile* lyp)
{
#ifdef HAVE_KLAYOUT
    const QString compId = QString::fromStdString(comp.id());
    if (comp.layout_path().empty()) {
        qDebug() << "Component" << compId << "has no layout path, using box mode";
        return;
    }
    if (!m_assembly) {
        return;
    }

    const RenderStackup& render =
        cachedRenderStackup(m_stackups, *m_assembly, comp.technology(), lyp);
    auto loaded = loadStandardComponent(comp, lyp, render, shapeFilterPercent(compId));
    if (loaded) {
        adoptStandardComponent(compId, std::move(*loaded));
    }
#else
    Q_UNUSED(comp);
    Q_UNUSED(lyp);
#endif
}

void AssemblyView::adoptStandardComponent(const QString& compId, StandardComponent&& loaded)
{
    m_polygonCache[compId] = std::move(loaded.polygons);
    m_areaStats[compId] = loaded.areaStats;
    if (!loaded.note.empty()) {
        m_componentNotes[compId] = std::move(loaded.note);
    }
    m_shapeCounts[compId] = std::move(loaded.counts);
    m_fastSources[compId] = std::move(loaded.fast);

    Component3DGeometry& geometry = loaded.geometry;
    // Upload layer meshes to GPU
    for (auto& layer : geometry.layers) {
        layer.mesh.upload();
    }

    m_layerGeometry[compId] = std::move(geometry);
    applyLayerOverrides(compId);

    qDebug() << "Built layer geometry for" << compId << ":"
             << m_layerGeometry[compId].layerCount() << "layers,"
             << m_layerGeometry[compId].totalTriangles() << "triangles";
}

void AssemblyView::setShapeFilterPercent(const QString& componentId, double percent)
{
    if (componentId.isEmpty()) return;
    percent = std::clamp(percent, 0.0, 100.0);
    double& stored = m_shapeFilterByComponent[componentId];  // inserts 0.0 default
    if (std::abs(percent - stored) < 0.01) return;
    stored = percent;

    // Debounce rapid slider drags. The pending compId is overwritten on every
    // call; if the user drags another component's slider mid-debounce, the
    // previous edit is already stored — only the rebuild for it is skipped, which
    // the next selection-driven rebuild (or selecting it again and nudging) will
    // catch. In practice the slider only edits the currently selected component.
    m_pendingFilterComponent = componentId;
    if (m_filterDebounceTimer) {
        m_filterDebounceTimer->start(150);
    }
    emit shapeFilterChanged(componentId, percent);
}

double AssemblyView::shapeFilterPercent(const QString& componentId) const
{
    auto it = m_shapeFilterByComponent.find(componentId);
    return it == m_shapeFilterByComponent.end() ? 0.0 : it->second;
}

void AssemblyView::applyShapeFilter()
{
    if (!m_initialized) return;
    if (m_pendingFilterComponent.isEmpty()) return;
    const QString compId = m_pendingFilterComponent;
    m_pendingFilterComponent.clear();

    if (!m_polygonCache.count(compId) || !m_layerGeometry.count(compId)) {
        return;
    }

    makeCurrent();
    try {
        rebuildFilteredGeometry(compId);
    } catch (const std::exception& e) {
        // Fired from the debounce QTimer slot; an escaping exception would be
        // uncaught in the Qt event loop and abort. Also ensures doneCurrent().
        qWarning() << "Exception during shape-filter rebuild:" << e.what();
    } catch (...) {
        qWarning() << "Unknown exception during shape-filter rebuild";
    }
    doneCurrent();

    updateSceneBounds();
    update();
}

void AssemblyView::rebuildFilteredGeometry(const QString& compId)
{
#ifdef HAVE_KLAYOUT
    auto cacheIt = m_polygonCache.find(compId);
    if (cacheIt == m_polygonCache.end()) return;

    auto statsIt = m_areaStats.find(compId);
    if (statsIt == m_areaStats.end()) return;

    // Filter polygons using this component's own percent
    auto polygons = cacheIt->second;
    double compFilter = shapeFilterPercent(compId);
    if (compFilter > 0.0 && statsIt->second.total_polygons > 0) {
        double threshold = ShapeFilter::thresholdFromPercentage(
            compFilter, statsIt->second);
        polygons = ShapeFilter::filter(polygons, threshold);
    }

    // Look up component for technology info
    Component* comp = m_assembly ? m_assembly->component(compId.toStdString()) : nullptr;
    if (!comp) return;

    const std::string& techId = comp->technology();

    // The cached stackup, and whether it came from a stackup file: the
    // black-box layers are added under the same rule as the first build.
    RenderStackup render;
    auto stackupIt = m_stackups.find(techId);
    if (stackupIt != m_stackups.end()) {
        render = stackupIt->second;
    }

    // Preserve transform from existing geometry
    QMatrix4x4 transform;
    auto geomIt = m_layerGeometry.find(compId);
    if (geomIt != m_layerGeometry.end()) {
        transform = geomIt->second.transform;
    }

    StandardBuild built = buildStandardGeometry(*comp, polygons,
                                                findSheet(m_layerProps, techId), render);
    Component3DGeometry& geometry = built.geometry;
    geometry.transform = transform;

    // Upload to GPU
    for (auto& layer : geometry.layers) {
        layer.mesh.upload();
    }

    m_layerGeometry[compId] = std::move(geometry);
    applyLayerOverrides(compId);
#else
    Q_UNUSED(compId);
#endif
}

void AssemblyView::updateSceneBounds()
{
    SceneBounds bounds;
    for (const auto& [compId, geom] : m_layerGeometry) {
        bounds.add(geom);
    }
    // Also include non-instanced meshes (fallback meshes for components without GDS)
    for (const auto& [id, mesh] : m_meshes) {
        bounds.add(mesh);
    }
    const AA_BOUNDING_BOX sceneBounds = bounds.box();

    qDebug() << "updateSceneBounds: mins=(" << sceneBounds.mins.x << "," << sceneBounds.mins.y << "," << sceneBounds.mins.z
             << ") maxes=(" << sceneBounds.maxes.x << "," << sceneBounds.maxes.y << "," << sceneBounds.maxes.z << ")";

    m_scene.setSceneBounds(sceneBounds);
}

void AssemblyView::updateBasePlane()
{
    const AA_BOUNDING_BOX& sceneBounds = m_scene.sceneBounds();

    // Get scene bounds extents
    float minX = sceneBounds.mins.x;
    float maxX = sceneBounds.maxes.x;
    float minZ = sceneBounds.mins.z;
    float maxZ = sceneBounds.maxes.z;

    // Calculate center of the scene (XZ plane)
    float centerX = (minX + maxX) / 2.0f;
    float centerZ = (minZ + maxZ) / 2.0f;

    // Calculate size with padding (20% extra on each side)
    float sizeX = (maxX - minX) * 1.4f;
    float sizeZ = (maxZ - minZ) * 1.4f;
    float planeSize = std::max(sizeX, sizeZ);

    // Minimum reasonable size (10mm)
    planeSize = std::max(planeSize, 10.0f);

    // Position Y: slightly below the lowest component to avoid z-fighting
    float planeY = sceneBounds.mins.y - 0.1f;

    // Recreate the grid/base plane mesh with new position and size
    m_gridMesh = MeshBuilder::buildPlaneMesh(planeSize, centerX, centerZ, planeY);

    // Upload mesh to GPU (required for rendering)
    m_gridMesh.upload();

    qDebug() << "updateBasePlane: center=" << centerX << centerZ
             << "size=" << planeSize << "y=" << planeY;
}

void AssemblyView::mousePressEvent(QMouseEvent* event)
{
    m_lastMousePos = event->pos();
    m_isDragging = true;
    m_dragButton = event->button();

    // Ctrl + Left = rubber-band zoom
    if (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ControlModifier)) {
        m_isRubberBandZoom = true;
        m_rubberBandOrigin = event->pos();
        if (!m_rubberBand) {
            m_rubberBand = new QRubberBand(QRubberBand::Rectangle, this);
        }
        m_rubberBand->setGeometry(QRect(m_rubberBandOrigin, QSize()));
        m_rubberBand->show();
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton) {
        // Check for component picking
        QString picked = pickComponent(event->pos().x(), event->pos().y());
        if (!picked.isEmpty()) {
            selectComponent(picked);
            emit componentClicked(picked);
        } else {
            selectComponent(QString());
        }
    }

    event->accept();
}

void AssemblyView::mouseReleaseEvent(QMouseEvent* event)
{
    // End rubber-band zoom
    if (m_isRubberBandZoom && m_rubberBand) {
        m_rubberBand->hide();
        m_isRubberBandZoom = false;
        QRect rect = QRect(m_rubberBandOrigin, event->pos()).normalized();
        if (rect.width() >= 5 && rect.height() >= 5) {
            zoomToRect(rect);
        }
        event->accept();
        return;
    }

    m_isDragging = false;
    m_dragButton = Qt::NoButton;
    event->accept();
}

void AssemblyView::mouseMoveEvent(QMouseEvent* event)
{
    // Handle rubber-band zoom dragging
    if (m_isRubberBandZoom && m_rubberBand) {
        m_rubberBand->setGeometry(QRect(m_rubberBandOrigin, event->pos()).normalized());
        event->accept();
        return;
    }

    if (!m_isDragging) {
        return;
    }

    QPoint delta = event->pos() - m_lastMousePos;
    m_lastMousePos = event->pos();

    if (m_dragButton == Qt::LeftButton && event->modifiers() & Qt::AltModifier) {
        // Alt + Left = Orbit
        m_scene.camera().orbit(delta.x(), delta.y());
    } else if (m_dragButton == Qt::MiddleButton ||
               (m_dragButton == Qt::LeftButton && event->modifiers() & Qt::ShiftModifier)) {
        // Middle or Shift + Left = Pan
        m_scene.camera().pan(delta.x(), delta.y());
    } else if (m_dragButton == Qt::RightButton) {
        // Right = Orbit
        m_scene.camera().orbit(delta.x(), delta.y());
    }

    emit cameraChanged();
    update();
    event->accept();
}

void AssemblyView::wheelEvent(QWheelEvent* event)
{
    float delta = event->angleDelta().y() / 120.0f;
    m_scene.camera().zoom(delta);
    emit cameraChanged();
    update();
    event->accept();
}

void AssemblyView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        QString picked = pickComponent(event->pos().x(), event->pos().y());
        if (!picked.isEmpty()) {
            emit componentDoubleClicked(picked);
        } else {
            fitToAssembly();
        }
    }

    event->accept();
}

void AssemblyView::zoomToRect(const QRect& rect)
{
    Camera& cam = m_scene.camera();

    // Scale distance by rectangle-to-viewport ratio, capped at 20x per drag
    float scaleX = static_cast<float>(rect.width()) / static_cast<float>(width());
    float scaleY = static_cast<float>(rect.height()) / static_cast<float>(height());
    float scale = std::max(scaleX, scaleY);
    scale = std::max(scale, 0.05f);

    // HiDPI: screen coords -> framebuffer coords
    float dpr = static_cast<float>(devicePixelRatioF());
    int fbW = static_cast<int>(width() * dpr);
    int fbH = static_cast<int>(height() * dpr);

    // Build view and projection matrices
    float aspect = (height() > 0) ? static_cast<float>(width()) / static_cast<float>(height()) : 1.0f;
    MATRIX4X4 view = cam.viewMatrix();
    MATRIX4X4 proj = cam.projectionMatrix(aspect);
    MATRIX4X4 invProj = proj.GetInverse();
    MATRIX4X4 invView = view.GetInverse();

    float FcoefHalf = cam.fcoef() * 0.5f;

    // Sample 9 points in the rectangle: center + 8 at 25%/75% grid.
    // Use the first valid depth found (center has priority).
    struct SamplePoint { float sx; float sy; };
    float cx = rect.center().x() + 0.5f;
    float cy = rect.center().y() + 0.5f;
    float qx = rect.width() * 0.25f;
    float qy = rect.height() * 0.25f;

    SamplePoint samples[9] = {
        {cx, cy},                               // center (highest priority)
        {cx - qx, cy - qy}, {cx, cy - qy}, {cx + qx, cy - qy},  // top row
        {cx - qx, cy},                     {cx + qx, cy},         // mid sides
        {cx - qx, cy + qy}, {cx, cy + qy}, {cx + qx, cy + qy}   // bottom row
    };

    // Read depth buffer at sample points to find actual 3D surface
    makeCurrent();

    float hitScreenX = cx;
    float hitScreenY = cy;
    float hitDepth = 1.0f;
    bool foundHit = false;

    for (int i = 0; i < 9 && !foundHit; ++i) {
        int fbX = static_cast<int>(samples[i].sx * dpr);
        int fbY = fbH - 1 - static_cast<int>(samples[i].sy * dpr);  // GL: bottom-up

        // Clamp to framebuffer bounds
        fbX = std::max(0, std::min(fbX, fbW - 1));
        fbY = std::max(0, std::min(fbY, fbH - 1));

        float depth = 1.0f;
        glReadPixels(fbX, fbY, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);

        if (depth < 1.0f - 1e-6f) {
            hitDepth = depth;
            hitScreenX = samples[i].sx;
            hitScreenY = samples[i].sy;
            foundHit = true;
        }
    }

    doneCurrent();

    if (foundHit && FcoefHalf > 1e-8f) {
        // Reverse logarithmic depth to recover eye-space Z.
        // Shader writes: gl_FragDepth = log2(1.0 + w) * Fcoef_half
        //   where w = -eyeZ (perspective projection).
        // Reverse:  w = pow(2, depth / Fcoef_half) - 1
        //           eyeZ = -w
        float w = std::pow(2.0f, hitDepth / FcoefHalf) - 1.0f;
        float eyeZ = -w;

        // Convert screen coords to NDC [-1, 1]
        float ndcX = (hitScreenX / static_cast<float>(width())) * 2.0f - 1.0f;
        float ndcY = 1.0f - (hitScreenY / static_cast<float>(height())) * 2.0f;

        // Unproject: NDC -> eye-space direction via inverse projection
        VECTOR4D clipNear(ndcX, ndcY, -1.0f, 1.0f);
        VECTOR4D eyeNear = invProj * clipNear;

        // Perspective divide to get eye-space direction
        if (std::abs(eyeNear.w) > 1e-8f) {
            eyeNear = eyeNear / eyeNear.w;
        }

        // Scale the direction to reach the correct eye-space depth
        if (std::abs(eyeNear.z) > 1e-8f) {
            float t = eyeZ / eyeNear.z;
            VECTOR4D eyePoint(eyeNear.x * t, eyeNear.y * t, eyeZ, 1.0f);

            // Transform from eye-space to world-space
            VECTOR4D worldPoint = invView * eyePoint;

            VECTOR3D newTarget(worldPoint.x, worldPoint.y, worldPoint.z);
            cam.setTarget(newTarget);
        }
    }
    // If no hit (all background), only zoom distance without moving target

    cam.setDistance(cam.distance() * scale);
    emit cameraChanged();
    update();
}

// ---------------------------------------------------------------------------
// Fast and Every shape

void AssemblyView::setShapeMode(ShapeMode mode)
{
    if (mode == m_shapeMode) {
        return;
    }
    m_shapeMode = mode;
    dropFastFrame();
    m_everyFrameSeconds = -1.0;
    readFastShapes();
    emit notesChanged();
    emit layerRowsChanged();
    update();
}

bool AssemblyView::shapesPrepared() const
{
    if (m_shapeMode == ShapeMode::Standard) {
        return false;
    }
    for (const auto& [id, source] : m_fastSources) {
        auto f = m_fast.find(id);
        if (f == m_fast.end() || f->second.fillOnly) {
            return false;
        }
    }
    return true;
}

bool AssemblyView::fillPrepared() const
{
    if (!m_assembly) return true;
    for (const auto& comp : m_assembly->components()) {
        const QString id = QString::fromStdString(comp->id());
        auto source = m_fastSources.find(id);
        if (source == m_fastSources.end() || m_fast.count(id)) continue;
        if (layersDrawn(*comp) && !fillSource(source->second).layers.empty()) {
            return false;
        }
    }
    return true;
}

void AssemblyView::setCamera(const Camera& camera)
{
    m_scene.camera() = camera;
    emit cameraChanged();
    update();
}

void AssemblyView::readFastShapes()
{
    // Standard reads fill alone, and only with fill on. Fast and Every read
    // every placed layer, and replace a fill-only read, giving back its share
    // of the budget.
    const bool fillOnly = m_shapeMode == ShapeMode::Standard;
    if (m_fastReading || !m_assembly || (fillOnly && !m_showFill)) {
        return;
    }
    if (!fillOnly) {
        bool dropped = false;
        for (auto it = m_fast.begin(); it != m_fast.end();) {
            if (!it->second.fillOnly) {
                ++it;
                continue;
            }
            for (const FastLayer& l : it->second.layers) m_fastBudget += l.tiles.records();
            m_fastKeys.erase(it->first);
            it = m_fast.erase(it);
            dropped = true;
        }
        if (dropped) {
            dropFastFrame();
            m_fastUploaded = false;
        }
    }
    // In assembly order, as studio-viewcount spends the budget.
    std::vector<FastSource> todo;
    for (const auto& comp : m_assembly->components()) {
        const QString id = QString::fromStdString(comp->id());
        auto source = m_fastSources.find(id);
        if (source == m_fastSources.end() || m_fast.count(id)) continue;
        if (fillOnly && fillSource(source->second).layers.empty()) continue;
        todo.push_back(source->second);
    }
    if (todo.empty()) {
        return;
    }
    m_fastReading = true;
    const int generation = m_fastGeneration;
    const std::size_t budget = m_fastBudget;
    // Only copies go to the worker; it touches neither the view nor the
    // assembly. Each read opens its own layout.
    m_fastWatcher->setFuture(QtConcurrent::run([todo = std::move(todo), generation, budget,
                                                fillOnly]() {
        auto read = std::make_shared<FastRead>();
        read->generation = generation;
        read->budget = budget;
        for (const FastSource& source : todo) {
            FastComponent c;
            try {
                c = fillOnly ? loadFillTiles(source, read->budget)
                             : loadFastComponent(source, read->budget);
            } catch (const std::exception& e) {
                c = FastComponent{};
                c.componentId = source.componentId;
                c.notes.push_back(fastReadFailedNote(source.componentId, e.what()));
            } catch (...) {
                c = FastComponent{};
                c.componentId = source.componentId;
                c.notes.push_back(fastReadFailedNote(source.componentId, "unknown error"));
            }
            c.fillOnly = fillOnly;
            read->components.emplace(QString::fromStdString(source.componentId), std::move(c));
        }
        return read;
    }));
}

void AssemblyView::onFastRead()
{
    m_fastReading = false;
    const std::shared_ptr<FastRead> read = m_fastWatcher->result();
    if (read && read->generation == m_fastGeneration) {
        dropFastFrame();
        for (auto& [id, c] : read->components) {
            std::set<LayerKey> keys;
            for (const FastLayer& l : c.layers) keys.insert(l.key);
            m_fastKeys[id] = std::move(keys);
            m_fast[id] = std::move(c);
        }
        m_fastBudget = read->budget;
        m_fastUploaded = false;
    }
    // A newer assembly, or a component built since, may still need reading.
    readFastShapes();
    if (shapesPrepared()) {
        emit shapesRead();
    }
    emit notesChanged();
    emit layerRowsChanged();
    update();
}

void AssemblyView::dropFastFrame()
{
    if (m_settleTimer) m_settleTimer->stop();
    m_fastActive = false;
    m_fastFrame = FastFrame{};
    m_fastChoice = TileChoice{};
    m_fastDraws.clear();
    m_tileMemory = TileChoiceMemory{};
    m_settledBuckets.clear();
    m_settledCoverage.clear();
    m_settledRows.clear();
    m_settledNotes.clear();
}

void AssemblyView::prepareFastFrame()
{
    m_fastActive = false;
    m_fastDraws.clear();
    // Standard draws a frame of fill alone, once it is read.
    const bool fillFrame = m_shapeMode == ShapeMode::Standard;
    if (!m_assembly || !m_fastGL.initialized()
        || !(fillFrame ? m_showFill && fillPrepared() : shapesPrepared())) {
        return;
    }
    const qreal dpr = devicePixelRatioF();
    const int w = m_renderWidth > 0 ? m_renderWidth : static_cast<int>(width() * dpr);
    const int h = m_renderHeight > 0 ? m_renderHeight : static_cast<int>(height() * dpr);
    if (w <= 0 || h <= 0) {
        return;
    }
    if (!m_fastUploaded) {
        std::vector<const FastLayer*> layers;
        for (const auto& [id, c] : m_fast) {
            for (const FastLayer& l : c.layers) layers.push_back(&l);
        }
        m_fastGL.upload(this, layers);
        m_fastUploaded = true;
    }

    std::vector<FastFramePart> parts;
    for (const auto& comp : m_assembly->components()) {
        const QString id = QString::fromStdString(comp->id());
        auto f = m_fast.find(id);
        auto g = m_layerGeometry.find(id);
        if (f == m_fast.end() || g == m_layerGeometry.end()) continue;
        parts.push_back({&f->second, &g->second, layersDrawn(*comp),
                         comp->render_mode() == RenderMode::DetailedNoSubstrate});
    }
    const float spread = layerZSpacingApplicable() ? m_layerZSpacing : 1.0f;
    m_fastFrame = fastFrame(
        parts,
        [this, fillFrame](const std::string& id, const FastLayer& l) {
            return (!fillFrame || l.place.fill)
                && isLayerVisible(QString::fromStdString(id), l.key.layer, l.key.datatype);
        },
        spread);
    m_fastChoice = chooseTiles(m_fastFrame.inputs, tileCamera(m_scene.camera(), w, h),
                               tileChoiceParams(m_shapeMode), m_tileMemory);

    for (std::size_t i = 0; i < m_fastFrame.refs.size(); ++i) {
        if (!m_fastFrame.inputs[i].visible) continue;
        const FastFrameLayer& ref = m_fastFrame.refs[i];
        const QString id = QString::fromStdString(ref.componentId);
        const Component* comp = m_assembly->component(ref.componentId);
        const LayerKey& key = ref.layer->key;
        FastDrawLayer d;
        d.layer = ref.layer;
        d.choiceIndex = i;
        std::copy(m_fastFrame.inputs[i].model.begin(), m_fastFrame.inputs[i].model.end(),
                  d.model.data());
        const QColor& c = ref.layer->place.color;
        d.color = QVector4D(c.redF(), c.greenF(), c.blueF(),
                            layerOpacity(id, key.layer, key.datatype));
        d.selected = id == m_selectedComponent;
        d.mirrored = comp && comp->orientation() == Orientation::FaceDown;
        d.coverRank = m_fastFrame.coverRanks[i];
        m_fastDraws.push_back(d);
    }
    m_fastActive = true;
}

void AssemblyView::drawFastLayers(bool translucent)
{
    if (!m_fastActive || m_fastDraws.empty()) {
        return;
    }
    const MATRIX4X4 viewMat = m_scene.camera().viewMatrix();
    const MATRIX4X4 projMat = m_scene.camera().projectionMatrix(currentAspect());
    FastFrameMatrices m;
    for (int i = 0; i < 16; ++i) {
        m.view.data()[i] = viewMat.GetEntry(i);
        m.projection.data()[i] = projMat.GetEntry(i);
    }
    const VECTOR3D light = m_scene.lightDirection();
    m.lightDirection = QVector3D(light.x, light.y, light.z);
    m.fcoefHalf = m_scene.camera().fcoef() * 0.5f;
    // As the standard translucent layers: both sides, no depth write (the
    // caller's).
    if (translucent) glDisable(GL_CULL_FACE);
    m_fastGL.draw(this, m_fastDraws, m_fastChoice, m, translucent);
    if (translucent) glEnable(GL_CULL_FACE);
}

void AssemblyView::settleFast()
{
    if (!m_fastActive) {
        return;
    }
    m_settledBuckets.clear();
    std::vector<std::string> rows;
    for (std::size_t i = 0; i < m_fastFrame.refs.size() && i < m_fastChoice.layers.size(); ++i) {
        const FastFrameLayer& ref = m_fastFrame.refs[i];
        const LayerBuckets& b = m_fastChoice.layers[i];
        const LayerKey& key = ref.layer->key;
        m_settledBuckets[QString::fromStdString(ref.componentId)][key] = b;
        rows.push_back(ref.componentId + " " + std::to_string(key.layer) + "/"
                       + std::to_string(key.datatype) + " "
                       + fastRowLabel(ref.layer->tiles.inGds, b) + (b.coverage ? " +" : ""));
    }
    m_settledCoverage = m_shapeMode == ShapeMode::Standard
                          ? fillCoverageNotes(m_fastFrame.refs, m_fastChoice)
                          : coverageNotes(m_fastFrame.refs, m_fastChoice);
    // Only what changed: a notes strip that resizes the view repaints it,
    // and the repaint settles again.
    if (rows != m_settledRows) {
        m_settledRows = std::move(rows);
        emit layerRowsChanged();
    }
    const QStringList n = notes();
    if (n != m_settledNotes) {
        m_settledNotes = n;
        emit notesChanged();
    }
}

} // namespace chiplet
