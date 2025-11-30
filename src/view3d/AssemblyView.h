/**
 * AssemblyView.h - QOpenGLWidget for 3D assembly visualization
 */

#ifndef CHIPLET_VIEW3D_ASSEMBLYVIEW_H
#define CHIPLET_VIEW3D_ASSEMBLYVIEW_H

#include <QOpenGLWidget>
#include <QOpenGLExtraFunctions>
#include <QMatrix4x4>
#include <map>

#include "Camera.h"
#include "ShaderProgram.h"
#include "ComponentMesh.h"
#include "SceneManager.h"
#include "core/Assembly.h"

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

    // Camera control
    void fitToAssembly();
    void resetCamera();

signals:
    void componentClicked(const QString& componentId);
    void componentDoubleClicked(const QString& componentId);
    void selectionChanged(const QString& componentId);

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
    void renderComponents();
    void renderGrid();
    QString pickComponent(int x, int y);
    void updateSceneBounds();

    // Data
    Assembly* m_assembly = nullptr;
    QString m_selectedComponent;

    // Scene management
    SceneManager m_scene;

    // Rendering
    ShaderProgram m_componentShader;
    ShaderProgram m_gridShader;
    std::map<QString, ComponentMesh> m_meshes;
    ComponentMesh m_gridMesh;

    // Mouse state
    QPoint m_lastMousePos;
    bool m_isDragging = false;
    Qt::MouseButton m_dragButton = Qt::NoButton;

    // State
    bool m_initialized = false;
    bool m_needsRebuild = false;
};

} // namespace chiplet

#endif // CHIPLET_VIEW3D_ASSEMBLYVIEW_H
