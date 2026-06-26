// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * AssemblyView.cpp - 3D assembly view implementation
 */

#include "AssemblyView.h"
#include "MeshBuilder.h"
#include "CoordFrame.h"
#include "GDSLayerExtractor.h"
#include "LayerMeshBuilder.h"
#include "ShapeFilter.h"
#include "core/GenericLayers.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <QTimer>
#include <QDebug>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFramebufferObjectFormat>
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

    if (!assembly) {
        if (m_initialized) {
            makeCurrent();
            m_meshes.clear();
            doneCurrent();
        }
        update();
        return;
    }

    if (m_initialized) {
        makeCurrent();
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
    }

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
}

bool AssemblyView::isComponentVisible(const QString& componentId) const
{
    auto it = m_componentVisibility.find(componentId);
    if (it != m_componentVisibility.end()) {
        return it->second;
    }
    return true;  // Default to visible
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

    if (changed) {
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

    if (changed) {
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

void AssemblyView::applyLayerOverrides(const QString& componentId)
{
    auto it = m_layerGeometry.find(componentId);
    if (it == m_layerGeometry.end()) {
        return;
    }

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
    m_scene.fitToScene();
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
        QVector3D translation = geom.transform.column(3).toVector3D();

        AA_BOUNDING_BOX compBounds;
        bool first = true;
        for (const auto& layer : geom.layers) {
            AA_BOUNDING_BOX lb = layer.mesh.boundingBox();
            AA_BOUNDING_BOX wb;
            VECTOR3D wMins(lb.mins.x + translation.x(),
                           lb.mins.y + translation.y(),
                           lb.mins.z + translation.z());
            VECTOR3D wMaxes(lb.maxes.x + translation.x(),
                            lb.maxes.y + translation.y(),
                            lb.maxes.z + translation.z());
            wb.SetFromMinsMaxes(wMins, wMaxes);
            if (first) {
                compBounds = wb;
                first = false;
            } else {
                compBounds.AddBounds(wb);
            }
        }

        if (!first) {
            m_scene.camera().fitToBox(compBounds);
            update();
            return;
        }
    }

    // Fallback: non-instanced meshes
    auto it = m_meshes.find(componentId);
    if (it != m_meshes.end()) {
        m_scene.camera().fitToBox(it->second.boundingBox());
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
            m_scene.camera().fitToBox(box);
            update();
            return;
        }
    }
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
        } catch (const std::exception& e) {
            qCritical() << "AssemblyView::initializeGL: Exception during mesh building:" << e.what();
        } catch (...) {
            qCritical() << "AssemblyView::initializeGL: Unknown exception during mesh building";
        }
    }
}

void AssemblyView::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void AssemblyView::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (!m_initialized) {
        return;
    }

    // renderGrid/renderComponents build their own matrices from the camera
    // internally.
    renderGrid();
    renderComponents();
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

    if (!m_assembly) {
        return;
    }

    // Load .lyp files for each technology
    const auto& techs = m_assembly->technologies();
    for (const auto& techPtr : techs) {
        const std::string& techId = techPtr->id();
        const std::string& lypPath = techPtr->layer_properties_path();
        if (!lypPath.empty()) {
            LayerPropertiesFile lyp;
            if (lyp.load(lypPath)) {
                size_t layerCount = lyp.layer_count();
                m_layerProps[techId] = std::move(lyp);
                qDebug() << "Loaded" << layerCount << "layers from"
                         << QString::fromStdString(lypPath);
            } else {
                qWarning() << "Failed to load layer properties:"
                           << QString::fromStdString(lypPath)
                           << "-" << QString::fromStdString(lyp.error());
            }
        }
    }
}

void AssemblyView::buildMeshes()
{
    m_meshes.clear();
    m_layerGeometry.clear();
    m_stackups.clear();

    if (!m_assembly) {
        m_needsRebuild = false;
        return;
    }

    const auto& components = m_assembly->components();
    if (components.empty()) {
        m_needsRebuild = false;
        return;
    }

    // Extract GDS bounding boxes for components with layouts.
    // This ensures box meshes match the actual GDS footprint in all
    // abstraction modes (Wireframe, Transparent, Solid).
    std::map<QString, GDSBoundingBox> gdsBounds;
    for (const auto& comp : components) {
        if (!comp || comp->layout_path().empty()) continue;
        GDSBoundingBox bbox = GDSLayerExtractor::extractBoundingBox(
            comp->layout_path(), comp->top_cell());
        if (bbox.is_valid()) {
            gdsBounds[QString::fromStdString(comp->id())] = bbox;
        }
    }

    // Build non-instanced box meshes for ALL components.
    // For components with GDS layouts, use GDS bounding box for dimensions
    // so that all abstraction levels show the correct footprint.
    for (const auto& comp : components) {
        if (!comp) continue;

        const LayerPropertiesFile* lyp = nullptr;
        const std::string& techId = comp->technology();
        if (!techId.empty()) {
            auto it = m_layerProps.find(techId);
            if (it != m_layerProps.end()) {
                lyp = &(it->second);
            }
        }

        QString compId = QString::fromStdString(comp->id());
        auto bboxIt = gdsBounds.find(compId);

        ComponentMesh mesh;
        if (bboxIt != gdsBounds.end()) {
            // GDS-derived box: match the layout footprint
            const GDSBoundingBox& bbox = bboxIt->second;
            const auto& pos = comp->position();
            auto dims = comp->dimensions();

            // Use GDS width/height, keep thickness from component
            if (dims.thickness <= 0) {
                dims.thickness = 200.0;  // Default die thickness
            }

            // Convert to mm (same as MeshBuilder)
            float w = static_cast<float>(bbox.width() / 1000.0);
            float d = static_cast<float>(dims.thickness / 1000.0);
            float h = static_cast<float>(bbox.height() / 1000.0);

            // Box is sized from the GDS bbox and positioned by .chiplet position.
            // LayerMode centers each mesh on its own GDS bbox center so that
            // .chiplet position consistently means "where the component's center
            // sits in assembly space". BoxMode follows the same convention here.
            // buildBox() centers along X and Y but treats offsetZ as the +Z face,
            // so add h/2 to land the box centered on -pos.y in 3D Z.
            const ScenePosition scenePos = sceneFromChiplet(pos);
            float offsetX = scenePos.x;
            float offsetY = scenePos.y;     // Elevation
            float offsetZ = scenePos.z + h / 2.0f;

            mesh = MeshBuilder::buildBox(w, d, h, offsetX, offsetY, offsetZ);
            mesh.setColor(MeshBuilder::colorForComponent(*comp, lyp));
        } else {
            // No GDS: use YAML dimensions (existing behavior)
            mesh = MeshBuilder::buildComponentMesh(*comp, lyp);
        }

        mesh.upload();
        m_meshes.emplace(compId, std::move(mesh));
    }

    qDebug() << "Built" << m_meshes.size() << "box meshes ("
             << gdsBounds.size() << " from GDS extents)";

    // Pre-build layer geometry for every component (this matched the former
    // default "Layer" view mode; the per-component render mode then decides
    // what is actually drawn).
    for (const auto& comp : components) {
        if (!comp) continue;

        {
            const LayerPropertiesFile* lyp = nullptr;
            const std::string& techId = comp->technology();
            if (!techId.empty()) {
                auto it = m_layerProps.find(techId);
                if (it != m_layerProps.end()) {
                    lyp = &(it->second);
                }
            }
            buildLayerGeometry(*comp, lyp);
        }
    }

    if (!m_layerGeometry.empty()) {
        qDebug() << "Built layer geometry for" << m_layerGeometry.size() << "components";
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

    // Pass 2: Transparent objects (depth write OFF, sorted back-to-front)
    glDepthMask(GL_FALSE);
    sortBackToFront(transparentIds);
    renderTransparentPass(transparentIds);

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
    m_componentShader.setUniformVec3("lightDirection", QVector3D(lightDir.x, lightDir.y, lightDir.z));
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

        // Flip-chip mirror reverses triangle winding -- fix face culling
        bool isFlipped = (comp->orientation() == Orientation::FaceDown);
        if (isFlipped) glFrontFace(GL_CW);

        // If Detailed (with or without substrate) and has layer geometry, render that
        bool isDetailedMode = (comp->render_mode() == RenderMode::Detailed)
                           || (comp->render_mode() == RenderMode::DetailedNoSubstrate);
        bool hideSubstrate  = (comp->render_mode() == RenderMode::DetailedNoSubstrate);
        if (isDetailedMode && m_layerGeometry.count(id)) {
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

            for (auto& layer : geometry.layers) {
                if (!layer.visible) continue;
                if (hideSubstrate && layer.name == "Substrate") continue;
                if (layer.opacity < 1.0f) continue;  // deferred to the transparent pass
                QColor lc = layer.color;
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

        for (auto& layer : geometry.layers) {
            if (!layer.visible) continue;
            if (layer.opacity >= 1.0f) continue;  // solid layers stayed in the opaque pass
            if (hideSubstrate && layer.name == "Substrate") continue;
            QColor lc = layer.color;
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
    return VECTOR3D(scenePos.x, scenePos.y, scenePos.z);
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
    m_componentShader.setUniformVec3("lightDirection", QVector3D(lightDir.x, lightDir.y, lightDir.z));
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

namespace {

// Black-box / no-LYP chiplet: the fallback stackup does not model the die's
// own layers, so LayerMeshBuilder (which drops layers absent from the stackup)
// would render it empty. Augment a LOCAL copy of the stackup so Detailed mode
// shows a real box instead of a flat 1 um plane:
//   - outline (206/0) -> full component thickness, "outline" role (die body)
//   - pads    (205/0) -> a thin flat plane (decal) on the active face, "pad" role
// so the body height matches Transparent mode and the pads sit flush on the
// face (flipZ moves them to the bottom for flip-chip). Unknown commercial layers
// (no canonical 205/206) become full-thickness
// slabs. The role names drive the black-box colors (blue body / yellow pads;
// configs/stackups/colors/generic/blackbox.yaml, with a hardcoded fallback in
// LayerMeshBuilder).
void augmentStackupForBlackBox(LayerStackup& stackup,
                               const std::map<LayerKey, LayerPolygons>& polygons,
                               const Component& comp)
{
    double dieT = comp.dimensions().thickness;
    if (dieT <= 0.0) {
        dieT = 200.0;  // matches the Transparent-mode default thickness (um)
    }
    const LayerKey outlineKey(GenericLayers::OUTLINE_LAYER,
                              GenericLayers::OUTLINE_DATATYPE);
    const LayerKey padKey(GenericLayers::PAD_LAYER, GenericLayers::PAD_DATATYPE);
    const bool hasOutline = polygons.find(outlineKey) != polygons.end();
    // Pads render as a thin flat plane flush on the die's active face -- a decal,
    // not a raised cap (1 um on a ~200 um die reads as a plane). flipZ moves it to
    // the bottom face for flip-chip dies.
    const double padPlaneUm = 1.0;
    double slabTop = 0.0;
    for (const auto& entry : polygons) {
        const LayerKey& key = entry.first;
        if (stackup.find(key)) {
            continue;  // already modeled (real stackup layer) -- leave untouched
        }
        if (key == outlineKey) {
            stackup.addLayer(key.layer, key.datatype, 0.0, dieT,
                             GenericLayers::OUTLINE_ROLE);
        } else if (key == padKey) {
            const double z = hasOutline ? dieT : 0.0;  // flat plane on the active face, else be the body
            const double t = hasOutline ? padPlaneUm : dieT;
            stackup.addLayer(key.layer, key.datatype, z, t, GenericLayers::PAD_ROLE);
        } else {
            stackup.addLayer(key.layer, key.datatype, slabTop, dieT, "blackbox");
            slabTop += dieT;
        }
    }
}

}  // namespace

void AssemblyView::buildLayerGeometry(const Component& comp, const LayerPropertiesFile* lyp)
{
#ifdef HAVE_KLAYOUT
    QString compId = QString::fromStdString(comp.id());

    // Get the GDS layout path
    const std::string& layoutPath = comp.layout_path();
    if (layoutPath.empty()) {
        qDebug() << "Component" << compId << "has no layout path, using box mode";
        return;
    }

    // Extract the layer geometry directly from the GDS file.
    qDebug() << "Building layer geometry for" << compId << "from" << QString::fromStdString(layoutPath);

    // Get or create stackup for technology
    const std::string& techId = comp.technology();
    LayerStackup stackup;
    LayerColorScheme colorScheme;
    bool hasColorScheme = false;

    auto stackupIt = m_stackups.find(techId);
    if (stackupIt != m_stackups.end()) {
        stackup = stackupIt->second;
    } else {
        bool usedStackup = false;

        // Priority -1: explicit stackup YAML declared on the technology
        // (technologies.<id>.stackup in the .chiplet). This lets an unsupported
        // PDK ship its own stackup; it wins over the built-in
        // BlenderGDSConfigs::stackupPath(techId) lookup. Same BlenderGDS loader,
        // so the merge/color paths below behave identically. Fail-soft: a load
        // failure falls through to the built-in lookup.
        if (!usedStackup && m_assembly && !techId.empty()) {
            Technology* tech = m_assembly->technology(techId);
            if (tech && !tech->stackup_path().empty()) {
                LayerStackup customStackup;
                if (customStackup.loadFromBlenderGDS(tech->stackup_path())) {
                    stackup = customStackup;
                    usedStackup = true;
                    qDebug() << "Using explicit stackup for" << QString::fromStdString(techId)
                             << "from" << QString::fromStdString(tech->stackup_path())
                             << "with" << stackup.layerCount() << "layers";
                } else {
                    qWarning() << "Explicit stackup failed to load for"
                               << QString::fromStdString(techId) << ":"
                               << QString::fromStdString(tech->stackup_path())
                               << "- falling back to built-in lookup";
                }
            }
        }

        // Priority 0: BlenderGDS YAML stackup (accurate physical dimensions)
        if (!usedStackup && !techId.empty()) {
            std::string bgdsPath = BlenderGDSConfigs::stackupPath(techId);
            if (!bgdsPath.empty()) {
                LayerStackup bgdsStackup;
                if (bgdsStackup.loadFromBlenderGDS(bgdsPath)) {
                    stackup = bgdsStackup;
                    usedStackup = true;
                    qDebug() << "Using BlenderGDS stackup for" << QString::fromStdString(techId)
                             << "from" << QString::fromStdString(bgdsPath)
                             << "with" << stackup.layerCount() << "layers";
                }
            }
        }

        // Priority 1: Try to get stackup from Technology's process definition (techfile)
        if (!usedStackup && m_assembly && !techId.empty()) {
            Technology* tech = m_assembly->technology(techId);
            if (tech && tech->has_process_def()) {
                stackup = tech->createStackup();
                usedStackup = true;
                qDebug() << "Using techfile stackup for" << QString::fromStdString(techId)
                         << "with" << stackup.layerCount() << "layers";
            }
        }

        // Priority 2: Detect known technology names and use predefined stackups
        if (!usedStackup && !techId.empty()) {
            if (techId.find("sg13g2") != std::string::npos ||
                techId.find("SG13G2") != std::string::npos ||
                techId.find("ihp") != std::string::npos) {
                stackup = Stackups::createSG13G2();
                usedStackup = true;
                qDebug() << "Using predefined SG13G2 stackup for" << QString::fromStdString(techId)
                         << "with" << stackup.layerCount() << "layers";
            }
            else if (techId.find("intm4tm2") != std::string::npos ||
                     techId.find("IntM4TM2") != std::string::npos) {
                stackup = Stackups::createInterposer();
                usedStackup = true;
                qDebug() << "Using predefined interposer stackup for" << QString::fromStdString(techId)
                         << "with" << stackup.layerCount() << "layers";
            }
        }

        // Priority 3: Create stackup from .lyp file if available (fallback with default thickness)
        if (!usedStackup && lyp) {
            stackup = LayerStackup::fromLayerProperties(*lyp);
            qDebug() << "Using .lyp stackup for" << QString::fromStdString(techId)
                     << "with" << stackup.layerCount() << "layers (default thickness)";
        }

        // Priority 4: Fall back to default interposer stackup
        if (!usedStackup && !lyp) {
            stackup = Stackups::createInterposer();
            qDebug() << "Using default interposer stackup for" << QString::fromStdString(techId);
        }

        // Merge the interconnect PDK's 3D bodies (CuPillar/SnAgCap, or a vendor
        // microbump) into the render stackup, mirroring
        // Assembly::calculate_component_z. The interconnect PDK -- not the
        // interposer stackup -- owns these bodies (500/501/502, vendor 510/511),
        // so the layer-render path must merge the selected method's fragment to
        // know their z/height. Additive + idempotent (addLayer overwrites by
        // key).
        //
        // ONLY for the interposer's technology: the bodies physically sit on
        // the interposer. Merging them into a die's stackup inflates
        // totalHeight(), which is the flip-chip beolTop -- every layer of a
        // FaceDown die would render shifted up by the whole body-stack height
        // (die floating above its pillars by ~44 um).
        bool interposerTech = false;
        if (m_assembly && !techId.empty()) {
            for (const auto& c : m_assembly->components()) {
                if (c->type() == ComponentType::Interposer &&
                    c->technology() == techId) {
                    interposerTech = true;
                    break;
                }
            }
        }
        if (interposerTech) {
            // Shared helpers = same fragment resolution and z-reference
            // offset as Assembly::calculate_component_z; the two cannot
            // diverge. Render merges the UNION of the fragments of all
            // methods the dies use (per-die connection ids are the keys);
            // the assembly-level adapter is the legacy fallback when no
            // method id resolves.
            const std::vector<std::string> keys =
                LayerStackup::resolveInterconnectKeys(
                    m_assembly->interconnect_method_ids(),
                    m_assembly->interconnect_adapter());
            const size_t merged = stackup.mergeInterconnectFragments(keys);
            if (merged > 0) {
                QStringList keyList;
                for (const auto& k : keys) keyList << QString::fromStdString(k);
                qDebug() << "Merged" << merged << "interconnect body layers from"
                         << keyList.join(", ")
                         << "into interposer tech" << QString::fromStdString(techId);
            }
        }

        m_stackups[techId] = stackup;
    }

    // Load BlenderGDS color scheme if available
    if (!techId.empty()) {
        std::string csPath = BlenderGDSConfigs::colorSchemePath(techId, "realistic");
        if (!csPath.empty() && colorScheme.loadFromYAML(csPath)) {
            hasColorScheme = true;
            qDebug() << "Loaded BlenderGDS color scheme" << QString::fromStdString(colorScheme.name)
                     << "for" << QString::fromStdString(techId)
                     << "with" << colorScheme.layers.size() << "layer colors";
        }
    }
    // Black-box (no .lyp): fall back to the generic pad/outline color scheme
    // (ships blue body + yellow pads; user-editable). LayerMeshBuilder also
    // carries a hardcoded blue/yellow fallback if this file is missing.
    if (!hasColorScheme && !lyp) {
        std::string gp = BlenderGDSConfigs::genericColorSchemePath();
        if (!gp.empty() && colorScheme.loadFromYAML(gp)) {
            hasColorScheme = true;
        }
    }

    // Extract polygons from GDS
    GDSLayerExtractor extractor;
    ExtractionConfig config;
    config.max_polygons_per_layer = 10000;  // Limit for performance
    config.min_polygon_area = 0.1;  // Skip tiny polygons (0.1 um^2)
    extractor.setConfig(config);

    // Pass the component's top_cell to extract from the correct cell
    auto polygons = extractor.extractFromFile(layoutPath, comp.top_cell());

    if (polygons.empty()) {
        qWarning() << "No polygons extracted from" << QString::fromStdString(layoutPath)
                   << "(top_cell:" << QString::fromStdString(comp.top_cell()) << ")";
        return;
    }

    qDebug() << "Extracted" << extractor.lastLayerCount() << "layers,"
             << extractor.lastPolygonCount() << "polygons from" << compId;

    // Cache polygon data for shape filtering (avoids re-extracting from GDS)
    m_polygonCache[compId] = polygons;
    m_areaStats[compId] = ShapeFilter::computeStatistics(polygons);

    // Apply active per-component shape filter (if the user previously set one
    // for this component, e.g. .chiplet reload preserves the slider value).
    double compFilter = shapeFilterPercent(compId);
    if (compFilter > 0.0 && m_areaStats[compId].total_polygons > 0) {
        double threshold = ShapeFilter::thresholdFromPercentage(
            compFilter, m_areaStats[compId]);
        polygons = ShapeFilter::filter(polygons, threshold);
    }

    // Black-box / no-LYP chiplet (commercial / closed PDK node): the fallback
    // stackup does not model this die's layers, so LayerMeshBuilder would skip
    // them and the component would render empty in 3D. Augment a LOCAL copy of
    // the stackup (the cached one is untouched) so the die body + pads render
    // with sensible height and role-based colors. See augmentStackupForBlackBox.
    if (!lyp) {
        augmentStackupForBlackBox(stackup, polygons, comp);
    }

    // Build 3D geometry from polygons
    // For flip-chip (FaceDown) dies, invert layer z-positions so that
    // the topmost metal (e.g. TopMetal2) sits at z=0 and lower metals
    // stack upward, matching the physical face-down orientation.
    bool flipZ = (comp.orientation() == Orientation::FaceDown);
    double beolTop = flipZ ? stackup.totalHeight() : 0.0;
    // Mesh anchor convention is now schema-driven (per
    // coord_frame_contract.md §2): each component declares
    // `anchor: gds_origin` or `anchor: bbox_center`. Legacy files
    // without the field default to BboxCenter (the parser warns).
    Anchor anchor = comp.anchor();

    LayerMeshBuilder meshBuilder;
    Component3DGeometry geometry = meshBuilder.build(
        polygons, stackup, lyp,
        hasColorScheme ? &colorScheme : nullptr,
        1.0, flipZ, beolTop, anchor);

    // Set component ID and apply transform
    geometry.componentId = compId;

    // Apply component position as transform
    // Coordinate mapping: chiplet X -> 3D X, chiplet Y -> 3D Z, chiplet Z -> 3D Y
    const auto& pos = comp.position();
    geometry.transform.setToIdentity();
    const ScenePosition scenePos = sceneFromChiplet(pos);
    geometry.transform.translate(scenePos.x, scenePos.y, scenePos.z);
    // Flip-chip: mirror X for face-down dies
    if (comp.orientation() == Orientation::FaceDown) {
        geometry.transform.scale(-1.0f, 1.0f, 1.0f);
    }

    // Upload layer meshes to GPU
    for (auto& layer : geometry.layers) {
        layer.mesh.upload();
    }

    m_layerGeometry[compId] = std::move(geometry);
    applyLayerOverrides(compId);

    qDebug() << "Built layer geometry for" << compId << ":"
             << m_layerGeometry[compId].layerCount() << "layers,"
             << m_layerGeometry[compId].totalTriangles() << "triangles";
#else
    Q_UNUSED(comp);
    Q_UNUSED(lyp);
#endif
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

    // Get cached stackup
    LayerStackup stackup;
    auto stackupIt = m_stackups.find(techId);
    if (stackupIt != m_stackups.end()) {
        stackup = stackupIt->second;
    }

    // Get layer properties
    const LayerPropertiesFile* lyp = nullptr;
    if (!techId.empty()) {
        auto it = m_layerProps.find(techId);
        if (it != m_layerProps.end()) {
            lyp = &(it->second);
        }
    }

    // Black-box (no .lyp): the cached stackup doesn't model this die's layers;
    // augment the local copy so it still renders on shape-filter rebuilds,
    // consistent with the initial buildLayerGeometry().
    if (!lyp) {
        augmentStackupForBlackBox(stackup, polygons, *comp);
    }

    // Get color scheme
    LayerColorScheme colorScheme;
    bool hasColorScheme = false;
    if (!techId.empty()) {
        std::string csPath = BlenderGDSConfigs::colorSchemePath(techId, "realistic");
        if (!csPath.empty() && colorScheme.loadFromYAML(csPath)) {
            hasColorScheme = true;
        }
    }
    if (!hasColorScheme && !lyp) {
        std::string gp = BlenderGDSConfigs::genericColorSchemePath();
        if (!gp.empty() && colorScheme.loadFromYAML(gp)) {
            hasColorScheme = true;
        }
    }

    // Preserve transform from existing geometry
    QMatrix4x4 transform;
    auto geomIt = m_layerGeometry.find(compId);
    if (geomIt != m_layerGeometry.end()) {
        transform = geomIt->second.transform;
    }

    // Rebuild mesh (preserve flip-chip z-inversion from initial build).
    // Anchor is schema-driven per coord_frame_contract.md §2.
    bool flipZ = (comp->orientation() == Orientation::FaceDown);
    double beolTop = flipZ ? stackup.totalHeight() : 0.0;
    Anchor anchor = comp->anchor();

    LayerMeshBuilder meshBuilder;
    Component3DGeometry geometry = meshBuilder.build(
        polygons, stackup, lyp,
        hasColorScheme ? &colorScheme : nullptr,
        1.0, flipZ, beolTop, anchor);

    geometry.componentId = compId;
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
    AA_BOUNDING_BOX sceneBounds;
    bool first = true;

    // Include layer geometry bounds (for Layer mode rendering)
    // This is checked FIRST because layer mode is the default and most common
    if (!m_layerGeometry.empty()) {
        for (const auto& [compId, geom] : m_layerGeometry) {
            for (const auto& layer : geom.layers) {
                AA_BOUNDING_BOX localBounds = layer.mesh.boundingBox();

                // Transform all 8 corners of the local bounding box to world space.
                // This correctly handles scale(-1,1,1) for flip-chip components,
                // where extracting only the translation column produces wrong bounds.
                QVector3D corners[8] = {
                    {localBounds.mins.x,  localBounds.mins.y,  localBounds.mins.z},
                    {localBounds.maxes.x, localBounds.mins.y,  localBounds.mins.z},
                    {localBounds.mins.x,  localBounds.maxes.y, localBounds.mins.z},
                    {localBounds.maxes.x, localBounds.maxes.y, localBounds.mins.z},
                    {localBounds.mins.x,  localBounds.mins.y,  localBounds.maxes.z},
                    {localBounds.maxes.x, localBounds.mins.y,  localBounds.maxes.z},
                    {localBounds.mins.x,  localBounds.maxes.y, localBounds.maxes.z},
                    {localBounds.maxes.x, localBounds.maxes.y, localBounds.maxes.z},
                };

                QVector3D wc = geom.transform.map(corners[0]);
                VECTOR3D wMins(wc.x(), wc.y(), wc.z());
                VECTOR3D wMaxes = wMins;

                for (int i = 1; i < 8; ++i) {
                    wc = geom.transform.map(corners[i]);
                    if (wc.x() < wMins.x) wMins.x = wc.x();
                    if (wc.y() < wMins.y) wMins.y = wc.y();
                    if (wc.z() < wMins.z) wMins.z = wc.z();
                    if (wc.x() > wMaxes.x) wMaxes.x = wc.x();
                    if (wc.y() > wMaxes.y) wMaxes.y = wc.y();
                    if (wc.z() > wMaxes.z) wMaxes.z = wc.z();
                }

                AA_BOUNDING_BOX worldBounds;
                worldBounds.SetFromMinsMaxes(wMins, wMaxes);

                if (first) {
                    sceneBounds = worldBounds;
                    first = false;
                } else {
                    sceneBounds.AddBounds(worldBounds);
                }
            }
        }
    }

    // Also include non-instanced meshes (fallback meshes for components without GDS)
    for (const auto& [id, mesh] : m_meshes) {
        if (first) {
            sceneBounds = mesh.boundingBox();
            first = false;
        } else {
            sceneBounds.AddBounds(mesh.boundingBox());
        }
    }

    // If no geometry at all, set default bounds
    if (first) {
        VECTOR3D mins(-50, -50, -10);
        VECTOR3D maxes(50, 50, 10);
        sceneBounds.SetFromMinsMaxes(mins, maxes);
    }

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

    update();
    event->accept();
}

void AssemblyView::wheelEvent(QWheelEvent* event)
{
    float delta = event->angleDelta().y() / 120.0f;
    m_scene.camera().zoom(delta);
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
    update();
}

} // namespace chiplet
