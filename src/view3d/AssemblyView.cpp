/**
 * AssemblyView.cpp - 3D assembly view implementation
 */

#include "AssemblyView.h"
#include "MeshBuilder.h"
#include "GDSLayerExtractor.h"
#include "LayerMeshBuilder.h"
#include "ShapeFilter.h"
#include "view2d/KLayoutBridge.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <QTimer>
#include <QDebug>
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
out vec3 worldPosition;
out float flogz;

void main() {
    fragNormal = normalMatrix * normal;
    fragPosition = vec3(modelView * vec4(position, 1.0));
    worldPosition = vec3(model * vec4(position, 1.0));
    gl_Position = modelViewProjection * vec4(position, 1.0);
    flogz = 1.0 + gl_Position.w;
}
)";

// Instanced vertex shader
static const char* componentVertexShaderInstanced = R"(
#version 330 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;

// Per-instance attributes
layout(location = 2) in vec4 instanceModelCol0;
layout(location = 3) in vec4 instanceModelCol1;
layout(location = 4) in vec4 instanceModelCol2;
layout(location = 5) in vec4 instanceModelCol3;
layout(location = 6) in vec4 instanceColor;
layout(location = 7) in float instanceSelected;

uniform mat4 viewProjection;
uniform mat4 view;

out vec3 fragNormal;
out vec3 fragPosition;
out vec3 worldPosition;
out vec4 vertexColor;
out float vertexSelected;
out float flogz;

void main() {
    // Reconstruct instance model matrix
    mat4 instanceModel = mat4(
        instanceModelCol0,
        instanceModelCol1,
        instanceModelCol2,
        instanceModelCol3
    );

    // Transform to world space
    vec4 worldPos = instanceModel * vec4(position, 1.0);
    worldPosition = worldPos.xyz;

    // Transform normal (assuming uniform scale)
    mat3 normalMat = mat3(instanceModel);
    fragNormal = normalMat * normal;

    // View space position
    fragPosition = vec3(view * worldPos);

    // Pass instance data to fragment shader
    vertexColor = instanceColor;
    vertexSelected = instanceSelected;

    gl_Position = viewProjection * worldPos;
    flogz = 1.0 + gl_Position.w;
}
)";

// Non-instanced fragment shader (legacy)
static const char* componentFragmentShader = R"(
#version 330 core
in vec3 fragNormal;
in vec3 fragPosition;
in vec3 worldPosition;
in float flogz;

uniform vec4 objectColor;
uniform vec3 lightDirection;
uniform bool selected;
uniform float Fcoef_half;

// Clip plane: vec4(normal.xyz, distance)
// Fragments are discarded if dot(position, normal) + distance < 0
uniform vec4 clipPlane;
uniform bool clipEnabled;

// Dither pattern support
uniform sampler2D patternTexture;
uniform bool usePattern;
uniform float patternScale;  // Typically 16.0 for 16x16 patterns

out vec4 FragColor;

void main() {
    // Clip plane test
    if (clipEnabled) {
        float dist = dot(worldPosition, clipPlane.xyz) + clipPlane.w;
        if (dist < 0.0) {
            discard;
        }
    }

    // Pattern test - discard fragments where pattern alpha is 0
    if (usePattern) {
        vec2 patternCoord = gl_FragCoord.xy / patternScale;
        float patternAlpha = texture(patternTexture, patternCoord).a;
        if (patternAlpha < 0.5) {
            discard;
        }
    }

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

// Instanced fragment shader (uses per-instance color/selected from vertex shader)
static const char* componentFragmentShaderInstanced = R"(
#version 330 core
in vec3 fragNormal;
in vec3 fragPosition;
in vec3 worldPosition;
in vec4 vertexColor;
in float vertexSelected;
in float flogz;

uniform vec3 lightDirection;
uniform float Fcoef_half;

// Clip plane: vec4(normal.xyz, distance)
uniform vec4 clipPlane;
uniform bool clipEnabled;

// Dither pattern support
uniform sampler2D patternTexture;
uniform bool usePattern;
uniform float patternScale;  // Typically 16.0 for 16x16 patterns

out vec4 FragColor;

void main() {
    // Clip plane test
    if (clipEnabled) {
        float dist = dot(worldPosition, clipPlane.xyz) + clipPlane.w;
        if (dist < 0.0) {
            discard;
        }
    }

    // Pattern test - discard fragments where pattern alpha is 0
    if (usePattern) {
        vec2 patternCoord = gl_FragCoord.xy / patternScale;
        float patternAlpha = texture(patternTexture, patternCoord).a;
        if (patternAlpha < 0.5) {
            discard;
        }
    }

    vec3 norm = normalize(fragNormal);
    float ambient = 0.3;
    float diffuse = max(dot(norm, -lightDirection), 0.0) * 0.6;
    float lighting = ambient + diffuse;

    vec3 color = vertexColor.rgb * lighting;

    if (vertexSelected > 0.5) {
        color = mix(color, vec3(1.0, 0.8, 0.0), 0.3);
    }

    FragColor = vec4(color, vertexColor.a);
    gl_FragDepth = log2(max(1e-6, flogz)) * Fcoef_half;
}
)";

static const char* gridVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 position;

uniform mat4 modelViewProjection;

out vec3 fragPosition;
out float flogz;

void main() {
    fragPosition = position;
    gl_Position = modelViewProjection * vec4(position, 1.0);
    flogz = 1.0 + gl_Position.w;
}
)";

static const char* gridFragmentShader = R"(
#version 330 core
in vec3 fragPosition;
in float flogz;

uniform vec4 gridColor;
uniform float fadeDistance;
uniform float Fcoef_half;

out vec4 FragColor;

void main() {
    float dist = length(fragPosition.xz);
    float fade = 1.0 - smoothstep(fadeDistance * 0.5, fadeDistance, dist);
    FragColor = vec4(gridColor.rgb, gridColor.a * fade);
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
    makeCurrent();
    m_meshes.clear();
    m_instanceGroups.clear();
    m_layerGeometry.clear();
    m_polygonCache.clear();
    m_areaStats.clear();
    m_gridMesh.release();
    m_ditherPatterns.cleanup();
    doneCurrent();
}

// Get default dimensions for component type (in micrometers)
static Dimensions3D getDefaultDimensionsForSignature(ComponentType type)
{
    Dimensions3D dims;
    switch (type) {
        case ComponentType::Die:
        case ComponentType::DieArray:
            dims.width = 2000.0;     // 2mm
            dims.height = 2000.0;    // 2mm
            dims.thickness = 200.0;  // 200um
            break;
        case ComponentType::Interposer:
            dims.width = 8000.0;     // 8mm
            dims.height = 8000.0;    // 8mm
            dims.thickness = 100.0;  // 100um
            break;
        case ComponentType::Substrate:
            dims.width = 10000.0;    // 10mm
            dims.height = 10000.0;   // 10mm
            dims.thickness = 500.0;  // 500um
            break;
    }
    return dims;
}

// Generate a unique signature for components with identical geometry
QString AssemblyView::getMeshSignature(const Component* comp)
{
    if (!comp) {
        return QString();
    }

    // Convert component type to string for signature
    QString typeStr;
    switch (comp->type()) {
        case ComponentType::Die:        typeStr = "Die"; break;
        case ComponentType::DieArray:   typeStr = "DieArray"; break;
        case ComponentType::Interposer: typeStr = "Interposer"; break;
        case ComponentType::Substrate:  typeStr = "Substrate"; break;
    }

    // Use actual dimensions or defaults if not specified
    auto dims = comp->dimensions();
    if (dims.width <= 0 || dims.height <= 0 || dims.thickness <= 0) {
        dims = getDefaultDimensionsForSignature(comp->type());
    }

    // Components with same type and dimensions share geometry
    return QString("%1_%2x%3x%4")
        .arg(typeStr)
        .arg(dims.width, 0, 'f', 1)
        .arg(dims.height, 0, 'f', 1)
        .arg(dims.thickness, 0, 'f', 1);
}

// Update the GPU buffer with per-instance data
void MeshInstanceGroup::updateInstanceBuffer()
{
    if (componentIds.empty()) {
        return;
    }

    std::vector<InstanceData> instances;
    instances.reserve(componentIds.size());

    for (size_t i = 0; i < componentIds.size(); ++i) {
        InstanceData data;

        // Copy transform matrix (column-major)
        const QMatrix4x4& mat = transforms[i];
        for (int j = 0; j < 16; ++j) {
            data.modelMatrix[j] = mat.constData()[j];
        }

        // Copy color
        const QColor& c = colors[i];
        data.color[0] = static_cast<float>(c.redF());
        data.color[1] = static_cast<float>(c.greenF());
        data.color[2] = static_cast<float>(c.blueF());
        data.color[3] = static_cast<float>(c.alphaF());

        // Selection state
        data.selected = selected[i] ? 1.0f : 0.0f;

        // Clear padding
        data.padding[0] = 0.0f;
        data.padding[1] = 0.0f;
        data.padding[2] = 0.0f;

        instances.push_back(data);
    }

    mesh.setInstanceData(instances);
    mesh.uploadInstanceData();
}

void AssemblyView::setAssembly(Assembly* assembly)
{
    m_assembly = assembly;
    m_needsRebuild = true;
    m_selectedComponent.clear();
    m_layerProps.clear();
    m_polygonCache.clear();
    m_areaStats.clear();
    m_shapeFilterPercent = 0.0;
    m_debugPrinted = false;

    if (!assembly) {
        if (m_initialized) {
            makeCurrent();
            m_meshes.clear();
            m_instanceGroups.clear();
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

        if (m_useInstancing && !m_instanceGroups.empty()) {
            // Update selection state in instance groups
            for (auto& [sig, group] : m_instanceGroups) {
                bool needsUpdate = false;
                for (size_t i = 0; i < group.componentIds.size(); ++i) {
                    bool wasSelected = group.selected[i];
                    bool shouldBeSelected = (group.componentIds[i] == componentId);
                    if (wasSelected != shouldBeSelected) {
                        group.selected[i] = shouldBeSelected;
                        needsUpdate = true;
                    }
                }
                if (needsUpdate) {
                    makeCurrent();
                    group.updateInstanceBuffer();
                    doneCurrent();
                }
            }
        } else {
            // Non-instanced: update mesh selection state
            if (!oldSelection.isEmpty() && m_meshes.count(oldSelection)) {
                m_meshes[oldSelection].setSelected(false);
            }
            if (!componentId.isEmpty() && m_meshes.count(componentId)) {
                m_meshes[componentId].setSelected(true);
            }
        }

        // Update gizmo position
        if (!componentId.isEmpty() && m_assembly) {
            Component* comp = m_assembly->component(componentId.toStdString());
            if (comp) {
                const auto& pos = comp->position();
                const auto& dims = comp->dimensions();
                // Position gizmo at center of component
                QVector3D gizmoPos(
                    static_cast<float>(pos.x + dims.width / 2.0),
                    static_cast<float>(pos.y + dims.height / 2.0),
                    static_cast<float>(pos.z + dims.thickness / 2.0)
                );
                m_gizmo.setPosition(gizmoPos);
                m_gizmo.setVisible(true);
            } else {
                m_gizmo.setVisible(false);
            }
        } else {
            m_gizmo.setVisible(false);
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

    // Search instanced groups (BoxMode)
    for (const auto& [sig, group] : m_instanceGroups) {
        for (size_t i = 0; i < group.componentIds.size(); ++i) {
            if (group.componentIds[i] == componentId) {
                m_scene.camera().fitToBox(group.boundingBoxes[i]);
                update();
                return;
            }
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

void AssemblyView::resetCamera()
{
    m_scene.camera().reset();
    update();
}

void AssemblyView::setClipEnabled(bool enabled)
{
    m_clipPlane.setEnabled(enabled);
    emit clipPlaneChanged();
    update();
}

void AssemblyView::setClipPosition(float position)
{
    m_clipPlane.setPosition(position);
    emit clipPlaneChanged();
    update();
}

void AssemblyView::setClipAxis(ClipAxis axis)
{
    m_clipPlane.setAxis(axis);
    emit clipPlaneChanged();
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

    if (!m_componentShaderInstanced.loadFromSource(componentVertexShaderInstanced, componentFragmentShaderInstanced)) {
        qWarning() << "Failed to load instanced component shader";
        m_useInstancing = false;  // Fall back to non-instanced rendering
    }

    if (!m_gridShader.loadFromSource(gridVertexShader, gridFragmentShader)) {
        qWarning() << "Failed to load grid shader";
    }

    // Build solid base plane mesh (instead of grid lines)
    m_gridMesh = MeshBuilder::buildPlaneMesh(100.0f);
    m_gridMesh.upload();

    // Initialize gizmo
    m_gizmo.initialize();

    // Initialize dither patterns
    m_ditherPatterns.initialize();

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
    // Start frame timing
    if (m_frameCount == 0) {
        m_frameTimer.start();
    }

    m_drawCallCount = 0;

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (!m_initialized) {
        return;
    }

    float aspect = static_cast<float>(width()) / height();

    // Get matrices from camera
    QMatrix4x4 view;
    QMatrix4x4 projection;

    // Convert our MATRIX4X4 to QMatrix4x4
    MATRIX4X4 viewMat = m_scene.camera().viewMatrix();
    MATRIX4X4 projMat = m_scene.camera().projectionMatrix(aspect);

    for (int i = 0; i < 16; ++i) {
        view.data()[i] = viewMat.GetEntry(i);
        projection.data()[i] = projMat.GetEntry(i);
    }

    // Render grid
    renderGrid();
    m_drawCallCount++;

    // Render components
    renderComponents();

    // Render gizmo (on top of selected component)
    if (m_gizmo.isVisible()) {
        QMatrix4x4 viewProjection = projection * view;
        VECTOR3D camPos = m_scene.camera().position();
        QVector3D cameraPosition(camPos.x, camPos.y, camPos.z);
        m_gizmo.render(viewProjection, cameraPosition);
        m_drawCallCount++;
    }

    // Update FPS counter
    m_frameCount++;
    if (m_frameTimer.elapsed() >= 1000) {
        m_fps = m_frameCount * 1000.0f / m_frameTimer.elapsed();
        m_frameCount = 0;
        m_frameTimer.restart();

        if (m_showDebugStats) {
            qDebug() << "FPS:" << m_fps << "Draw calls:" << m_drawCallCount;
        }
    }
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
    m_instanceGroups.clear();
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

            // Position the box to align with GDS coordinates.
            // GDS geometry is at (gds_x, gds_y) in cell space, then translated
            // by component position. Box must match.
            // Flip-chip dies get scale(-1,1,1) in LayerMode, so the GDS center X
            // must be negated to keep the box aligned with the mirrored geometry.
            bool isFlipChip = (comp->orientation() == Orientation::FaceDown);
            float gdsCenterX = static_cast<float>((bbox.x_min + bbox.x_max) / 2000.0);
            float offsetX = static_cast<float>(pos.x / 1000.0) + (isFlipChip ? -gdsCenterX : gdsCenterX);
            float offsetY = static_cast<float>(pos.z / 1000.0);     // Elevation
            float offsetZ = static_cast<float>(-pos.y / 1000.0 - bbox.y_min / 1000.0);  // Near Z face

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

    // Build layer geometry for Detailed components or when in global LayerMode
    for (const auto& comp : components) {
        if (!comp) continue;

        bool needsDetail = (comp->render_mode() == RenderMode::Detailed)
                        || (m_viewMode == ViewMode::LayerMode);

        if (needsDetail) {
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

    if (m_useInstancing && !m_instanceGroups.empty()) {
        // Instance groups: one entry per instance (not per group)
        for (const auto& [sig, group] : m_instanceGroups) {
            for (size_t i = 0; i < group.componentIds.size(); ++i) {
                m_meshIndexToId.push_back(group.componentIds[i]);
                boxes.push_back(group.boundingBoxes[i]);
                indices.push_back(idx);
                ++idx;
            }
        }
    } else {
        // Non-instanced: one entry per mesh
        for (const auto& [id, mesh] : m_meshes) {
            m_meshIndexToId.push_back(id);
            boxes.push_back(mesh.boundingBox());
            indices.push_back(idx);
            ++idx;
        }
    }

    // Build or rebuild the BVH
    if (!m_bvh) {
        m_bvh = std::make_unique<BVH>();
    }
    m_bvh->build(boxes, indices);
    m_bvhDirty = false;

    if (m_showDebugStats) {
        qDebug() << "BVH built:" << m_bvh->nodeCount() << "nodes,"
                 << m_bvh->leafCount() << "leaves, depth" << m_bvh->maxDepth();
    }
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

void AssemblyView::setupShaderUniforms()
{
    float aspect = static_cast<float>(width()) / height();
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

    m_componentShader.setUniformBool("clipEnabled", m_clipPlane.isEnabled());
    if (m_clipPlane.isEnabled()) {
        QVector4D plane = m_clipPlane.planeEquation();
        m_componentShader.setUniformVec4("clipPlane", plane);
    }

    m_componentShader.setUniformBool("usePattern", false);
    m_componentShader.setUniformFloat("patternScale", 16.0f);
}

void AssemblyView::renderOpaquePass(const std::vector<QString>& ids)
{
    if (ids.empty()) return;

    m_componentShader.bind();
    setupShaderUniforms();

    for (const QString& id : ids) {
        Component* comp = m_assembly->component(id.toStdString());
        if (!comp) continue;

        bool isSelected = (id == m_selectedComponent);

        // Flip-chip mirror reverses triangle winding -- fix face culling
        bool isFlipped = (comp->orientation() == Orientation::FaceDown);
        if (isFlipped) glFrontFace(GL_CW);

        // If Detailed and has layer geometry, render that
        if (comp->render_mode() == RenderMode::Detailed && m_layerGeometry.count(id)) {
            auto& geometry = m_layerGeometry[id];

            float aspect = static_cast<float>(width()) / height();
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

            for (auto& layer : geometry.layers) {
                if (!layer.visible) continue;
                QColor lc = layer.color;
                m_componentShader.setUniformVec4("objectColor",
                    QVector4D(lc.redF(), lc.greenF(), lc.blueF(), 1.0f));
                layer.mesh.render();
                m_drawCallCount++;
            }
        } else {
            // Solid mode or Detailed without GDS: render box mesh
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
            m_drawCallCount++;
        }

        if (isFlipped) glFrontFace(GL_CCW);
    }

    m_componentShader.release();
}

void AssemblyView::renderTransparentPass(const std::vector<QString>& ids)
{
    if (ids.empty()) return;

    // Disable face culling so both sides of transparent geometry are visible
    glDisable(GL_CULL_FACE);

    m_componentShader.bind();
    setupShaderUniforms();

    for (const QString& id : ids) {
        auto meshIt = m_meshes.find(id);
        if (meshIt == m_meshes.end()) continue;

        bool isSelected = (id == m_selectedComponent);
        QColor color = meshIt->second.color();
        m_componentShader.setUniformVec4("objectColor",
            QVector4D(color.redF(), color.greenF(), color.blueF(), 0.35f));
        m_componentShader.setUniformBool("selected", isSelected);

        meshIt->second.render();
        m_drawCallCount++;
    }

    m_componentShader.release();

    // Restore face culling
    glEnable(GL_CULL_FACE);
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
        m_drawCallCount++;
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

    // Coordinate mapping: chiplet X -> 3D X, chiplet Y -> 3D -Z, chiplet Z -> 3D Y
    // Convert from um to mm
    return VECTOR3D(
        static_cast<float>((pos.x + dims.width / 2.0) / 1000.0),
        static_cast<float>((pos.z + dims.thickness / 2.0) / 1000.0),
        static_cast<float>(-(pos.y + dims.height / 2.0) / 1000.0)
    );
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

    if (newMode == RenderMode::Detailed) {
        // Build layer geometry for this component if not already present
        if (!m_layerGeometry.count(componentId)) {
            Component* comp = m_assembly->component(componentId.toStdString());
            if (comp) {
                makeCurrent();
                const LayerPropertiesFile* lyp = nullptr;
                const std::string& techId = comp->technology();
                if (!techId.empty()) {
                    auto it = m_layerProps.find(techId);
                    if (it != m_layerProps.end()) {
                        lyp = &(it->second);
                    }
                }
                buildLayerGeometry(*comp, lyp);
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

    float aspect = static_cast<float>(width()) / height();
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

    // No clipping for base plane
    m_componentShader.setUniformBool("clipEnabled", false);
    m_componentShader.setUniformBool("usePattern", false);

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

            if (m_useInstancing && !m_instanceGroups.empty()) {
                // Find the instance's bounding box in the groups
                for (const auto& [sig, group] : m_instanceGroups) {
                    for (size_t i = 0; i < group.componentIds.size(); ++i) {
                        if (group.componentIds[i] == id) {
                            float tMin, tMax;
                            if (group.boundingBoxes[i].rayIntersect(ray.origin, ray.direction, tMin, tMax)) {
                                return tMin > 0 ? tMin : -1.0f;
                            }
                            return -1.0f;
                        }
                    }
                }
                return -1.0f;
            } else {
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
            }
        });

    if (closestIdx >= 0 && closestIdx < static_cast<int>(m_meshIndexToId.size())) {
        return m_meshIndexToId[closestIdx];
    }

    return QString();
}

void AssemblyView::setViewMode(ViewMode mode)
{
    if (m_viewMode != mode) {
        m_viewMode = mode;
        m_needsRebuild = true;
        if (m_initialized && m_assembly) {
            makeCurrent();
            buildMeshes();
            updateSceneBounds();
            doneCurrent();
        }
        emit viewModeChanged(mode);
        update();
    }
}

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

    // Check if we have a KLayoutBridge instance (for already-loaded layouts)
    // For simplicity, extract directly from file
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
            else if (techId.find("interposer") != std::string::npos ||
                     techId.find("Interposer") != std::string::npos ||
                     techId.find("rdl") != std::string::npos ||
                     techId.find("RDL") != std::string::npos) {
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

    // Apply active shape filter
    if (m_shapeFilterPercent > 0.0 && m_areaStats[compId].total_polygons > 0) {
        double threshold = ShapeFilter::thresholdFromPercentage(
            m_shapeFilterPercent, m_areaStats[compId]);
        polygons = ShapeFilter::filter(polygons, threshold);
    }

    // Build 3D geometry from polygons
    // For flip-chip (FaceDown) dies, invert layer z-positions so that
    // the topmost metal (e.g. TopMetal2) sits at z=0 and lower metals
    // stack upward, matching the physical face-down orientation.
    bool flipZ = (comp.orientation() == Orientation::FaceDown);
    double beolTop = flipZ ? stackup.totalHeight() : 0.0;

    LayerMeshBuilder meshBuilder;
    Component3DGeometry geometry = meshBuilder.build(
        polygons, stackup, lyp,
        hasColorScheme ? &colorScheme : nullptr,
        1.0, flipZ, beolTop);

    // Set component ID and apply transform
    geometry.componentId = compId;

    // Apply component position as transform
    // Coordinate mapping: chiplet X -> 3D X, chiplet Y -> 3D Z, chiplet Z -> 3D Y
    const auto& pos = comp.position();
    geometry.transform.setToIdentity();
    geometry.transform.translate(
        static_cast<float>(pos.x / 1000.0),
        static_cast<float>(pos.z / 1000.0),    // Chiplet Z -> 3D Y (vertical)
        static_cast<float>(-pos.y / 1000.0));  // Chiplet Y -> 3D -Z (negated)
    // Flip-chip: mirror X for face-down dies
    if (comp.orientation() == Orientation::FaceDown) {
        geometry.transform.scale(-1.0f, 1.0f, 1.0f);
    }

    // Upload layer meshes to GPU
    for (auto& layer : geometry.layers) {
        layer.mesh.upload();
    }

    m_layerGeometry[compId] = std::move(geometry);

    qDebug() << "Built layer geometry for" << compId << ":"
             << m_layerGeometry[compId].layerCount() << "layers,"
             << m_layerGeometry[compId].totalTriangles() << "triangles";
#else
    Q_UNUSED(comp);
    Q_UNUSED(lyp);
#endif
}

void AssemblyView::setShapeFilterPercent(double percent)
{
    percent = std::clamp(percent, 0.0, 100.0);
    if (std::abs(percent - m_shapeFilterPercent) < 0.01) return;
    m_shapeFilterPercent = percent;
    if (m_filterDebounceTimer) {
        m_filterDebounceTimer->start(150);
    }
    emit shapeFilterChanged(percent);
}

void AssemblyView::applyShapeFilter()
{
    if (!m_initialized || m_polygonCache.empty()) return;

    makeCurrent();
    for (const auto& [compId, cachedPolygons] : m_polygonCache) {
        if (m_layerGeometry.count(compId)) {
            rebuildFilteredGeometry(compId);
        }
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

    // Filter polygons
    auto polygons = cacheIt->second;
    if (m_shapeFilterPercent > 0.0 && statsIt->second.total_polygons > 0) {
        double threshold = ShapeFilter::thresholdFromPercentage(
            m_shapeFilterPercent, statsIt->second);
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

    // Get color scheme
    LayerColorScheme colorScheme;
    bool hasColorScheme = false;
    if (!techId.empty()) {
        std::string csPath = BlenderGDSConfigs::colorSchemePath(techId, "realistic");
        if (!csPath.empty() && colorScheme.loadFromYAML(csPath)) {
            hasColorScheme = true;
        }
    }

    // Preserve transform from existing geometry
    QMatrix4x4 transform;
    auto geomIt = m_layerGeometry.find(compId);
    if (geomIt != m_layerGeometry.end()) {
        transform = geomIt->second.transform;
    }

    // Rebuild mesh (preserve flip-chip z-inversion from initial build)
    bool flipZ = (comp->orientation() == Orientation::FaceDown);
    double beolTop = flipZ ? stackup.totalHeight() : 0.0;

    LayerMeshBuilder meshBuilder;
    Component3DGeometry geometry = meshBuilder.build(
        polygons, stackup, lyp,
        hasColorScheme ? &colorScheme : nullptr,
        1.0, flipZ, beolTop);

    geometry.componentId = compId;
    geometry.transform = transform;

    // Upload to GPU
    for (auto& layer : geometry.layers) {
        layer.mesh.upload();
    }

    m_layerGeometry[compId] = std::move(geometry);
#else
    Q_UNUSED(compId);
#endif
}

void AssemblyView::renderLayerGeometry()
{
    if (m_layerGeometry.empty() || !m_componentShader.isValid()) {
        return;
    }

    float aspect = static_cast<float>(width()) / height();
    MATRIX4X4 viewMat = m_scene.camera().viewMatrix();
    MATRIX4X4 projMat = m_scene.camera().projectionMatrix(aspect);
    VECTOR3D lightDir = m_scene.lightDirection();

    QMatrix4x4 view, projection;
    for (int i = 0; i < 16; ++i) {
        view.data()[i] = viewMat.GetEntry(i);
        projection.data()[i] = projMat.GetEntry(i);
    }

    m_componentShader.bind();
    m_componentShader.setUniformVec3("lightDirection", QVector3D(lightDir.x, lightDir.y, lightDir.z));
    m_componentShader.setUniformFloat("Fcoef_half", m_scene.camera().fcoef() * 0.5f);

    // Clip plane uniforms
    m_componentShader.setUniformBool("clipEnabled", m_clipPlane.isEnabled());
    if (m_clipPlane.isEnabled()) {
        QVector4D plane = m_clipPlane.planeEquation();
        m_componentShader.setUniformVec4("clipPlane", plane);
    }

    // Pattern uniforms (disabled for layer rendering)
    m_componentShader.setUniformBool("usePattern", false);
    m_componentShader.setUniformFloat("patternScale", 16.0f);

    // Render each component's layer geometry
    for (auto& [compId, geometry] : m_layerGeometry) {
        // Skip invisible components
        if (!isComponentVisible(compId)) {
            continue;
        }

        bool isSelected = (compId == m_selectedComponent);

        // Apply component transform
        QMatrix4x4 model = geometry.transform;
        QMatrix4x4 modelView = view * model;
        QMatrix4x4 mvp = projection * modelView;
        QMatrix3x3 normalMat = modelView.normalMatrix();

        m_componentShader.setUniformMat4("modelViewProjection", mvp);
        m_componentShader.setUniformMat4("modelView", modelView);
        m_componentShader.setUniformMat4("model", model);
        m_componentShader.setUniformMat3("normalMatrix", normalMat);
        m_componentShader.setUniformBool("selected", isSelected);

        // Render each visible layer
        for (auto& layer : geometry.layers) {
            if (!layer.visible) continue;
            if (!layer.mesh.hasData()) continue;

            QColor color = layer.color;
            m_componentShader.setUniformVec4("objectColor",
                QVector4D(color.redF(), color.greenF(), color.blueF(), color.alphaF()));

            layer.mesh.render();
            m_drawCallCount++;
        }
    }

    m_componentShader.release();
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

    // Also include instance groups (for Box mode with instancing)
    if (m_useInstancing && !m_instanceGroups.empty()) {
        for (const auto& [sig, group] : m_instanceGroups) {
            for (const auto& bb : group.boundingBoxes) {
                if (first) {
                    sceneBounds = bb;
                    first = false;
                } else {
                    sceneBounds.AddBounds(bb);
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

    // Update clip plane range based on current axis
    float minPos, maxPos;
    switch (m_clipPlane.axis()) {
    case ClipAxis::X:
        minPos = sceneBounds.mins.x;
        maxPos = sceneBounds.maxes.x;
        break;
    case ClipAxis::Y:
        minPos = sceneBounds.mins.y;
        maxPos = sceneBounds.maxes.y;
        break;
    case ClipAxis::Z:
    default:
        minPos = sceneBounds.mins.z;
        maxPos = sceneBounds.maxes.z;
        break;
    }
    m_clipPlane.setRange(minPos, maxPos);
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
        // First, check for gizmo axis picking (if a component is selected)
        if (m_gizmo.isVisible() && !m_selectedComponent.isEmpty()) {
            auto ray = m_scene.screenToRay(event->pos().x(), event->pos().y(), width(), height());
            GizmoAxis pickedAxis = m_gizmo.pickAxis(ray.origin, ray.direction);

            if (pickedAxis != GizmoAxis::None) {
                // Start gizmo drag
                m_isDraggingGizmo = true;
                m_activeGizmoAxis = pickedAxis;
                m_gizmo.setHighlightedAxis(pickedAxis);
                m_gizmoDragMouseStart = event->pos();
                m_gizmoDragStart = m_gizmo.position();
                event->accept();
                update();
                return;
            }
        }

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

    // End gizmo drag and emit move request
    if (m_isDraggingGizmo && m_activeGizmoAxis != GizmoAxis::None) {
        QVector3D currentPos = m_gizmo.position();
        QVector3D delta = currentPos - m_gizmoDragStart;

        // Only emit if there was actual movement
        if (delta.length() > 0.001f) {
            // Constrain to active axis
            double dx = 0, dy = 0, dz = 0;
            switch (m_activeGizmoAxis) {
                case GizmoAxis::X: dx = delta.x(); break;
                case GizmoAxis::Y: dy = delta.y(); break;
                case GizmoAxis::Z: dz = delta.z(); break;
                default: break;
            }
            emit moveComponentRequested(m_selectedComponent, dx, dy, dz);
        }

        m_isDraggingGizmo = false;
        m_activeGizmoAxis = GizmoAxis::None;
        m_gizmo.setHighlightedAxis(GizmoAxis::None);
        update();
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

    // Handle gizmo dragging
    if (m_isDraggingGizmo && m_activeGizmoAxis != GizmoAxis::None) {
        // Calculate drag delta in screen space
        QPoint mouseDelta = event->pos() - m_gizmoDragMouseStart;

        // Get camera matrices for unprojection
        float aspect = static_cast<float>(width()) / height();
        MATRIX4X4 viewMat = m_scene.camera().viewMatrix();
        MATRIX4X4 projMat = m_scene.camera().projectionMatrix(aspect);

        // Project the gizmo position to screen space
        QMatrix4x4 view, projection;
        for (int i = 0; i < 16; ++i) {
            view.data()[i] = viewMat.GetEntry(i);
            projection.data()[i] = projMat.GetEntry(i);
        }
        QMatrix4x4 mvp = projection * view;

        // Get axis direction in world space
        QVector3D axisDir = TransformGizmo::axisDirection(m_activeGizmoAxis);

        // Project axis direction to screen space to determine drag sensitivity
        QVector3D worldPos = m_gizmoDragStart;
        QVector3D worldPosOffset = worldPos + axisDir;

        QVector4D screen1 = mvp * QVector4D(worldPos, 1.0f);
        QVector4D screen2 = mvp * QVector4D(worldPosOffset, 1.0f);

        if (std::abs(screen1.w()) > 0.001f && std::abs(screen2.w()) > 0.001f) {
            screen1 /= screen1.w();
            screen2 /= screen2.w();

            // Convert to pixel coordinates
            float sx1 = (screen1.x() * 0.5f + 0.5f) * width();
            float sy1 = (1.0f - (screen1.y() * 0.5f + 0.5f)) * height();
            float sx2 = (screen2.x() * 0.5f + 0.5f) * width();
            float sy2 = (1.0f - (screen2.y() * 0.5f + 0.5f)) * height();

            // Screen-space axis direction
            QVector2D screenAxisDir(sx2 - sx1, sy2 - sy1);
            float screenAxisLen = screenAxisDir.length();

            if (screenAxisLen > 0.001f) {
                screenAxisDir.normalize();

                // Project mouse delta onto screen axis direction
                QVector2D mouseVec(mouseDelta.x(), mouseDelta.y());
                float projection = QVector2D::dotProduct(mouseVec, screenAxisDir);

                // Convert back to world units
                float worldDelta = projection / screenAxisLen;

                // Calculate new position
                QVector3D newPos = m_gizmoDragStart + axisDir * worldDelta;
                m_gizmo.setPosition(newPos);
            }
        }

        update();
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

void AssemblyView::setGlobalZOffset(double offset_um)
{
    if (m_globalZOffset == offset_um) return;
    m_globalZOffset = offset_um;
    updateTransforms();
}

void AssemblyView::updateTransforms()
{
    if (!m_assembly || !m_initialized) return;

    const auto& components = m_assembly->components();

    // Update layer geometry transforms (LayerMode)
    for (const auto& comp : components) {
        if (!comp) continue;
        QString compId = QString::fromStdString(comp->id());
        auto it = m_layerGeometry.find(compId);
        if (it != m_layerGeometry.end()) {
            const auto& pos = comp->position();
            // Z offset only applies to dies, not interposers/substrates
            double zOff = (comp->type() == ComponentType::Interposer ||
                           comp->type() == ComponentType::Substrate) ? 0.0 : m_globalZOffset;
            it->second.transform.setToIdentity();
            it->second.transform.translate(
                static_cast<float>(pos.x / 1000.0),
                static_cast<float>((pos.z + zOff) / 1000.0),
                static_cast<float>(-pos.y / 1000.0));
            if (comp->orientation() == Orientation::FaceDown) {
                it->second.transform.scale(-1.0f, 1.0f, 1.0f);
            }
        }
    }

    // Update instanced transforms (BoxMode)
    for (auto& [sig, group] : m_instanceGroups) {
        for (size_t i = 0; i < group.componentIds.size(); ++i) {
            std::string id = group.componentIds[i].toStdString();
            const Component* comp = m_assembly->component(id);
            if (!comp) continue;

            const auto& pos = comp->position();
            double zOff = (comp->type() == ComponentType::Interposer ||
                           comp->type() == ComponentType::Substrate) ? 0.0 : m_globalZOffset;
            QMatrix4x4 transform;
            transform.setToIdentity();
            transform.translate(
                static_cast<float>(pos.x / 1000.0),
                static_cast<float>((pos.z + zOff) / 1000.0),
                static_cast<float>(-pos.y / 1000.0));
            // No mirror in BoxMode -- boxes are symmetric, mirror only
            // affects Detailed/LayerMode where internal geometry matters
            group.transforms[i] = transform;

            // Update bounding box
            AA_BOUNDING_BOX localBB = group.mesh.boundingBox();
            VECTOR3D offset(static_cast<float>(pos.x / 1000.0),
                           static_cast<float>((pos.z + zOff) / 1000.0),
                           static_cast<float>(-pos.y / 1000.0));
            group.boundingBoxes[i].mins = localBB.mins + offset;
            group.boundingBoxes[i].maxes = localBB.maxes + offset;
        }

        makeCurrent();
        group.updateInstanceBuffer();
        doneCurrent();
    }

    m_bvhDirty = true;
    updateSceneBounds();
    update();
}

} // namespace chiplet
