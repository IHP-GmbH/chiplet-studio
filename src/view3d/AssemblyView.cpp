/**
 * AssemblyView.cpp - 3D assembly view implementation
 */

#include "AssemblyView.h"
#include "MeshBuilder.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <QDebug>

namespace chiplet {

// Shader source embedded in code (loaded from resources in production)
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

void main() {
    fragNormal = normalMatrix * normal;
    fragPosition = vec3(modelView * vec4(position, 1.0));
    worldPosition = vec3(model * vec4(position, 1.0));
    gl_Position = modelViewProjection * vec4(position, 1.0);
}
)";

static const char* componentFragmentShader = R"(
#version 330 core
in vec3 fragNormal;
in vec3 fragPosition;
in vec3 worldPosition;

uniform vec4 objectColor;
uniform vec3 lightDirection;
uniform bool selected;

// Clip plane: vec4(normal.xyz, distance)
// Fragments are discarded if dot(position, normal) + distance < 0
uniform vec4 clipPlane;
uniform bool clipEnabled;

out vec4 FragColor;

void main() {
    // Clip plane test
    if (clipEnabled) {
        float dist = dot(worldPosition, clipPlane.xyz) + clipPlane.w;
        if (dist < 0.0) {
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
}
)";

static const char* gridVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 position;

uniform mat4 modelViewProjection;

out vec3 fragPosition;

void main() {
    fragPosition = position;
    gl_Position = modelViewProjection * vec4(position, 1.0);
}
)";

static const char* gridFragmentShader = R"(
#version 330 core
in vec3 fragPosition;

uniform vec4 gridColor;
uniform float fadeDistance;

out vec4 FragColor;

void main() {
    float dist = length(fragPosition.xz);
    float fade = 1.0 - smoothstep(fadeDistance * 0.5, fadeDistance, dist);
    FragColor = vec4(gridColor.rgb, gridColor.a * fade);
}
)";

AssemblyView::AssemblyView(QWidget* parent)
    : QOpenGLWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
}

AssemblyView::~AssemblyView()
{
    makeCurrent();
    m_meshes.clear();
    m_gridMesh.release();
    doneCurrent();
}

void AssemblyView::setAssembly(Assembly* assembly)
{
    m_assembly = assembly;
    m_needsRebuild = true;
    m_selectedComponent.clear();
    m_layerProps.clear();

    if (m_initialized) {
        makeCurrent();
        loadLayerProperties();
        buildMeshes();
        updateSceneBounds();
        fitToAssembly();
        doneCurrent();
    }

    update();
}

void AssemblyView::selectComponent(const QString& componentId)
{
    if (m_selectedComponent != componentId) {
        // Update mesh selection state
        if (!m_selectedComponent.isEmpty() && m_meshes.count(m_selectedComponent)) {
            m_meshes[m_selectedComponent].setSelected(false);
        }
        if (!componentId.isEmpty() && m_meshes.count(componentId)) {
            m_meshes[componentId].setSelected(true);
        }

        m_selectedComponent = componentId;
        emit selectionChanged(componentId);
        update();
    }
}

void AssemblyView::fitToAssembly()
{
    m_scene.fitToScene();
    update();
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

    if (!m_gridShader.loadFromSource(gridVertexShader, gridFragmentShader)) {
        qWarning() << "Failed to load grid shader";
    }

    // Build grid mesh
    m_gridMesh = MeshBuilder::buildGridMesh(100.0f, 10.0f);
    m_gridMesh.upload();

    m_initialized = true;

    // Build meshes if assembly was set before initialization
    if (m_assembly && m_needsRebuild) {
        loadLayerProperties();
        buildMeshes();
        updateSceneBounds();
        fitToAssembly();
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

    // Render components
    renderComponents();
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

    if (!m_assembly) {
        return;
    }

    const auto& components = m_assembly->components();
    for (const auto& comp : components) {
        if (comp) {
            // Find layer properties for component's technology
            const LayerPropertiesFile* lyp = nullptr;
            const std::string& techId = comp->technology();
            if (!techId.empty()) {
                auto it = m_layerProps.find(techId);
                if (it != m_layerProps.end()) {
                    lyp = &(it->second);
                }
            }

            ComponentMesh mesh = MeshBuilder::buildComponentMesh(*comp, lyp);
            mesh.upload();
            m_meshes.emplace(QString::fromStdString(comp->id()), std::move(mesh));
        }
    }

    m_needsRebuild = false;
}

void AssemblyView::renderComponents()
{
    if (!m_componentShader.isValid() || m_meshes.empty()) {
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

    QMatrix4x4 mvp = projection * view;
    QMatrix3x3 normalMat = view.normalMatrix();

    // Model matrix is identity (meshes are pre-transformed)
    QMatrix4x4 model;
    model.setToIdentity();

    m_componentShader.bind();
    m_componentShader.setUniformMat4("modelViewProjection", mvp);
    m_componentShader.setUniformMat4("modelView", view);
    m_componentShader.setUniformMat4("model", model);
    m_componentShader.setUniformMat3("normalMatrix", normalMat);
    m_componentShader.setUniformVec3("lightDirection", QVector3D(lightDir.x, lightDir.y, lightDir.z));

    // Clip plane uniforms
    m_componentShader.setUniformBool("clipEnabled", m_clipPlane.isEnabled());
    if (m_clipPlane.isEnabled()) {
        QVector4D plane = m_clipPlane.planeEquation();
        m_componentShader.setUniformVec4("clipPlane", plane);
    }

    for (auto& [id, mesh] : m_meshes) {
        QColor color = mesh.color();
        m_componentShader.setUniformVec4("objectColor",
            QVector4D(color.redF(), color.greenF(), color.blueF(), color.alphaF()));
        m_componentShader.setUniformBool("selected", mesh.isSelected());

        mesh.render();
    }

    m_componentShader.release();
}

void AssemblyView::renderGrid()
{
    if (!m_gridShader.isValid() || !m_gridMesh.hasData()) {
        return;
    }

    float aspect = static_cast<float>(width()) / height();
    MATRIX4X4 viewMat = m_scene.camera().viewMatrix();
    MATRIX4X4 projMat = m_scene.camera().projectionMatrix(aspect);

    QMatrix4x4 view, projection;
    for (int i = 0; i < 16; ++i) {
        view.data()[i] = viewMat.GetEntry(i);
        projection.data()[i] = projMat.GetEntry(i);
    }

    QMatrix4x4 mvp = projection * view;

    glDisable(GL_CULL_FACE);

    m_gridShader.bind();
    m_gridShader.setUniformMat4("modelViewProjection", mvp);
    m_gridShader.setUniformVec4("gridColor", QVector4D(0.4f, 0.4f, 0.4f, 0.5f));
    m_gridShader.setUniformFloat("fadeDistance", 100.0f);

    // Draw as lines
    glBindVertexArray(0);  // Unbind any existing VAO

    m_gridMesh.render();

    m_gridShader.release();

    glEnable(GL_CULL_FACE);
}

QString AssemblyView::pickComponent(int x, int y)
{
    // Simple picking using ray casting
    auto ray = m_scene.screenToRay(x, y, width(), height());

    QString closestId;
    float closestDist = std::numeric_limits<float>::max();

    for (const auto& [id, mesh] : m_meshes) {
        const AA_BOUNDING_BOX& box = mesh.boundingBox();

        // Simple ray-box intersection test
        float tmin = (box.mins.x - ray.origin.x) / ray.direction.x;
        float tmax = (box.maxes.x - ray.origin.x) / ray.direction.x;
        if (tmin > tmax) std::swap(tmin, tmax);

        float tymin = (box.mins.y - ray.origin.y) / ray.direction.y;
        float tymax = (box.maxes.y - ray.origin.y) / ray.direction.y;
        if (tymin > tymax) std::swap(tymin, tymax);

        if ((tmin > tymax) || (tymin > tmax)) continue;

        if (tymin > tmin) tmin = tymin;
        if (tymax < tmax) tmax = tymax;

        float tzmin = (box.mins.z - ray.origin.z) / ray.direction.z;
        float tzmax = (box.maxes.z - ray.origin.z) / ray.direction.z;
        if (tzmin > tzmax) std::swap(tzmin, tzmax);

        if ((tmin > tzmax) || (tzmin > tmax)) continue;

        if (tzmin > tmin) tmin = tzmin;

        if (tmin < closestDist && tmin > 0) {
            closestDist = tmin;
            closestId = id;
        }
    }

    return closestId;
}

void AssemblyView::updateSceneBounds()
{
    AA_BOUNDING_BOX sceneBounds;
    bool first = true;

    for (const auto& [id, mesh] : m_meshes) {
        if (first) {
            sceneBounds = mesh.boundingBox();
            first = false;
        } else {
            sceneBounds.AddBounds(mesh.boundingBox());
        }
    }

    // If no meshes, set default bounds
    if (first) {
        VECTOR3D mins(-50, -50, -10);
        VECTOR3D maxes(50, 50, 10);
        sceneBounds.SetFromMinsMaxes(mins, maxes);
    }

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

void AssemblyView::mousePressEvent(QMouseEvent* event)
{
    m_lastMousePos = event->pos();
    m_isDragging = true;
    m_dragButton = event->button();

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
    m_isDragging = false;
    m_dragButton = Qt::NoButton;
    event->accept();
}

void AssemblyView::mouseMoveEvent(QMouseEvent* event)
{
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

} // namespace chiplet
