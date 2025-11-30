/**
 * MainWindow.cpp - Implementation
 */

#include "MainWindow.h"
#include "HierarchyPanel.h"
#include "PropertiesPanel.h"
#include "view2d/KLayout2DView.h"
#include "view3d/AssemblyView.h"
#include "view3d/ClipPlane.h"
#include "formats/ChipletFormat.h"
#include "core/Technology.h"
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QDockWidget>
#include <QFileDialog>
#include <QMessageBox>
#include <QToolBar>
#include <QSlider>
#include <QLabel>
#include <QCheckBox>
#include <QPushButton>
#include <QButtonGroup>

namespace chiplet {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("Chiplet Studio");
    resize(1280, 800);

    setupMenus();
    setupPanels();
    setupClipToolbar();
}

MainWindow::~MainWindow() = default;

void MainWindow::setupMenus()
{
    QMenu* fileMenu = menuBar()->addMenu("&File");

    QAction* newAction = fileMenu->addAction("&New");
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &MainWindow::onFileNew);

    QAction* openAction = fileMenu->addAction("&Open...");
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::onFileOpen);

    QAction* saveAction = fileMenu->addAction("&Save");
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, &MainWindow::onFileSave);

    fileMenu->addSeparator();

    QAction* exitAction = fileMenu->addAction("E&xit");
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QMainWindow::close);
}

void MainWindow::setupPanels()
{
    // Hierarchy panel (left dock)
    QDockWidget* hierarchyDock = new QDockWidget("Hierarchy", this);
    m_hierarchyPanel = new HierarchyPanel(hierarchyDock);
    hierarchyDock->setWidget(m_hierarchyPanel);
    addDockWidget(Qt::LeftDockWidgetArea, hierarchyDock);

    // Properties panel (right dock)
    m_propertiesDock = new QDockWidget("Properties", this);
    m_propertiesPanel = new PropertiesPanel(m_propertiesDock);
    m_propertiesDock->setWidget(m_propertiesPanel);
    addDockWidget(Qt::RightDockWidgetArea, m_propertiesDock);

    // 2D Layout view (right dock, tabbed with Properties)
    m_klayout2DDock = new QDockWidget("2D Layout", this);
    m_klayout2DView = new KLayout2DView(m_klayout2DDock);
    m_klayout2DDock->setWidget(m_klayout2DView);
    addDockWidget(Qt::RightDockWidgetArea, m_klayout2DDock);

    // Tab the 2D view with Properties panel
    tabifyDockWidget(m_propertiesDock, m_klayout2DDock);
    m_propertiesDock->raise();  // Properties visible by default

    // Central 3D view widget
    m_assemblyView = new AssemblyView(this);
    setCentralWidget(m_assemblyView);

    // Connect signals

    // 3D View -> Properties panel (existing)
    connect(m_assemblyView, &AssemblyView::componentClicked,
            this, [this](const QString& componentId) {
                // Update properties panel when component is clicked
                if (m_assembly && !componentId.isEmpty()) {
                    Component* comp = m_assembly->component(componentId.toStdString());
                    m_propertiesPanel->setComponent(comp);
                } else {
                    m_propertiesPanel->setComponent(nullptr);
                }
            });

    // Hierarchy -> 3D View (bidirectional selection sync)
    connect(m_hierarchyPanel, &HierarchyPanel::componentSelected,
            m_assemblyView, &AssemblyView::selectComponent);

    // 3D View -> Hierarchy (bidirectional selection sync)
    connect(m_assemblyView, &AssemblyView::selectionChanged,
            m_hierarchyPanel, &HierarchyPanel::selectComponent);

    // Hierarchy -> Properties panel
    connect(m_hierarchyPanel, &HierarchyPanel::componentSelected,
            this, [this](const QString& componentId) {
                if (m_assembly && !componentId.isEmpty()) {
                    Component* comp = m_assembly->component(componentId.toStdString());
                    m_propertiesPanel->setComponent(comp);
                } else {
                    m_propertiesPanel->setComponent(nullptr);
                }
            });

    // Hierarchy zoom request -> 3D View
    connect(m_hierarchyPanel, &HierarchyPanel::zoomToComponentRequested,
            this, [this](const QString& componentId) {
                // Select the component and fit view
                m_assemblyView->selectComponent(componentId);
                // TODO: Add fitToComponent() method in AssemblyView
            });

    // 2D drill-down connections
    connect(m_hierarchyPanel, &HierarchyPanel::componentDoubleClicked,
            this, &MainWindow::onComponentDrillDown);

    // 3D View double-click for drill-down (if signal exists)
    connect(m_assemblyView, &AssemblyView::componentDoubleClicked,
            this, &MainWindow::onComponentDrillDown);
}

void MainWindow::onFileNew()
{
    m_assembly = std::make_unique<Assembly>();
    m_assembly->set_name("Untitled");
    m_assemblyView->setAssembly(m_assembly.get());
    m_hierarchyPanel->setAssembly(m_assembly.get());
    m_propertiesPanel->setAssembly(m_assembly.get());
    m_propertiesPanel->clearSelection();
}

void MainWindow::onFileOpen()
{
    QString path = QFileDialog::getOpenFileName(
        this,
        "Open Assembly",
        QString(),
        "Chiplet Files (*.chiplet *.yaml *.yml);;All Files (*)"
    );

    if (!path.isEmpty()) {
        try {
            ChipletFormat format;
            m_assembly = format.load(path.toStdString());
            m_assemblyView->setAssembly(m_assembly.get());
            m_hierarchyPanel->setAssembly(m_assembly.get());
            m_propertiesPanel->setAssembly(m_assembly.get());
            m_propertiesPanel->clearSelection();
            setWindowTitle(QString("Chiplet Studio - %1").arg(
                QString::fromStdString(m_assembly->name())));
        } catch (const std::exception& e) {
            QMessageBox::critical(this, "Error",
                QString("Failed to load assembly: %1").arg(e.what()));
        }
    }
}

void MainWindow::onFileSave()
{
    if (!m_assembly) {
        return;
    }

    QString path = QFileDialog::getSaveFileName(
        this,
        "Save Assembly",
        QString(),
        "Chiplet Files (*.chiplet);;All Files (*)"
    );

    if (!path.isEmpty()) {
        // TODO: Save assembly to file
    }
}

void MainWindow::setupClipToolbar()
{
    m_clipToolbar = addToolBar("Cross-section");
    m_clipToolbar->setMovable(false);

    // Enable checkbox
    m_clipEnable = new QCheckBox("Clip", this);
    m_clipEnable->setToolTip("Enable cross-section view");
    m_clipToolbar->addWidget(m_clipEnable);

    m_clipToolbar->addSeparator();

    // Axis buttons (X, Y, Z)
    m_axisGroup = new QButtonGroup(this);
    m_axisGroup->setExclusive(true);

    QPushButton* btnX = new QPushButton("X", this);
    QPushButton* btnY = new QPushButton("Y", this);
    QPushButton* btnZ = new QPushButton("Z", this);

    btnX->setCheckable(true);
    btnY->setCheckable(true);
    btnZ->setCheckable(true);
    btnZ->setChecked(true);  // Default axis

    btnX->setFixedWidth(30);
    btnY->setFixedWidth(30);
    btnZ->setFixedWidth(30);

    btnX->setToolTip("Clip along X axis (YZ plane)");
    btnY->setToolTip("Clip along Y axis (XZ plane)");
    btnZ->setToolTip("Clip along Z axis (XY plane)");

    m_axisGroup->addButton(btnX, static_cast<int>(ClipAxis::X));
    m_axisGroup->addButton(btnY, static_cast<int>(ClipAxis::Y));
    m_axisGroup->addButton(btnZ, static_cast<int>(ClipAxis::Z));

    m_clipToolbar->addWidget(btnX);
    m_clipToolbar->addWidget(btnY);
    m_clipToolbar->addWidget(btnZ);

    m_clipToolbar->addSeparator();

    // Position slider
    m_clipSlider = new QSlider(Qt::Horizontal, this);
    m_clipSlider->setRange(0, 1000);
    m_clipSlider->setValue(500);  // Middle position
    m_clipSlider->setMinimumWidth(150);
    m_clipSlider->setToolTip("Clip plane position");
    m_clipToolbar->addWidget(m_clipSlider);

    // Position label
    m_clipPosLabel = new QLabel("0.0", this);
    m_clipPosLabel->setMinimumWidth(60);
    m_clipPosLabel->setAlignment(Qt::AlignCenter);
    m_clipToolbar->addWidget(m_clipPosLabel);

    m_clipToolbar->addSeparator();

    // Flip button
    m_flipButton = new QPushButton("Flip", this);
    m_flipButton->setToolTip("Flip clip direction");
    m_flipButton->setFixedWidth(40);
    m_clipToolbar->addWidget(m_flipButton);

    // Connect signals
    connect(m_clipEnable, &QCheckBox::toggled,
            this, &MainWindow::onClipToggle);

    connect(m_axisGroup, QOverload<int>::of(&QButtonGroup::idClicked),
            this, &MainWindow::onClipAxisChanged);

    connect(m_clipSlider, &QSlider::valueChanged,
            this, &MainWindow::onClipPositionChanged);

    connect(m_flipButton, &QPushButton::clicked,
            this, &MainWindow::onClipFlip);

    // Connect to AssemblyView for position label updates
    connect(m_assemblyView, &AssemblyView::clipPlaneChanged,
            this, &MainWindow::updateClipPositionLabel);
}

void MainWindow::onClipToggle(bool enabled)
{
    if (m_assemblyView) {
        m_assemblyView->setClipEnabled(enabled);
    }
}

void MainWindow::onClipAxisChanged(int axis)
{
    if (m_assemblyView) {
        m_assemblyView->setClipAxis(static_cast<ClipAxis>(axis));
        // Reset slider to middle
        m_clipSlider->setValue(500);
    }
}

void MainWindow::onClipPositionChanged(int value)
{
    if (m_assemblyView) {
        float normalizedPos = value / 1000.0f;
        m_assemblyView->clipPlane().setNormalizedPosition(normalizedPos);
        m_assemblyView->update();
        updateClipPositionLabel();
    }
}

void MainWindow::onClipFlip()
{
    if (m_assemblyView) {
        m_assemblyView->clipPlane().flip();
        m_assemblyView->update();
    }
}

void MainWindow::updateClipPositionLabel()
{
    if (m_assemblyView) {
        // Get position in mm (scene units)
        float pos = m_assemblyView->clipPlane().position();
        m_clipPosLabel->setText(QString("%1").arg(pos, 0, 'f', 1));
    }
}

void MainWindow::onComponentDrillDown(const QString& componentId)
{
    if (!m_assembly || componentId.isEmpty()) {
        return;
    }

    Component* comp = m_assembly->component(componentId.toStdString());
    if (!comp || comp->layout_path().empty()) {
        m_klayout2DView->clearLayout();
        return;
    }

    // Get layer properties from technology
    QString lypPath;
    if (!comp->technology().empty()) {
        Technology* tech = m_assembly->technology(comp->technology());
        if (tech && !tech->layer_properties_path().empty()) {
            lypPath = QString::fromStdString(tech->layer_properties_path());
        }
    }

    // Load layout and show 2D dock
    QString layoutPath = QString::fromStdString(comp->layout_path());
    if (m_klayout2DView->loadLayout(layoutPath, lypPath)) {
        m_klayout2DDock->show();
        m_klayout2DDock->raise();
    }
}

} // namespace chiplet
