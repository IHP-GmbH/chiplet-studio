// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * MainWindow.cpp - Implementation
 */

#include "MainWindow.h"
#include "HierarchyPanel.h"
#include "PropertiesPanel.h"
#include "DrillDownPanel.h"
#include "ScriptConsole.h"
#include "FlowPanel.h"
#include "NetGraphPanel.h"
#include "CellSelectionDialog.h"
#include "view2d/KLayout2DView.h"
#include "view2d/CellComponentMapper.h"
#include "view3d/AssemblyView.h"
#include "view3d/ClipPlane.h"
#include "view3d/GDSAnalyzer.h"
#include "formats/ChipletFormat.h"
#include "core/Technology.h"
#include "core/LayerStackup.h"
#include "core/commands/CmdMoveComponent.h"
#include "core/commands/CmdSetRenderMode.h"
#include "core/Snapper.h"
#include "scripting/ScriptEngine.h"
#include "core/flow/FlowEngine.h"
#include "core/flow/FlowConfig.h"
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
#include <QDoubleSpinBox>
#include <QProgressDialog>
#include <QTimer>
#include <QStatusBar>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QtConcurrent/QtConcurrent>
#include <QApplication>

namespace chiplet {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("Chiplet Studio");
    resize(1280, 800);

    setupMenus();
    setupPanels();
    setupClipToolbar();
    setupSnapToolbar();
    setupViewModeToolbar();
    setupScriptConsole();
    setupFlowPanel();
    setupNetGraphPanel();
    setupAutoSave();

    // Initialize async load watcher
    m_loadWatcher = new QFutureWatcher<LoadResult>(this);
    connect(m_loadWatcher, &QFutureWatcher<LoadResult>::finished,
            this, &MainWindow::onAssemblyLoadFinished);
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

    // Edit menu
    QMenu* editMenu = menuBar()->addMenu("&Edit");

    m_undoAction = editMenu->addAction("&Undo");
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_undoAction->setEnabled(false);
    connect(m_undoAction, &QAction::triggered, this, [this]() {
        if (m_commandProcessor) {
            m_commandProcessor->undo();
        }
    });

    m_redoAction = editMenu->addAction("&Redo");
    m_redoAction->setShortcut(QKeySequence::Redo);
    m_redoAction->setEnabled(false);
    connect(m_redoAction, &QAction::triggered, this, [this]() {
        if (m_commandProcessor) {
            m_commandProcessor->redo();
        }
    });

    // Window icon
    QPixmap logo(":/images/resources/ihp_logo.png");
    if (!logo.isNull()) {
        setWindowIcon(QIcon(logo));
    }
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
    m_klayout2DDock->setFeatures(
        QDockWidget::DockWidgetMovable |
        QDockWidget::DockWidgetFloatable |
        QDockWidget::DockWidgetClosable);
    m_drillDownPanel = new DrillDownPanel(m_klayout2DDock);
    m_klayout2DView = m_drillDownPanel->view2d();
    m_klayout2DDock->setWidget(m_drillDownPanel);
    addDockWidget(Qt::RightDockWidgetArea, m_klayout2DDock);

    // Add maximize/minimize buttons when dock is floating (detached)
    connect(m_klayout2DDock, &QDockWidget::topLevelChanged,
            this, [this](bool floating) {
        if (floating) {
            QTimer::singleShot(0, this, [this]() {
                if (!m_klayout2DDock->isFloating()) return;
                QRect geo = m_klayout2DDock->geometry();
                m_klayout2DDock->hide();
                m_klayout2DDock->setWindowFlags(
                    Qt::Window |
                    Qt::WindowTitleHint |
                    Qt::WindowCloseButtonHint |
                    Qt::WindowMaximizeButtonHint |
                    Qt::WindowMinimizeButtonHint);
                m_klayout2DDock->setGeometry(geo);
                m_klayout2DDock->show();
                m_klayout2DDock->raise();
                m_klayout2DDock->activateWindow();
            });
        }
    });

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
                m_propertiesPanel->setComponent(componentId.toStdString(), m_assembly.get());
            });

    // Hierarchy -> 3D View (bidirectional selection sync)
    connect(m_hierarchyPanel, &HierarchyPanel::componentSelected,
            m_assemblyView, &AssemblyView::selectComponent);

    // 3D View -> Hierarchy (bidirectional selection sync)
    connect(m_assemblyView, &AssemblyView::selectionChanged,
            m_hierarchyPanel, &HierarchyPanel::selectComponent);

    // Toolbar shape-filter slider tracks the selected component. blockSignals
    // around setValue prevents the resync from being mistaken for a user edit
    // (which would write 0 into the new component's stored percent).
    connect(m_assemblyView, &AssemblyView::selectionChanged,
            this, [this](const QString& componentId) {
                if (!m_shapeFilterSlider || !m_shapeFilterLabel) return;
                if (componentId.isEmpty()) {
                    m_shapeFilterSlider->blockSignals(true);
                    m_shapeFilterSlider->setValue(0);
                    m_shapeFilterSlider->blockSignals(false);
                    m_shapeFilterSlider->setEnabled(false);
                    m_shapeFilterLabel->setText("--");
                    return;
                }
                double percent = m_assemblyView->shapeFilterPercent(componentId);
                m_shapeFilterSlider->blockSignals(true);
                m_shapeFilterSlider->setValue(static_cast<int>(percent * 10.0 + 0.5));
                m_shapeFilterSlider->blockSignals(false);
                m_shapeFilterSlider->setEnabled(true);
                m_shapeFilterLabel->setText(QString("%1%").arg(percent, 0, 'f', 1));
            });

    // Hierarchy -> Properties panel
    connect(m_hierarchyPanel, &HierarchyPanel::componentSelected,
            this, [this](const QString& componentId) {
                m_propertiesPanel->setComponent(componentId.toStdString(), m_assembly.get());
            });

    // Hierarchy interconnect row -> Properties panel (assembly-level bumping
    // method; clears the 3D component highlight, nothing to highlight for it)
    connect(m_hierarchyPanel, &HierarchyPanel::interconnectSelected,
            this, [this]() {
                m_propertiesPanel->setAssembly(m_assembly.get());
                m_propertiesPanel->showInterconnect();
                m_assemblyView->selectComponent(QString());
            });

    // Hierarchy interconnect checkbox -> show/hide the method's 3D body
    // layers (they render merged into the interposer component's stackup)
    connect(m_hierarchyPanel, &HierarchyPanel::interconnectVisibilityChanged,
            this, [this](bool visible) {
                if (!m_assembly || m_assembly->interconnect_adapter().empty()) {
                    return;
                }
                const std::string frag =
                    BlenderGDSConfigs::interconnectStackupFragmentPath(
                        m_assembly->interconnect_adapter());
                LayerStackup ic;
                if (frag.empty() || !ic.loadFromBlenderGDS(frag)) {
                    return;
                }
                for (const auto& comp : m_assembly->components()) {
                    if (comp->type() != ComponentType::Interposer) {
                        continue;
                    }
                    QString compId = QString::fromStdString(comp->id());
                    for (const auto& l : ic.sortedLayers()) {
                        m_assemblyView->setLayerVisible(
                            compId, l.layer, l.datatype, visible);
                    }
                }
            });

    // Hierarchy zoom request -> 3D View
    connect(m_hierarchyPanel, &HierarchyPanel::zoomToComponentRequested,
            this, [this](const QString& componentId) {
                m_assemblyView->selectComponent(componentId);
                m_assemblyView->fitToComponent(componentId);
            });

    // 2D drill-down connections
    connect(m_hierarchyPanel, &HierarchyPanel::componentDoubleClicked,
            this, &MainWindow::onComponentDrillDown);

    // DrillDownPanel back button -> context-dependent behavior
    connect(m_drillDownPanel, &DrillDownPanel::backRequested,
            this, [this]() {
                switch (m_drillDownPanel->panelMode()) {
                case DrillDownPanel::PanelMode::DrillDown:
                    // From an individual chiplet GDS, return to the assembly view
                    // (DrillDownPanel reloads the full assembly GDS).
                    m_drillDownPanel->clearContext();
                    break;
                case DrillDownPanel::PanelMode::Assembly:
                    // Already in the assembly GDS: reset to the full top-level
                    // layout, undoing any in-place hierarchy navigation. The 2D
                    // dock's own close button handles going back to the 3D view.
                    m_drillDownPanel->showFullAssembly();
                    break;
                default:
                    // Empty mode: nothing loaded, just hide the dock.
                    m_klayout2DDock->hide();
                    m_propertiesDock->show();
                    m_propertiesDock->raise();
                    break;
                }
            });

    // 2D -> 3D: cell navigation in assembly view highlights component in 3D
    connect(m_drillDownPanel, &DrillDownPanel::componentNavigated,
            this, [this](const QString& componentId) {
                m_assemblyView->selectComponent(componentId);
                m_hierarchyPanel->selectComponent(componentId);
            });

    // 3D -> 2D: component click navigates to wrapper cell (only in assembly mode)
    connect(m_assemblyView, &AssemblyView::componentClicked,
            this, [this](const QString& componentId) {
                if (m_drillDownPanel->panelMode() == DrillDownPanel::PanelMode::Assembly) {
                    QString cell = m_drillDownPanel->cellMapper().primaryCellForComponent(componentId);
                    if (!cell.isEmpty()) {
                        m_klayout2DView->setCurrentCell(cell);
                    }
                }
            });

    // Hierarchy visibility toggle -> 3D View
    connect(m_hierarchyPanel, &HierarchyPanel::componentVisibilityChanged,
            m_assemblyView, &AssemblyView::setComponentVisibility);

    // Properties panel per-layer show/hide -> 3D View
    connect(m_propertiesPanel, &PropertiesPanel::layerVisibilityChanged,
            m_assemblyView, &AssemblyView::setLayerVisible);

    // Seed the Properties panel layer checkboxes from the 3D view's current
    // visibility, so revisiting a component with hidden layers shows them unchecked.
    m_propertiesPanel->setLayerVisibilityResolver(
        [this](const QString& componentId, int layer, int datatype) {
            return m_assemblyView ? m_assemblyView->isLayerVisible(componentId, layer, datatype)
                                  : true;
        });

    // 3D View double-click for drill-down (if signal exists)
    connect(m_assemblyView, &AssemblyView::componentDoubleClicked,
            this, &MainWindow::onComponentDrillDown);

    // 3D View move request -> CommandProcessor
    connect(m_assemblyView, &AssemblyView::moveComponentRequested,
            this, [this](const QString& componentId, double dx, double dy, double dz) {
                if (!m_commandProcessor || !m_assembly) {
                    return;
                }
                Component* comp = m_assembly->component(componentId.toStdString());
                if (!comp) {
                    return;
                }
                Position3D oldPos = comp->position();
                Position3D newPos = {oldPos.x + dx, oldPos.y + dy, oldPos.z + dz};

                // Apply snapping if enabled
                if (m_snapEnabled && m_gridSize > 0.0) {
                    newPos = Snapper::snap(newPos, m_gridSize);
                }

                auto cmd = std::make_unique<CmdMoveComponent>(
                    componentId.toStdString(), oldPos, newPos);
                m_commandProcessor->execute(std::move(cmd));
                m_assemblyView->update();
            });

    // Hierarchy panel render mode change request -> CommandProcessor
    connect(m_hierarchyPanel, &HierarchyPanel::renderModeChangeRequested,
            this, [this](const QString& componentId, RenderMode newMode) {
                if (!m_commandProcessor || !m_assembly) {
                    return;
                }
                Component* comp = m_assembly->component(componentId.toStdString());
                if (!comp) {
                    return;
                }
                RenderMode oldMode = comp->render_mode();
                if (oldMode == newMode) {
                    return;
                }
                auto cmd = std::make_unique<CmdSetRenderMode>(
                    componentId.toStdString(), oldMode, newMode);
                m_commandProcessor->execute(std::move(cmd));
                m_assemblyView->onComponentRenderModeChanged(componentId, newMode);
                if (m_hierarchyPanel) {
                    m_hierarchyPanel->updateRenderModeDisplay(componentId);
                }
            });
}

void MainWindow::onFileNew()
{
    m_assembly = std::make_unique<Assembly>();
    m_assembly->set_name("Untitled");
    m_currentFilePath.clear();
    m_assemblyView->setAssembly(m_assembly.get());
    m_hierarchyPanel->setAssembly(m_assembly.get());
    m_propertiesPanel->setAssembly(m_assembly.get());
    m_propertiesPanel->clearSelection();
    if (m_netGraphPanel) {
        m_netGraphPanel->setAssembly(m_assembly.get());
    }

    setWindowTitle("Chiplet Studio - Untitled");

    // Initialize command processor for undo/redo
    initializeCommandProcessor();

    // Clear flow panel
    if (m_flowEngine) {
        m_flowEngine->clear_steps();
    }
    if (m_flowPanel) {
        m_flowPanel->set_flow_engine(nullptr);
    }

    // Reset autosave timer
    if (m_autoSaveTimer) {
        m_autoSaveTimer->start(AUTO_SAVE_INTERVAL_MS);
    }
}

void MainWindow::openFile(const QString& path)
{
    if (path.isEmpty() || !QFile::exists(path)) {
        return;
    }

    m_pendingLoadPath = path;
    m_loadCanceled = false;
    emit loadingStarted(path);

    m_loadProgress = new QProgressDialog("Loading Assembly...", "Cancel", 0, 0, this);
    m_loadProgress->setWindowTitle("Loading");
    m_loadProgress->setWindowModality(Qt::WindowModal);
    m_loadProgress->setMinimumDuration(0);
    m_loadProgress->setValue(0);
    connect(m_loadProgress, &QProgressDialog::canceled,
            this, &MainWindow::onLoadCanceled);

    QFuture<LoadResult> future = QtConcurrent::run([path]() -> LoadResult {
        LoadResult result;
        try {
            ChipletFormat format;
            result.assembly = format.load(path.toStdString());
        } catch (const std::exception& e) {
            result.error = QString::fromStdString(e.what());
        }
        return result;
    });
    m_loadWatcher->setFuture(future);
}

void MainWindow::onFileOpen()
{
    QString path = QFileDialog::getOpenFileName(
        this,
        "Open Assembly",
        QString(),
        "Chiplet Files (*.chiplet *.yaml *.yml);;All Files (*)"
    );

    if (path.isEmpty()) {
        return;
    }

    // Store path and reset cancel flag
    m_pendingLoadPath = path;
    m_loadCanceled = false;

    // Emit signal for testing
    emit loadingStarted(path);

    // Create and show progress dialog
    m_loadProgress = new QProgressDialog("Loading Assembly...", "Cancel", 0, 0, this);
    m_loadProgress->setWindowTitle("Loading");
    m_loadProgress->setWindowModality(Qt::WindowModal);
    m_loadProgress->setMinimumDuration(0);  // Show immediately
    m_loadProgress->setValue(0);

    // Connect cancel button
    connect(m_loadProgress, &QProgressDialog::canceled,
            this, &MainWindow::onLoadCanceled);

    // Start async loading
    // Note: We capture path by value since it needs to outlive this scope
    QFuture<LoadResult> future = QtConcurrent::run([path]() -> LoadResult {
        LoadResult result;
        try {
            ChipletFormat format;
            result.assembly = format.load(path.toStdString());
        } catch (const std::exception& e) {
            result.error = QString::fromStdString(e.what());
        }
        return result;
    });

    m_loadWatcher->setFuture(future);
}

void MainWindow::onAssemblyLoadFinished()
{
    // Check if load was canceled BEFORE closing progress dialog
    // (closing the dialog can emit canceled() signal)
    bool wasCanceled = m_loadCanceled;

    // Clean up progress dialog - disconnect signal first to avoid race condition
    if (m_loadProgress) {
        disconnect(m_loadProgress, &QProgressDialog::canceled,
                   this, &MainWindow::onLoadCanceled);
        m_loadProgress->close();
        m_loadProgress->deleteLater();
        m_loadProgress = nullptr;
    }

    // Check if load was canceled
    if (wasCanceled) {
        m_pendingLoadPath.clear();
        return;
    }

    // Get result from future (takeResult() moves, avoiding copy)
    LoadResult result = m_loadWatcher->future().takeResult();

    if (!result.error.isEmpty()) {
        QMessageBox::critical(this, "Error",
            QString("Failed to load assembly: %1").arg(result.error));
        emit loadingError(result.error);
        m_pendingLoadPath.clear();
        return;
    }

    if (!result.assembly) {
        QMessageBox::critical(this, "Error", "Failed to load assembly: Unknown error");
        emit loadingError("Unknown error");
        m_pendingLoadPath.clear();
        return;
    }

    // Successfully loaded - update UI on main thread
    m_assembly = std::move(result.assembly);
    m_currentFilePath = m_pendingLoadPath;
    m_pendingLoadPath.clear();

    // Detect cells for components that need them (shows dialog if needed)
    bool cellsChanged = detectAndSelectCells();

    // If cells were detected/selected, save back to file
    if (cellsChanged && !m_currentFilePath.isEmpty()) {
        try {
            ChipletFormat format;
            format.save(*m_assembly, m_currentFilePath.toStdString());
            statusBar()->showMessage("Saved cell selections to " + m_currentFilePath, 3000);
        } catch (const std::exception& e) {
            qWarning() << "Failed to save cell selections:" << e.what();
        }
    }

    // Update all views (mesh generation happens here on main thread)
    m_assemblyView->setAssembly(m_assembly.get());
    m_hierarchyPanel->setAssembly(m_assembly.get());
    m_propertiesPanel->setAssembly(m_assembly.get());
    m_propertiesPanel->clearSelection();
    if (m_netGraphPanel) {
        m_netGraphPanel->setAssembly(m_assembly.get());
    }

    setWindowTitle(QString("Chiplet Studio - %1").arg(
        QString::fromStdString(m_assembly->name())));

    // Initialize command processor for undo/redo
    initializeCommandProcessor();

    // Populate flow engine from assembly's flow definition
    populateFlowEngine();

    // Load assembly GDS into 2D panel if available
    loadAssemblyGds();

    // Reset autosave timer
    if (m_autoSaveTimer) {
        m_autoSaveTimer->start(AUTO_SAVE_INTERVAL_MS);
    }

    statusBar()->showMessage("Loaded: " + m_currentFilePath, 3000);
    emit loadingFinished();
}

void MainWindow::onLoadCanceled()
{
    m_loadCanceled = true;
    // The watcher will still finish, but we'll ignore the result
    statusBar()->showMessage("Load canceled", 2000);
}

QString MainWindow::resolveAssemblyGdsPath() const
{
    if (!m_assembly) return {};

    // 1. Explicit assembly_gds field
    if (!m_assembly->assembly_gds().empty()) {
        QString path = QString::fromStdString(m_assembly->assembly_gds());
        if (QFile::exists(path)) {
            return path;
        }
    }

    // 2. Auto-detect from interposer layout path
    for (const auto& comp : m_assembly->components()) {
        if (comp->type() == ComponentType::Interposer && !comp->layout_path().empty()) {
            QFileInfo info(QString::fromStdString(comp->layout_path()));
            QString baseName = info.completeBaseName();
            QString dir = info.absolutePath();
            QString suffix = info.suffix();

            // Try replacing "_interposer" with "_complete"
            if (baseName.contains("_interposer", Qt::CaseInsensitive)) {
                QString candidate = dir + "/" +
                    baseName.replace("_interposer", "_complete", Qt::CaseInsensitive) +
                    "." + suffix;
                if (QFile::exists(candidate)) {
                    return candidate;
                }
            }

            // Try appending "_complete" before extension
            QString candidate2 = dir + "/" + info.completeBaseName() + "_complete." + suffix;
            if (QFile::exists(candidate2)) {
                return candidate2;
            }

            break;  // Only check first interposer
        }
    }

    return {};
}

void MainWindow::loadAssemblyGds()
{
    if (!m_assembly) return;

    QString assemblyGds = resolveAssemblyGdsPath();
    if (assemblyGds.isEmpty()) return;

    // Defer loading to next event loop iteration so KLayout widget
    // initialization completes before we start loading layouts.
    // Without this, KLayout's hierarchy panel initialization
    // can trigger a menu assertion during first-time widget creation.
    QTimer::singleShot(0, this, [this, assemblyGds]() {
        if (!m_assembly) return;

        // Find best LYP from technologies
        QString lypPath;
        for (const auto& tech : m_assembly->technologies()) {
            if (!tech->layer_properties_path().empty()) {
                lypPath = QString::fromStdString(tech->layer_properties_path());
                break;
            }
        }

        m_drillDownPanel->setAssemblyGds(assemblyGds, lypPath, *m_assembly);
        m_klayout2DDock->show();
        m_klayout2DDock->raise();
    });
}

void MainWindow::initializeCommandProcessor()
{
    m_commandProcessor = std::make_unique<CommandProcessor>(m_assembly.get());
    connect(m_commandProcessor.get(), &CommandProcessor::can_undo_changed,
            m_undoAction, &QAction::setEnabled);
    connect(m_commandProcessor.get(), &CommandProcessor::can_redo_changed,
            m_redoAction, &QAction::setEnabled);
    connect(m_commandProcessor.get(), &CommandProcessor::stack_changed,
            this, [this]() {
        if (m_commandProcessor->can_undo()) {
            m_undoAction->setText("&Undo " + m_commandProcessor->undo_description());
        } else {
            m_undoAction->setText("&Undo");
        }
        if (m_commandProcessor->can_redo()) {
            m_redoAction->setText("&Redo " + m_commandProcessor->redo_description());
        } else {
            m_redoAction->setText("&Redo");
        }
    });

    // Update script engine with current assembly
    if (m_scriptEngine && m_scriptEngine->is_initialized()) {
        m_scriptEngine->set_assembly(m_assembly.get(), m_commandProcessor.get());
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
        m_drillDownPanel->clearContext();
        return;
    }

    // Get layer properties from technology
    QString lypPath;
    QString techName;
    if (!comp->technology().empty()) {
        techName = QString::fromStdString(comp->technology());
        Technology* tech = m_assembly->technology(comp->technology());
        if (tech && !tech->layer_properties_path().empty()) {
            lypPath = QString::fromStdString(tech->layer_properties_path());
        }
    }

    // Load layout and show 2D dock
    QString layoutPath = QString::fromStdString(comp->layout_path());
    if (m_klayout2DView->loadLayout(layoutPath, lypPath)) {
        // Set context on drill-down panel (populates cell combo, reparents side panels)
        QString compName = QString::fromStdString(comp->name());
        m_drillDownPanel->setContext(componentId, compName, techName);

        // If component has a top cell, navigate to it
        if (!comp->top_cell().empty()) {
            m_klayout2DView->setCurrentCell(
                QString::fromStdString(comp->top_cell()));
        }

        m_klayout2DDock->show();
        m_klayout2DDock->raise();
    }
}

void MainWindow::setRecoveredAssembly(std::unique_ptr<Assembly> assembly)
{
    if (!assembly) {
        return;
    }

    m_assembly = std::move(assembly);
    m_currentFilePath.clear();  // Recovered, needs Save As
    m_assemblyView->setAssembly(m_assembly.get());
    m_hierarchyPanel->setAssembly(m_assembly.get());
    m_propertiesPanel->setAssembly(m_assembly.get());
    m_propertiesPanel->clearSelection();
    if (m_netGraphPanel) {
        m_netGraphPanel->setAssembly(m_assembly.get());
    }

    setWindowTitle(QString("Chiplet Studio - %1 [Recovered]").arg(
        QString::fromStdString(m_assembly->name())));

    // Initialize command processor for undo/redo
    initializeCommandProcessor();

    // Populate flow engine from recovered assembly
    populateFlowEngine();

    // Reset autosave timer
    if (m_autoSaveTimer) {
        m_autoSaveTimer->start(AUTO_SAVE_INTERVAL_MS);
    }
}

void MainWindow::setupSnapToolbar()
{
    QToolBar* snapToolbar = addToolBar("Snapping");
    snapToolbar->setMovable(false);

    // Snap checkbox
    m_snapEnable = new QCheckBox("Snap", this);
    m_snapEnable->setToolTip("Snap to grid");
    m_snapEnable->setChecked(false);
    snapToolbar->addWidget(m_snapEnable);

    snapToolbar->addSeparator();

    // Grid size spinbox
    QLabel* gridLabel = new QLabel("Grid:", this);
    snapToolbar->addWidget(gridLabel);

    m_gridSizeSpinBox = new QDoubleSpinBox(this);
    m_gridSizeSpinBox->setRange(0.1, 1000.0);
    m_gridSizeSpinBox->setValue(10.0);  // Default: 10um
    m_gridSizeSpinBox->setSuffix(" um");
    m_gridSizeSpinBox->setDecimals(1);
    m_gridSizeSpinBox->setToolTip("Grid spacing in micrometers");
    snapToolbar->addWidget(m_gridSizeSpinBox);

    // Preset buttons
    QPushButton* btn1um = new QPushButton("1", this);
    QPushButton* btn10um = new QPushButton("10", this);
    QPushButton* btn100um = new QPushButton("100", this);

    btn1um->setFixedWidth(30);
    btn10um->setFixedWidth(30);
    btn100um->setFixedWidth(35);

    btn1um->setToolTip("Set grid to 1um");
    btn10um->setToolTip("Set grid to 10um");
    btn100um->setToolTip("Set grid to 100um");

    snapToolbar->addWidget(btn1um);
    snapToolbar->addWidget(btn10um);
    snapToolbar->addWidget(btn100um);

    // Connect signals
    connect(m_snapEnable, &QCheckBox::toggled, this, [this](bool checked) {
        m_snapEnabled = checked;
    });

    connect(m_gridSizeSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double value) {
        m_gridSize = value;
    });

    connect(btn1um, &QPushButton::clicked, this, [this]() {
        m_gridSizeSpinBox->setValue(1.0);
    });
    connect(btn10um, &QPushButton::clicked, this, [this]() {
        m_gridSizeSpinBox->setValue(10.0);
    });
    connect(btn100um, &QPushButton::clicked, this, [this]() {
        m_gridSizeSpinBox->setValue(100.0);
    });
}

void MainWindow::setupViewModeToolbar()
{
    QToolBar* renderToolbar = addToolBar("View Mode");
    renderToolbar->setMovable(false);

    // Label
    QLabel* modeLabel = new QLabel("View:", this);
    renderToolbar->addWidget(modeLabel);

    // Button group for exclusive selection
    m_viewModeGroup = new QButtonGroup(this);
    m_viewModeGroup->setExclusive(true);

    // Box mode button (simple boxes)
    QPushButton* btnBox = new QPushButton("Box", this);
    btnBox->setCheckable(true);
    btnBox->setToolTip("Simple box representation (fast)");
    btnBox->setFixedWidth(50);
    m_viewModeGroup->addButton(btnBox, static_cast<int>(ViewMode::BoxMode));
    renderToolbar->addWidget(btnBox);

    // Layer mode button (KLayout 2.5D style)
    QPushButton* btnLayer = new QPushButton("Layer", this);
    btnLayer->setCheckable(true);
    btnLayer->setChecked(true);  // Default mode
    btnLayer->setToolTip("Layer-by-layer 2.5D visualization (like KLayout)");
    btnLayer->setFixedWidth(50);
    m_viewModeGroup->addButton(btnLayer, static_cast<int>(ViewMode::LayerMode));
    renderToolbar->addWidget(btnLayer);

    // Connect button group to AssemblyView
    connect(m_viewModeGroup, QOverload<int>::of(&QButtonGroup::idClicked),
            this, [this](int id) {
        if (m_assemblyView) {
            m_assemblyView->setViewMode(static_cast<ViewMode>(id));
        }
    });

    // Z offset control
    renderToolbar->addSeparator();
    QLabel* zLabel = new QLabel("Z Offset (um):", this);
    renderToolbar->addWidget(zLabel);

    m_zOffsetSpinBox = new QDoubleSpinBox(this);
    m_zOffsetSpinBox->setRange(-10000.0, 10000.0);
    m_zOffsetSpinBox->setSingleStep(1.0);
    m_zOffsetSpinBox->setDecimals(1);
    m_zOffsetSpinBox->setValue(0.0);
    m_zOffsetSpinBox->setToolTip("Global Z offset for all components (micrometers)");
    m_zOffsetSpinBox->setFixedWidth(90);
    renderToolbar->addWidget(m_zOffsetSpinBox);

    connect(m_zOffsetSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double value) {
        if (m_assemblyView) {
            m_assemblyView->setGlobalZOffset(value);
        }
    });

    // Shape filter slider (area-based polygon filtering for Detailed mode)
    renderToolbar->addSeparator();
    QLabel* filterLabel = new QLabel("Filter:", this);
    renderToolbar->addWidget(filterLabel);

    m_shapeFilterSlider = new QSlider(Qt::Horizontal, this);
    m_shapeFilterSlider->setRange(0, 1000);
    m_shapeFilterSlider->setValue(0);
    m_shapeFilterSlider->setMinimumWidth(120);
    m_shapeFilterSlider->setEnabled(false);  // no component selected yet
    m_shapeFilterSlider->setToolTip(
        "Shape area filter for the selected component: hide small polygons (0% = show all)");
    renderToolbar->addWidget(m_shapeFilterSlider);

    m_shapeFilterLabel = new QLabel("--", this);
    m_shapeFilterLabel->setMinimumWidth(40);
    m_shapeFilterLabel->setAlignment(Qt::AlignCenter);
    renderToolbar->addWidget(m_shapeFilterLabel);

    connect(m_shapeFilterSlider, &QSlider::valueChanged,
            this, [this](int value) {
        double percent = value / 10.0;
        m_shapeFilterLabel->setText(QString("%1%").arg(percent, 0, 'f', 1));
        if (!m_assemblyView) return;
        const QString compId = m_assemblyView->selectedComponent();
        if (compId.isEmpty()) return;  // slider should already be disabled
        m_assemblyView->setShapeFilterPercent(compId, percent);
    });
}

void MainWindow::setupScriptConsole()
{
    // Create script engine
    m_scriptEngine = std::make_unique<ScriptEngine>(this);

    // Initialize Python interpreter
    if (ScriptEngine::is_available()) {
        if (!m_scriptEngine->initialize()) {
            qWarning() << "Failed to initialize Python interpreter";
        }
    }

    // Create console widget
    m_scriptConsole = new ScriptConsole(this);
    m_scriptConsole->set_engine(m_scriptEngine.get());

    // Create dock widget
    m_scriptConsoleDock = new QDockWidget("Python Console", this);
    m_scriptConsoleDock->setWidget(m_scriptConsole);
    m_scriptConsoleDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);

    // Add to bottom dock area
    addDockWidget(Qt::BottomDockWidgetArea, m_scriptConsoleDock);

    // Set initial height
    m_scriptConsoleDock->setMinimumHeight(100);
    m_scriptConsoleDock->resize(m_scriptConsoleDock->width(), 200);

    // Add View menu entry
    QMenu* viewMenu = menuBar()->findChild<QMenu*>("viewMenu");
    if (!viewMenu) {
        viewMenu = menuBar()->addMenu("&View");
        viewMenu->setObjectName("viewMenu");
    }

    QAction* toggleConsole = m_scriptConsoleDock->toggleViewAction();
    toggleConsole->setText("Python Console");
    toggleConsole->setShortcut(QKeySequence("Ctrl+`"));
    viewMenu->addAction(toggleConsole);

    // 2D Layout dock toggle
    QAction* toggle2D = m_klayout2DDock->toggleViewAction();
    toggle2D->setText("2D Layout");
    toggle2D->setShortcut(QKeySequence("F4"));
    viewMenu->addAction(toggle2D);
}

void MainWindow::setupFlowPanel()
{
    m_flowEngine = new FlowEngine(this);

    m_flowPanel = new FlowPanel(this);

    m_flowPanelDock = new QDockWidget("Flow Pipeline", this);
    m_flowPanelDock->setWidget(m_flowPanel);
    m_flowPanelDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);

    addDockWidget(Qt::BottomDockWidgetArea, m_flowPanelDock);

    m_flowPanelDock->setMinimumHeight(100);
    m_flowPanelDock->resize(m_flowPanelDock->width(), 200);

    // Tab with script console, hidden by default (View > Flow Pipeline to show)
    if (m_scriptConsoleDock) {
        tabifyDockWidget(m_scriptConsoleDock, m_flowPanelDock);
        m_scriptConsoleDock->raise();
    }
    m_flowPanelDock->hide();

    // Connect FlowPanel signals to FlowEngine
    connect(m_flowPanel, &FlowPanel::stepRunRequested,
            this, [this](const QString& id) {
                if (m_flowEngine) {
                    m_flowEngine->run_step(id.toStdString());
                }
            });

    connect(m_flowPanel, &FlowPanel::runAllRequested,
            this, [this]() {
                if (m_flowEngine) {
                    m_flowEngine->run_all();
                }
            });

    // View menu entry
    QMenu* viewMenu = menuBar()->findChild<QMenu*>("viewMenu");
    if (!viewMenu) {
        viewMenu = menuBar()->addMenu("&View");
        viewMenu->setObjectName("viewMenu");
    }

    QAction* toggleFlow = m_flowPanelDock->toggleViewAction();
    toggleFlow->setText("Flow Pipeline");
    toggleFlow->setShortcut(QKeySequence("Ctrl+F"));
    viewMenu->addAction(toggleFlow);

    // Base plane toggle
    viewMenu->addSeparator();
    QAction* toggleBasePlane = new QAction("Base Plane", this);
    toggleBasePlane->setCheckable(true);
    toggleBasePlane->setChecked(true);
    toggleBasePlane->setShortcut(QKeySequence("Ctrl+B"));
    connect(toggleBasePlane, &QAction::toggled, m_assemblyView,
            &AssemblyView::setBasePlaneVisible);
    viewMenu->addAction(toggleBasePlane);
}

void MainWindow::setupNetGraphPanel()
{
    m_netGraphPanel = new NetGraphPanel(this);

    m_netGraphDock = new QDockWidget("Net Graph", this);
    m_netGraphDock->setWidget(m_netGraphPanel);
    m_netGraphDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);

    addDockWidget(Qt::BottomDockWidgetArea, m_netGraphDock);
    m_netGraphDock->setMinimumHeight(100);
    m_netGraphDock->resize(m_netGraphDock->width(), 250);

    // Tab with flow panel, hidden by default (View > Net Graph to show)
    if (m_flowPanelDock) {
        tabifyDockWidget(m_flowPanelDock, m_netGraphDock);
    } else if (m_scriptConsoleDock) {
        tabifyDockWidget(m_scriptConsoleDock, m_netGraphDock);
    }
    m_netGraphDock->hide();

    // Bidirectional selection sync
    // NetGraph -> 3D + Hierarchy + Properties
    connect(m_netGraphPanel, &NetGraphPanel::componentSelected,
            m_assemblyView, &AssemblyView::selectComponent);
    connect(m_netGraphPanel, &NetGraphPanel::componentSelected,
            m_hierarchyPanel, &HierarchyPanel::selectComponent);
    connect(m_netGraphPanel, &NetGraphPanel::componentSelected,
            this, [this](const QString& componentId) {
                if (m_propertiesPanel && m_assembly) {
                    m_propertiesPanel->setComponent(componentId.toStdString(), m_assembly.get());
                }
            });

    // 3D/Hierarchy -> NetGraph
    connect(m_assemblyView, &AssemblyView::selectionChanged,
            m_netGraphPanel, &NetGraphPanel::highlightComponent);
    connect(m_hierarchyPanel, &HierarchyPanel::componentSelected,
            m_netGraphPanel, &NetGraphPanel::highlightComponent);

    // View menu entry
    QMenu* viewMenu = menuBar()->findChild<QMenu*>("viewMenu");
    if (!viewMenu) {
        viewMenu = menuBar()->addMenu("&View");
        viewMenu->setObjectName("viewMenu");
    }

    QAction* toggleNetGraph = m_netGraphDock->toggleViewAction();
    toggleNetGraph->setText("Net Graph");
    toggleNetGraph->setShortcut(QKeySequence("Ctrl+G"));
    viewMenu->addAction(toggleNetGraph);
}

void MainWindow::populateFlowEngine()
{
    if (!m_assembly || !m_flowEngine) {
        if (m_flowPanel) {
            m_flowPanel->set_flow_engine(nullptr);
        }
        return;
    }

    m_flowEngine->clear_steps();

    if (!m_assembly->has_flow()) {
        m_flowPanel->set_flow_engine(nullptr);
        return;
    }

    const FlowDefinition& def = m_assembly->flow_definition();

    m_flowEngine->set_working_directory(def.working_directory);

    for (const auto& [key, value] : def.environment) {
        m_flowEngine->set_environment(key, value);
    }

    for (const auto& step : def.steps) {
        m_flowEngine->add_step(step);
    }

    m_flowPanel->set_flow_engine(m_flowEngine);
    qDebug() << "Loaded flow pipeline with"
             << m_flowEngine->step_count() << "steps";
}

void MainWindow::setupAutoSave()
{
    m_autoSaveTimer = new QTimer(this);
    connect(m_autoSaveTimer, &QTimer::timeout,
            this, &MainWindow::onAutoSave);

    // Timer starts when an assembly is loaded/created
}

void MainWindow::onAutoSave()
{
    if (!m_assembly) {
        return;
    }

    QString savePath = autoSavePath();
    if (savePath.isEmpty()) {
        return;
    }

    // Serialize on main thread (fast - just building YAML in memory)
    try {
        ChipletFormat format;
        // We need to save first to get the serialized content
        // For now, do a synchronous save since serialization is fast
        // The actual file write could be backgrounded but YAML generation
        // needs the assembly which shouldn't be modified during save
        format.save(*m_assembly, savePath.toStdString());
        statusBar()->showMessage("Auto-saved to " + savePath, 3000);
        qDebug() << "Auto-saved to" << savePath;
    } catch (const std::exception& e) {
        qWarning() << "Auto-save failed:" << e.what();
        statusBar()->showMessage("Auto-save failed: " + QString::fromStdString(e.what()), 5000);
    }
}

QString MainWindow::autoSavePath() const
{
    // Use a consistent autosave location
    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dataPath.isEmpty()) {
        dataPath = QDir::tempPath();
    }

    // Ensure directory exists
    QDir dir(dataPath);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    // If we have a current file path, save alongside it
    if (!m_currentFilePath.isEmpty()) {
        QFileInfo info(m_currentFilePath);
        return info.absolutePath() + "/" + info.baseName() + ".autosave.chiplet";
    }

    // Otherwise use the app data location
    return dataPath + "/autosave.chiplet";
}

bool MainWindow::detectAndSelectCells()
{
    if (!m_assembly) {
        return false;
    }

    bool anyChanges = false;
    GDSAnalyzer analyzer;

    for (const auto& compPtr : m_assembly->components()) {
        Component* comp = compPtr.get();
        if (!comp) continue;

        // Skip if no layout path
        if (comp->layout_path().empty()) {
            continue;
        }

        // Skip if cells already specified
        if (!comp->cells().empty()) {
            continue;
        }

        // Analyze the GDS file
        QString layoutPath = QString::fromStdString(comp->layout_path());
        auto cells = analyzer.analyzeCells(comp->layout_path());

        if (cells.empty()) {
            qWarning() << "No cells found in GDS:" << layoutPath;
            continue;
        }

        // If only one cell, use it automatically
        if (cells.size() == 1) {
            comp->set_top_cell(cells[0].name);
            anyChanges = true;
            qDebug() << "Auto-selected single cell" << QString::fromStdString(cells[0].name)
                     << "for component" << QString::fromStdString(comp->id());
            continue;
        }

        // Check if it's a flat GDS (all cells are top candidates)
        bool isFlatGds = analyzer.isFlatGDS(comp->layout_path());

        // Count top candidates
        int topCandidateCount = 0;
        for (const auto& cell : cells) {
            if (cell.isTopCandidate) {
                topCandidateCount++;
            }
        }

        // If exactly one top candidate, use it automatically
        if (!isFlatGds && topCandidateCount == 1) {
            for (const auto& cell : cells) {
                if (cell.isTopCandidate) {
                    comp->set_top_cell(cell.name);
                    anyChanges = true;
                    qDebug() << "Auto-selected top cell" << QString::fromStdString(cell.name)
                             << "for component" << QString::fromStdString(comp->id());
                    break;
                }
            }
            continue;
        }

        // Need user selection: flat GDS or multiple top candidates
        QString compName = QString::fromStdString(comp->name());
        QString gdsFile = QFileInfo(layoutPath).fileName();

        CellSelectionDialog dialog(cells, compName, gdsFile, isFlatGds, this);

        if (dialog.exec() == QDialog::Accepted) {
            QStringList selectedCells = dialog.selectedCells();

            if (!selectedCells.isEmpty()) {
                std::vector<std::string> cellsVec;
                for (const QString& cellName : selectedCells) {
                    cellsVec.push_back(cellName.toStdString());
                }
                comp->set_cells(cellsVec);
                anyChanges = true;

                qDebug() << "User selected" << selectedCells.size() << "cells for component"
                         << QString::fromStdString(comp->id());
            }
        } else {
            // User canceled - use all top candidates as default
            std::vector<std::string> defaultCells;
            for (const auto& cell : cells) {
                if (cell.isTopCandidate || isFlatGds) {
                    defaultCells.push_back(cell.name);
                }
            }
            if (!defaultCells.empty()) {
                comp->set_cells(defaultCells);
                anyChanges = true;
                qDebug() << "Using default cells for component" << QString::fromStdString(comp->id());
            }
        }
    }

    return anyChanges;
}

} // namespace chiplet
