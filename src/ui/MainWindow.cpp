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
#include "ImportGdsDialog.h"
#include "CrashRecoveryDialog.h"
#include "ExportPngDialog.h"
#include "ViewingNotesBar.h"
#include "ShortcutPolicy.h"
#include "DockWidthPolicy.h"
#include "PanelWiring.h"
#include "view2d/KLayout2DView.h"
#include "view2d/CellComponentMapper.h"
#include "view3d/AssemblyView.h"
#include "view3d/SceneMiniMap.h"
#include "view3d/GDSAnalyzer.h"
#include "view3d/GDSLayerExtractor.h"
#include "formats/ChipletFormat.h"
#include "formats/ChipletSynth.h"

#include <optional>
#include "core/Technology.h"
#include "core/LayerStackup.h"
#include "core/TechResolve.h"
#include "view2d/AssemblySheet.h"
#include "core/commands/CmdMoveComponent.h"
#include "core/commands/CmdSetRenderMode.h"
#include "scripting/ScriptEngine.h"
#include "core/flow/FlowEngine.h"
#include "core/flow/FlowConfig.h"
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QDockWidget>
#include <QAbstractButton>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QToolBar>
#include <QSlider>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QProgressDialog>
#include <QTimer>
#include <QStatusBar>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QUuid>
#include <QtConcurrent/QtConcurrent>
#include <QApplication>
#include <QScreen>
#include <QImage>
#include <QEvent>
#include <QCloseEvent>
#include <QElapsedTimer>
#include <QtCore/qscopeguard.h>

namespace chiplet {



MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("Chiplet Studio");
    resize(1280, 800);

    setupMenus();
    setupPanels();
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

    QAction* reloadAction = fileMenu->addAction("&Reload");
    reloadAction->setShortcut(QKeySequence::Refresh);
    connect(reloadAction, &QAction::triggered, this, &MainWindow::onFileReload);

    QAction* saveAction = fileMenu->addAction("&Save");
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, &MainWindow::onFileSave);

    QAction* saveAsAction = fileMenu->addAction("Save &As...");
    // SaveAs is the one standard key the generic theme left unbound in the
    // rehearsal image: the menu showed a blank accelerator and Ctrl+Shift+S did
    // nothing, while New/Open/Save/Refresh/Quit all resolved. Fall back only
    // when the platform gave us nothing.
    saveAsAction->setShortcuts(
        shortcutsOrFallback(QKeySequence::keyBindings(QKeySequence::SaveAs),
                            QStringLiteral("Ctrl+Shift+S")));
    connect(saveAsAction, &QAction::triggered, this, &MainWindow::onFileSaveAs);

    QAction* recoveryAction = fileMenu->addAction("Recovery &Checkpoints...");
    connect(recoveryAction, &QAction::triggered, this, &MainWindow::onFileRecovery);

    fileMenu->addSeparator();

    QAction* importGdsAction = fileMenu->addAction("&Import GDS...");
    importGdsAction->setShortcut(QKeySequence("Ctrl+I"));
    connect(importGdsAction, &QAction::triggered, this, &MainWindow::onImportGds);

    QAction* exportPngAction = fileMenu->addAction("&Export PNG...");
    exportPngAction->setShortcut(QKeySequence("Ctrl+E"));
    connect(exportPngAction, &QAction::triggered, this, &MainWindow::onExportPng);

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
    m_hierarchyDock = new QDockWidget("Hierarchy", this);
    m_hierarchyPanel = new HierarchyPanel(m_hierarchyDock);
    m_hierarchyDock->setWidget(m_hierarchyPanel);
    addDockWidget(Qt::LeftDockWidgetArea, m_hierarchyDock);

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

    // View menu toggles so closed docks can be reopened. Without these, closing
    // Properties or Hierarchy via the title-bar X left no way to bring them back.
    {
        QMenu* viewMenu = menuBar()->findChild<QMenu*>("viewMenu");
        if (!viewMenu) {
            viewMenu = menuBar()->addMenu("&View");
            viewMenu->setObjectName("viewMenu");
        }
        QAction* toggleHierarchy = m_hierarchyDock->toggleViewAction();
        toggleHierarchy->setText("Hierarchy");
        toggleHierarchy->setShortcut(QKeySequence("F2"));
        viewMenu->addAction(toggleHierarchy);

        QAction* toggleProperties = m_propertiesDock->toggleViewAction();
        toggleProperties->setText("Properties");
        toggleProperties->setShortcut(QKeySequence("F3"));
        viewMenu->addAction(toggleProperties);
    }

    // Central 3D view widget, under the notes strip that speaks for both views
    m_assemblyView = new AssemblyView(this);
    {
        auto* central = new QWidget(this);
        auto* centralLayout = new QVBoxLayout(central);
        centralLayout->setContentsMargins(0, 0, 0, 0);
        centralLayout->setSpacing(0);
        m_viewingNotesBar = new ViewingNotesBar(central);
        centralLayout->addWidget(m_viewingNotesBar);
        centralLayout->addWidget(m_assemblyView, 1);
        setCentralWidget(central);
    }
    connect(m_assemblyView, &AssemblyView::notesChanged,
            this, &MainWindow::refreshViewingNotes);
    connect(m_assemblyView, &AssemblyView::layerRowsChanged, this, [this]() {
        if (m_propertiesPanel) m_propertiesPanel->refreshLayerRows();
    });
    // A 2D load starts with fill hidden; if the switch is on, follow it. The
    // panel repopulates its list on the same signal, connected earlier.
    connect(m_klayout2DView, &KLayout2DView::notesChanged,
            this, &MainWindow::refreshViewingNotes);
    connect(m_klayout2DView, &KLayout2DView::layoutChanged, this, [this](bool) {
        if (m_showFillAction && m_showFillAction->isChecked() && m_drillDownPanel) {
            m_drillDownPanel->setFillVisible(true);
        }
        refreshViewingNotes();
    });

    // 3D top-down mini-map / navigator: a corner overlay child of the view so it
    // composites on top of the QOpenGLWidget. MainWindow keeps it pinned to the
    // corner (eventFilter on the view) and toggles it from the View menu. It
    // rebuilds its floor-plan on content/selection changes and refreshes its
    // camera marker on every camera move.
    m_sceneMiniMap = new SceneMiniMap(m_assemblyView, m_assemblyView);
    m_assemblyView->installEventFilter(this);
    connect(m_assemblyView, &AssemblyView::overviewChanged, this, [this]() {
        // Content (assembly/meshes) changed: rebuild the floor-plan, then decide
        // whether the overlay should be shown (hidden on an empty scene).
        if (m_sceneMiniMap) m_sceneMiniMap->onContentChanged();
        updateSceneMiniMapVisibility();
    });
    connect(m_assemblyView, &AssemblyView::cameraChanged,
            m_sceneMiniMap, &SceneMiniMap::onCameraChanged);
    connect(m_assemblyView, &AssemblyView::selectionChanged,
            m_sceneMiniMap, &SceneMiniMap::onContentChanged);
    updateSceneMiniMapVisibility();

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
    connectPropertiesRequest(m_hierarchyPanel, m_propertiesPanel, m_propertiesDock,
                             [this]() { return m_assembly.get(); });

    // Hierarchy interconnect row -> Properties panel (assembly-level bumping
    // method; clears the 3D component highlight, nothing to highlight for it)
    connect(m_hierarchyPanel, &HierarchyPanel::interconnectSelected,
            this, [this]() {
                m_propertiesPanel->setAssembly(m_assembly.get());
                m_propertiesPanel->showInterconnect();
                m_assemblyView->selectComponent(QString());
            });

    // Die's interconnect method child row -> Properties panel (per-die
    // `connection:`); highlight the die it belongs to in the 3D view.
    connect(m_hierarchyPanel, &HierarchyPanel::interconnectMethodSelected,
            this, [this](const QString& componentId, const QString& methodId) {
                m_propertiesPanel->setAssembly(m_assembly.get());
                m_propertiesPanel->showInterconnectMethod(componentId,
                                                          methodId);
                m_assemblyView->selectComponent(componentId);
            });

    // Hierarchy interconnect checkbox -> show/hide the method's 3D body
    // layers (they render merged into the interposer component's stackup)
    connect(m_hierarchyPanel, &HierarchyPanel::interconnectVisibilityChanged,
            this, [this](bool visible) {
                if (!m_assembly) {
                    return;
                }
                // Raw union of the active methods' fragments: only the
                // layer/datatype keys matter here, the z reference frame is
                // irrelevant for show/hide.
                LayerStackup ic = LayerStackup::loadInterconnectFragments(
                    LayerStackup::resolveInterconnectKeys(
                        m_assembly->interconnect_method_ids(),
                        m_assembly->interconnect_adapter()));
                if (ic.empty()) {
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

    // Properties panel per-layer opacity -> 3D View
    connect(m_propertiesPanel, &PropertiesPanel::layerOpacityChanged,
            m_assemblyView, &AssemblyView::setLayerOpacity);

    // Seed the Transparency dialog with the 3D view's current per-layer opacity.
    m_propertiesPanel->setLayerOpacityResolver(
        [this](const QString& componentId, int layer, int datatype) {
            return m_assemblyView ? m_assemblyView->layerOpacity(componentId, layer, datatype)
                                  : 1.0f;
        });

    // Layer list for technologies with no .lyp (e.g. Import GDS): list the
    // layers the 3D view rendered from the resolved PDK stackup.
    m_propertiesPanel->setLayerListResolver(
        [this](const QString& componentId) {
            std::vector<PropertiesPanel::RenderedLayerInfo> out;
            if (!m_assemblyView) return out;
            for (const auto& e : m_assemblyView->componentLayers(componentId)) {
                out.push_back({e.layer, e.datatype, e.name, e.fill,
                               e.count, e.countNote, e.meshed});
            }
            return out;
        });

    // 3D View double-click for drill-down (if signal exists)
    connect(m_assemblyView, &AssemblyView::componentDoubleClicked,
            this, &MainWindow::onComponentDrillDown);

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
                // The views follow through connectRenderModeSync, as they do
                // on undo and redo.
                auto cmd = std::make_unique<CmdSetRenderMode>(
                    componentId.toStdString(), oldMode, newMode);
                m_commandProcessor->execute(std::move(cmd));
            });
}

bool MainWindow::detachAssemblyFromViews()
{
    // Keep the outgoing document's last full state under its own identity.
    // Opening/New never adopts or overwrites a different recovery checkpoint.
    const bool checkpointed = checkpointRecovery();
    if (!checkpointed && QMessageBox::question(
            this, tr("Recovery copy is stale"),
            tr("The current state could not be checkpointed. Replacing this document "
               "may lose edits that are only in memory. Replace it anyway?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
        return false;
    }
    // Cell selection and view construction can enter nested event loops. Retire
    // the outgoing persistence identity BEFORE any new model becomes visible;
    // an old timer must never checkpoint the new model into the old handle.
    m_installingAssembly = true;
    if (m_autoSaveTimer) m_autoSaveTimer->stop();
    if (checkpointed) retireRecoveryCheckpoint();
    m_recoveryCheckpoint.reset();
    m_autoSaveIdentity = QUuid::createUuid().toString(QUuid::WithoutBraces);
    // Detach every consumer from the current assembly BEFORE it is replaced or
    // freed. detectAndSelectCells() (load path) can spin a modal dialog whose
    // nested event loop processes a pending 3D repaint; a view still holding the
    // just-freed assembly would be a use-after-free. clearSelection precedes the
    // properties panel's setAssembly(nullptr) so it drops any stale component
    // pointer first.
    m_assemblyView->setAssembly(nullptr);
    m_hierarchyPanel->setAssembly(nullptr);
    m_propertiesPanel->clearSelection();
    m_propertiesPanel->setAssembly(nullptr);
    if (m_netGraphPanel) {
        m_netGraphPanel->setAssembly(nullptr);
    }
    m_commandProcessor.reset();  // holds a raw Assembly*
    if (m_scriptEngine) {
        m_scriptEngine->clear_assembly();  // nulls g_scriptAssembly
    }
    // Reset the 2D drill-down dock so it does not show geometry from the
    // outgoing assembly. reset() (not clearContext()) also forgets the stored
    // assembly GDS, otherwise opening a project that has no 2D layout would keep
    // the previous project's layout on screen.
    if (m_drillDownPanel) {
        m_drillDownPanel->reset();
    }
    // Both views open the next document with fill hidden; match the switch.
    if (m_showFillAction) {
        const QSignalBlocker block(m_showFillAction);
        m_showFillAction->setChecked(false);
    }
    updateLayerZSpacingControls();  // no assembly => Layer-Z controls greyed out
    return true;
}

void MainWindow::onFileNew()
{
    const auto installation = qScopeGuard([this] { m_installingAssembly = false; });
    if (!detachAssemblyFromViews()) return;
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

    updateLayerZSpacingControls();  // empty assembly: still greyed (no single die)

    setWindowTitle("Chiplet Studio - Untitled[*]");
    setWindowModified(false);

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

    // Ignore a re-entrant open while a load is already in flight; otherwise the
    // previous QProgressDialog leaks and the in-flight future is detached. This
    // does not block the first CLI/startup open (no load is in flight then).
    if (m_loadProgress || (m_loadWatcher && m_loadWatcher->isRunning())) {
        statusBar()->showMessage("A load is already in progress", 3000);
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

void MainWindow::onFileReload()
{
    // Re-read the current .chiplet from disk. The typical loop is
    // re-export from KiCad, then refresh here -- no close/reopen.
    // openFile() runs the same full load pipeline as Open, so
    // technologies, interconnect fragments and every view rebuild.
    // Same semantics as Open: the in-memory session is replaced.
    if (m_currentFilePath.isEmpty()) {
        statusBar()->showMessage("Nothing to reload (no file loaded)", 3000);
        return;
    }
    if (!QFile::exists(m_currentFilePath)) {
        statusBar()->showMessage(
            "Cannot reload: file no longer exists: " + m_currentFilePath,
            5000);
        return;
    }
    statusBar()->showMessage("Reloading " + m_currentFilePath, 2000);
    openFile(m_currentFilePath);
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

    // Single async-load pipeline (also used by the CLI and Reload paths).
    openFile(path);
}

void MainWindow::onImportGds()
{
    ImportGdsDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    const QString gdsPath = dlg.gdsPath();

    // Detect the top cell. analyzeCells() sorts top candidates first, then by
    // bounding-box area descending, so cells.front() is the right pick whether
    // the GDS is hierarchical (one candidate) or flat (largest by area).
    GDSAnalyzer analyzer;
    auto cells = analyzer.analyzeCells(gdsPath.toStdString());
    if (cells.empty()) {
        QMessageBox::critical(this, tr("Import GDS"),
            tr("Could not read cells from the GDS:\n%1")
                .arg(QString::fromStdString(analyzer.lastError())));
        return;
    }
    const std::string topCell = cells.front().name;

    // Bounding box (micrometers). Falls back to a default footprint if KLayout
    // cannot measure it, so the synthesized die is never zero-sized.
    const GDSBoundingBox bbox =
        GDSLayerExtractor::extractBoundingBox(gdsPath.toStdString(), topCell);

    SingleGdsImportSpec spec;
    spec.gdsPath = QFileInfo(gdsPath).absoluteFilePath().toStdString();
    spec.topCell = topCell;
    if (bbox.is_valid()) {
        spec.widthUm = bbox.width();
        spec.heightUm = bbox.height();
    }
    spec.thicknessUm = dlg.thicknessUm();
    spec.techId = dlg.techId().toStdString();        // "" => custom
    spec.customLyp = dlg.customLyp().toStdString();
    spec.customStackup = dlg.customStackup().toStdString();

    // The write policy lives in ChipletSynth, Qt-free, so the whole of it is
    // testable without a GUI: fresh destinations only, a refusal that leaves
    // nothing behind, and a fallback that follows a real write failure. This
    // slot supplies the one thing that genuinely needs a window, which is asking
    // for a different name.
    QFileInfo gdsInfo(gdsPath);
    const QString preferred =
        gdsInfo.absoluteDir().filePath(gdsInfo.completeBaseName() + ".chiplet");

    const auto askForAnotherName =
        [this](const std::string& occupied) -> std::optional<std::string> {
        const QString taken = QString::fromStdString(occupied);
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("Import GDS"));
        box.setText(tr("%1 already exists.").arg(taken));
        box.setInformativeText(
            tr("The import will not replace a file it did not create. Choose a "
               "different name, or cancel."));
        QAbstractButton* rename =
            box.addButton(tr("Choose a Different Name..."), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() != rename) {
            return std::nullopt;
        }
        const QString chosen = QFileDialog::getSaveFileName(
            this, tr("Save the Imported Chiplet As"), taken,
            tr("Chiplet Files (*.chiplet);;All Files (*)"), nullptr,
            QFileDialog::DontConfirmOverwrite);
        if (chosen.isEmpty()) {
            return std::nullopt;
        }
        return chosen.toStdString();
    };

    const ImportWriteResult written =
        writeSingleGdsImport(spec, preferred.toStdString(), askForAnotherName);

    switch (written.outcome) {
    case ImportWriteResult::Outcome::Cancelled:
        return;
    case ImportWriteResult::Outcome::Refused:
        QMessageBox::critical(this, tr("Import GDS"),
            tr("This GDS cannot be described as a .chiplet, so nothing was "
               "written:\n\n%1").arg(QString::fromStdString(written.message)));
        return;
    case ImportWriteResult::Outcome::Failed:
        QMessageBox::critical(this, tr("Import GDS"),
            tr("The import could not be written:\n\n%1")
                .arg(QString::fromStdString(written.message)));
        return;
    case ImportWriteResult::Outcome::WrittenToFallback:
        statusBar()->showMessage(
            tr("Could not write beside the GDS; imported to %1")
                .arg(QString::fromStdString(written.path)), 8000);
        break;
    case ImportWriteResult::Outcome::Written:
        break;
    }

    // Load through the existing async pipeline (wires views + cell selection).
    openFile(QString::fromStdString(written.path));
}

void MainWindow::onExportPng()
{
    if (!m_assemblyView) {
        return;
    }

    // Seed the dialog with the live framebuffer size (device pixels) so the
    // default export matches what is currently on screen.
    const qreal dpr = m_assemblyView->devicePixelRatioF();
    const QSize viewSize(qMax(1, static_cast<int>(m_assemblyView->width() * dpr)),
                         qMax(1, static_cast<int>(m_assemblyView->height() * dpr)));

    ExportPngDialog dlg(viewSize, this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }

    const QSize size = dlg.outputSize();
    QImage image = m_assemblyView->renderToImage(
        size, dlg.transparentBackground(), dlg.samples());
    if (image.isNull()) {
        QMessageBox::critical(this, tr("Export PNG"),
            tr("Could not render the view at %1 x %2.\n"
               "Try a smaller size or turn off anti-aliasing.")
                .arg(size.width())
                .arg(size.height()));
        return;
    }

    // Write the chosen DPI into the PNG metadata (stored as dots per meter).
    const int dpm = qRound(dlg.dpi() / 0.0254);
    image.setDotsPerMeterX(dpm);
    image.setDotsPerMeterY(dpm);

    // Default name from the assembly, in the directory of the current file.
    QString defaultName = QStringLiteral("chiplet_view.png");
    if (m_assembly && !m_assembly->name().empty()) {
        defaultName = QString::fromStdString(m_assembly->name()) + ".png";
    }
    QString startDir = QDir::homePath();
    if (!m_currentFilePath.isEmpty()) {
        startDir = QFileInfo(m_currentFilePath).absolutePath();
    }

    QString path = QFileDialog::getSaveFileName(
        this, tr("Export PNG"), QDir(startDir).filePath(defaultName),
        tr("PNG Image (*.png)"));
    if (path.isEmpty()) {
        return;
    }
    if (!path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".png");
    }

    if (!image.save(path, "PNG")) {
        QMessageBox::critical(this, tr("Export PNG"),
            tr("Could not write the PNG file:\n%1").arg(path));
        return;
    }

    statusBar()->showMessage(
        tr("Exported %1 x %2 PNG to %3")
            .arg(image.width())
            .arg(image.height())
            .arg(path),
        6000);
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

    // Successfully loaded - update UI on main thread. Detach every consumer
    // from the OUTGOING assembly before the move below frees it (the
    // cell-selection dialog's nested event loop could otherwise repaint a freed
    // assembly). Everything is repointed to the new assembly below.
    const auto installation = qScopeGuard([this] { m_installingAssembly = false; });
    if (!detachAssemblyFromViews()) {
        m_pendingLoadPath.clear();
        statusBar()->showMessage(tr("Kept the current document."), 3000);
        return;
    }

    m_assembly = std::move(result.assembly);
    m_currentFilePath = m_pendingLoadPath;
    m_pendingLoadPath.clear();

    // Detect cells for components that need them (shows dialog if needed). The
    // selections are applied IN MEMORY only: opening a file must not silently
    // overwrite it. Previously this rewrote the source even when the user
    // cancelled and baked resolved absolute paths over the original ${VAR}
    // strings. Selections persist when the user explicitly saves.
    detectAndSelectCells();

    // Update all views (mesh generation happens here on main thread)
    m_assemblyView->setAssembly(m_assembly.get());
    m_hierarchyPanel->setAssembly(m_assembly.get());
    m_propertiesPanel->setAssembly(m_assembly.get());
    m_propertiesPanel->clearSelection();
    if (m_netGraphPanel) {
        m_netGraphPanel->setAssembly(m_assembly.get());
    }

    updateLayerZSpacingControls();  // enable Layer-Z only if this is a lone die

    setWindowTitle(QString("Chiplet Studio - %1[*]").arg(
        QString::fromStdString(m_assembly->name())));
    setWindowModified(false);

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

QString MainWindow::resolveAssemblyGdsPath(bool* interposerOnly,
                                           QString* loneComponent) const
{
    if (interposerOnly) *interposerOnly = false;
    if (loneComponent) loneComponent->clear();
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

            // 2b. No merged/complete GDS on disk: fall back to the interposer's
            //     own layout so the 2D panel shows the substrate instead of
            //     nothing. It will not include the dies (KLayout2DView loads a
            //     single GDS); the caller labels it accordingly.
            QString interposerGds = QString::fromStdString(comp->layout_path());
            if (QFile::exists(interposerGds)) {
                if (interposerOnly) *interposerOnly = true;
                return interposerGds;
            }

            break;  // Only check first interposer
        }
    }

    // 3. Lone-layout fallback: no interposer and no explicit assembly GDS, but a
    //    single component carries a layout. This is exactly what File > Import
    //    GDS produces (one synthesized die), so show that layout in the 2D panel
    //    instead of leaving it empty. Gated on a single layout-bearing component
    //    so it never has to guess which GDS represents the assembly.
    QString loneLayout;
    QString loneId;
    int layoutCount = 0;
    for (const auto& comp : m_assembly->components()) {
        if (!comp->layout_path().empty()) {
            ++layoutCount;
            loneLayout = QString::fromStdString(comp->layout_path());
            loneId = QString::fromStdString(comp->id());
        }
    }
    if (layoutCount == 1 && QFile::exists(loneLayout)) {
        if (loneComponent) *loneComponent = loneId;
        return loneLayout;
    }

    return {};
}

void MainWindow::loadAssemblyGds()
{
    if (!m_assembly) return;

    bool interposerOnly = false;
    QString loneComponent;
    QString assemblyGds = resolveAssemblyGdsPath(&interposerOnly, &loneComponent);
    if (assemblyGds.isEmpty()) return;

    // Defer loading to next event loop iteration so KLayout widget
    // initialization completes before we start loading layouts.
    // Without this, KLayout's hierarchy panel initialization
    // can trigger a menu assertion during first-time widget creation.
    // A replacement (including New or recovery) can happen before this callback.
    // Bind the path to the opened-document identity, not an Assembly address
    // that could be reused after the outgoing model is freed.
    const QString documentIdentity = m_autoSaveIdentity;
    QTimer::singleShot(0, this, [this, assemblyGds, interposerOnly, loneComponent,
                                 documentIdentity]() {
        if (!m_assembly || m_autoSaveIdentity != documentIdentity) return;

        // Each technology styles its own layers: the carrier's sheet for the
        // carrier, each die technology's sheet for its die cells (see
        // AssemblySheet.h). A lone imported layout is all its component's.
        LoadSpec spec = loneComponent.isEmpty()
            ? assemblyLoadSpec(*m_assembly, interposerOnly)
            : componentLoadSpec(*m_assembly, loneComponent.toStdString());
        if (loneComponent.isEmpty() && !interposerOnly) {
            spec.carrierFillPath = carrierFillSource(*m_assembly, assemblyGds.toStdString());
        }

        m_drillDownPanel->setAssemblyGds(assemblyGds, spec, *m_assembly,
                                         interposerOnly);
        m_klayout2DDock->show();
        m_klayout2DDock->raise();
        applyInitial2DDockWidth();
    });
}

void MainWindow::refreshViewingNotes()
{
    if (!m_viewingNotesBar) return;
    QStringList notes;
    if (m_assembly) {
        try {
            for (const auto& n : technologyNotes(*m_assembly)) {
                notes << QString::fromStdString(n);
            }
            for (const auto& n : placementNotes(*m_assembly)) {
                notes << QString::fromStdString(n);
            }
        } catch (const std::exception& e) {
            notes << QString("Viewing notes incomplete: %1").arg(e.what());
        }
    }
    if (m_klayout2DView) notes << m_klayout2DView->notes();
    if (m_assemblyView) notes << m_assemblyView->notes();
    m_viewingNotesBar->setNotes(notes);
}

void MainWindow::applyInitial2DDockWidth()
{
    if (m_klayout2DDockWidthApplied || !m_klayout2DDock || !m_assemblyView) {
        return;
    }
    // Set at SCHEDULING time, not inside the slot below. Both call sites fire
    // straight after show() and raise(), so two of them in one event-loop turn
    // would both pass a guard that is only set when the slot runs, and queue two
    // competing resizes. Once only, and whatever width the user chooses later is
    // theirs.
    m_klayout2DDockWidthApplied = true;

    // Both call sites reach this straight after show() and raise(), where the
    // widths are still whatever the layout had before. Compute a cycle later,
    // on settled numbers: exact arithmetic on pre-layout widths is still wrong.
    QTimer::singleShot(0, this, [this]() {
        if (!m_klayout2DDock || !m_assemblyView) {
            return;
        }
        // A floating dock has its own top-level geometry and a hidden one has
        // no share of the row, so there is nothing here to allocate. Say so
        // rather than resizing something the user detached.
        if (m_klayout2DDock->isFloating() || !m_klayout2DDock->isVisible()) {
            qInfo("2D dock initial width: skipped, dock is %s",
                  m_klayout2DDock->isFloating() ? "floating" : "hidden");
            return;
        }
        const int windowWidth = width();
        const int dockWidth = m_klayout2DDock->width();
        const int centralWidth = m_assemblyView->width();
        const int target = initial2DDockWidth(windowWidth, dockWidth, centralWidth);
        // Any changed, positive target, in either direction. The dock opening
        // too wide is the case this exists for, and a rule that only grows
        // cannot answer it.
        const bool requested = (target > 0 && target != dockWidth);
        if (requested) {
            resizeDocks({m_klayout2DDock}, {target}, Qt::Horizontal);
        }

        // Unconditional, once the layout has answered. resizeDocks is a request:
        // it can be refused, granted, or overshot, and the case where nothing
        // was asked for is the one a reader most needs stated rather than
        // inferred from silence.
        QTimer::singleShot(0, this, [this, windowWidth, dockWidth, centralWidth,
                                     target, requested]() {
            if (!m_klayout2DDock || !m_assemblyView) {
                return;
            }
            qInfo("2D dock initial width: window %d; dock %d -> %d (target %d, "
                  "%s); 3D viewport %d -> %d (floor %d)",
                  windowWidth, dockWidth, m_klayout2DDock->width(), target,
                  requested ? "requested" : "no request", centralWidth,
                  m_assemblyView->width(), kMinCentralWidth);
        });
    });
}

void MainWindow::initializeCommandProcessor()
{
    m_commandProcessor = std::make_unique<CommandProcessor>(m_assembly.get(), this);
    connectRenderModeSync(m_commandProcessor.get(), m_assemblyView, m_hierarchyPanel);
    connect(m_commandProcessor.get(), &CommandProcessor::model_changed,
            this, [this]() {
        setWindowModified(true);
        if (m_installingAssembly) return;
        if (m_recoveryTimer) m_recoveryTimer->start();
        if (m_recoveryWarning && !m_recoveryFailed) {
            m_recoveryWarning->setText(tr("Recovery copy pending"));
            m_recoveryWarning->setToolTip(tr("Waiting for a pause in editing before writing "
                "the recovery copy. Save, auto-save and closing flush it immediately."));
            m_recoveryWarning->setVisible(true);
        }
    });
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
    m_installingAssembly = false;
    checkpointRecovery();
}

void MainWindow::onFileSave()
{
    if (!m_currentFilePath.isEmpty()) {
        saveAssemblyToPath(m_currentFilePath);
        return;
    }
    onFileSaveAs();
}

void MainWindow::onFileSaveAs()
{
    if (!m_assembly) {
        return;
    }

    QString path = QFileDialog::getSaveFileName(
        this,
        "Save As - choose a new filename (unrelated existing files are protected)",
        m_currentFilePath,
        "Chiplet Files (*.chiplet);;All Files (*)",
        nullptr,
        QFileDialog::DontConfirmOverwrite
    );

    if (path.isEmpty()) {
        return;
    }

    saveAssemblyToPath(path);
}

void MainWindow::onFileRecovery()
{
    const auto directory = QStandardPaths::writableLocation(
        QStandardPaths::AppDataLocation).toStdString();
    if (directory.empty()) {
        QMessageBox::warning(this, tr("Recovery checkpoints"),
                            tr("The application recovery directory is unavailable."));
        return;
    }
    try {
        const auto current = m_recoveryCheckpoint && !m_recoveryCheckpoint->closed()
            ? m_recoveryCheckpoint->path() : std::filesystem::path{};
        auto recovered = CrashRecoveryDialog::check_and_recover(directory, true, current);
        if (recovered) setRecoveredAssembly(std::move(recovered));
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Recovery checkpoints"), QString::fromUtf8(e.what()));
    }
}

void MainWindow::saveAssemblyToPath(const QString& path)
{
    if (!m_assembly || path.isEmpty()) return;

    // This Save implementation preserves source spellings; it is not a project
    // relocation/exporter. Do not silently change relative asset resolution.
    const auto& source = m_assembly->document_persistence().source_path;
    if (!source.empty()) {
        const auto sourceDir = QFileInfo(QString::fromStdString(source)).absoluteDir();
        const auto targetDir = QFileInfo(path).absoluteDir();
        if (sourceDir.canonicalPath() != targetDir.canonicalPath()) {
            const auto relative = [](const std::string& reference) {
                return !reference.empty() && reference.rfind("${", 0) != 0 &&
                    QDir::isRelativePath(QString::fromStdString(reference));
            };
            bool dependsOnSourceDirectory = relative(m_assembly->assembly_gds_source()) ||
                relative(m_assembly->netlist().external_netlist_path()) ||
                m_assembly->has_flow() || !m_assembly->flow_source_text().empty();
            for (const auto& component : m_assembly->components())
                dependsOnSourceDirectory |= relative(component->layout_path_source());
            for (const auto& technology : m_assembly->technologies())
                dependsOnSourceDirectory |= relative(technology->layer_properties_source()) ||
                    relative(technology->stackup_source());
            if (dependsOnSourceDirectory) {
                QMessageBox::warning(this, tr("Save As would change asset resolution"),
                    tr("This document uses relative assets or a flow working directory. "
                       "Project relocation is not implemented by Save As. Choose a new "
                       "filename in the original directory:\n%1\n\n"
                       "The destination and your open edits are unchanged.")
                        .arg(sourceDir.absolutePath()));
                return;
            }
        }
    }

    try {
        ChipletFormat format;
        format.save(*m_assembly, path.toStdString());
        m_currentFilePath = path;
        setWindowModified(false);
        setWindowTitle(QString("Chiplet Studio - %1[*]").arg(
            QString::fromStdString(m_assembly->name())));
        statusBar()->showMessage("Saved: " + path, 3000);
        checkpointRecovery();
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Error",
            QString("Failed to save assembly: %1").arg(e.what()));
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

    // The component's own sheet (declared, else built-in) styles its layout.
    const QString techName = QString::fromStdString(comp->technology());

    // Load layout and show 2D dock
    QString layoutPath = QString::fromStdString(comp->layout_path());
    if (m_klayout2DView->loadComposedLayout(
            layoutPath, componentLoadSpec(*m_assembly, comp->id()))) {
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
        applyInitial2DDockWidth();
    }
}

void MainWindow::setRecoveredAssembly(std::unique_ptr<Assembly> assembly)
{
    if (!assembly) {
        return;
    }

    const auto installation = qScopeGuard([this] { m_installingAssembly = false; });
    if (!detachAssemblyFromViews()) return;
    m_assembly = std::move(assembly);
    // The recovered content and the authorized on-disk baseline are distinct.
    // Reuse the recorded pathname, but the format still checks its exact bytes
    // before replacing it. New documents without a receipt require Save As.
    const auto& persistence = m_assembly->document_persistence();
    m_currentFilePath = persistence.primary
        ? QString::fromStdString(persistence.primary->canonical_path()) : QString();
    m_assemblyView->setAssembly(m_assembly.get());
    m_hierarchyPanel->setAssembly(m_assembly.get());
    m_propertiesPanel->setAssembly(m_assembly.get());
    m_propertiesPanel->clearSelection();
    if (m_netGraphPanel) {
        m_netGraphPanel->setAssembly(m_assembly.get());
    }

    updateLayerZSpacingControls();  // enable Layer-Z only if this is a lone die

    setWindowTitle(QString("Chiplet Studio - %1 [Recovered][*]").arg(
        QString::fromStdString(m_assembly->name())));

    // Initialize command processor for undo/redo
    initializeCommandProcessor();
    setWindowModified(true);
    statusBar()->showMessage(tr("Recovered checkpoint; undo/redo history starts empty."), 8000);

    // Populate flow engine from recovered assembly
    populateFlowEngine();

    // Recovery detaches the outgoing 2D view just like a normal open. Restore
    // the recovered document's on-disk layout as well; this does not regenerate
    // that layout from any unsaved component-placement edits.
    loadAssemblyGds();

    // Reset autosave timer
    if (m_autoSaveTimer) {
        m_autoSaveTimer->start(AUTO_SAVE_INTERVAL_MS);
    }
}

void MainWindow::setupViewModeToolbar()
{
    QToolBar* renderToolbar = addToolBar("View Mode");
    renderToolbar->setMovable(false);

    // Layer Z spacing (visualization-only): spread Detailed-mode stackup layers
    // apart in Z to inspect the 3D stackup. 1.00x = physical/default; Reset Z
    // returns to default. Does not change the saved model. Enabled only for a
    // single imported die (a multi-component assembly would interleave the fans),
    // so the controls are greyed out otherwise by updateLayerZSpacingControls().
    m_layerZLabel = new QLabel("Layer Z:", this);
    renderToolbar->addWidget(m_layerZLabel);
    m_layerZSpacingSpin = new QDoubleSpinBox(this);
    m_layerZSpacingSpin->setRange(1.0, AssemblyView::kMaxLayerZSpacing);
    m_layerZSpacingSpin->setDecimals(1);
    m_layerZSpacingSpin->setSingleStep(0.5);
    m_layerZSpacingSpin->setValue(1.0);
    m_layerZSpacingSpin->setSuffix("x");
    m_layerZSpacingSpin->setToolTip(
        "Visualization-only Z exaggeration for Detailed-mode stackups "
        "(1.0x = physical). Spreads layers apart without changing the model. "
        "Available only for a single imported GDS.");
    renderToolbar->addWidget(m_layerZSpacingSpin);
    connect(m_layerZSpacingSpin,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double v) {
        if (m_assemblyView) m_assemblyView->setLayerZSpacing(static_cast<float>(v));
    });
    m_layerZResetAction = renderToolbar->addAction("Reset Z", this, [this]() {
        if (m_layerZSpacingSpin) m_layerZSpacingSpin->setValue(1.0);
    });
    m_layerZResetAction->setToolTip("Reset layer Z spacing to default (1.0x)");

    // Start greyed out: no single-die assembly is loaded yet.
    updateLayerZSpacingControls();

    // Push the filter cluster to the top-right corner: an expanding spacer eats
    // the free width so the control sits flush right, sized at about a quarter
    // of the screen width.
    QWidget* spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    renderToolbar->addWidget(spacer);

    // Shape filter slider (area-based polygon filtering for Detailed mode)
    QLabel* filterLabel = new QLabel("Filter:", this);
    renderToolbar->addWidget(filterLabel);

    m_shapeFilterSlider = new QSlider(Qt::Horizontal, this);
    m_shapeFilterSlider->setRange(0, 1000);
    m_shapeFilterSlider->setValue(0);
    int sliderWidth = 240;
    if (QScreen* screen = QApplication::primaryScreen()) {
        sliderWidth = screen->availableGeometry().width() / 4;
    }
    m_shapeFilterSlider->setFixedWidth(sliderWidth);
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

void MainWindow::updateLayerZSpacingControls()
{
    const bool applicable =
        m_assemblyView && m_assemblyView->layerZSpacingApplicable();

    // setAssembly resets the view's factor to 1.0; mirror that on the spin so it
    // never shows a stale spread after switching assemblies. Block the signal so
    // resetting the widget does not bounce back through setLayerZSpacing.
    if (m_layerZSpacingSpin) {
        const QSignalBlocker block(m_layerZSpacingSpin);
        m_layerZSpacingSpin->setValue(1.0);
        m_layerZSpacingSpin->setEnabled(applicable);
    }
    if (m_layerZLabel) m_layerZLabel->setEnabled(applicable);
    if (m_layerZResetAction) m_layerZResetAction->setEnabled(applicable);
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

    // Tab with the script console; both start hidden. The Python console is a
    // power-user surface reopened via View > Python Console (Ctrl+`). The flow
    // panel has no menu toggle and reveals itself only when the loaded assembly
    // defines a flow (see populateFlowEngine), so it never shows an empty panel.
    if (m_scriptConsoleDock) {
        tabifyDockWidget(m_scriptConsoleDock, m_flowPanelDock);
        m_scriptConsoleDock->hide();
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

    // View menu (kept for the base-plane toggle below). The Flow Pipeline dock has
    // no menu entry on purpose: it is not user-toggled and auto-reveals only when the
    // assembly defines a flow (populateFlowEngine). This keeps the tested flow engine
    // available without advertising an empty panel that no shipped project populates.
    QMenu* viewMenu = menuBar()->findChild<QMenu*>("viewMenu");
    if (!viewMenu) {
        viewMenu = menuBar()->addMenu("&View");
        viewMenu->setObjectName("viewMenu");
    }

    // Base plane toggle
    viewMenu->addSeparator();
    QAction* toggleBasePlane = new QAction("Base Plane", this);
    toggleBasePlane->setCheckable(true);
    toggleBasePlane->setChecked(true);
    toggleBasePlane->setShortcut(QKeySequence("Ctrl+B"));
    connect(toggleBasePlane, &QAction::toggled, m_assemblyView,
            &AssemblyView::setBasePlaneVisible);
    viewMenu->addAction(toggleBasePlane);

    // Fill (layers a stackup marks `purpose: filler`) is hidden at every open
    // in both views; this switches all of it at once. The per-layer boxes in
    // the 2D list and in Properties still work afterwards.
    m_showFillAction = new QAction("Show Fill", this);
    m_showFillAction->setCheckable(true);
    m_showFillAction->setChecked(false);
    m_showFillAction->setToolTip("Show or hide every fill layer in the 2D and 3D views");
    connect(m_showFillAction, &QAction::toggled, this, [this](bool on) {
        m_assemblyView->setShowFill(on);
        if (m_drillDownPanel) m_drillDownPanel->setFillVisible(on);
        if (m_propertiesPanel) m_propertiesPanel->refreshLayerChecks();
    });
    viewMenu->addAction(m_showFillAction);

    // How 3D draws the shapes of a detailed component. Standard is the view
    // as it was, and the setting at every start; the other two say in the
    // notes strip and the layer rows what they drew.
    QMenu* shapesMenu = viewMenu->addMenu("3D Shapes");
    shapesMenu->setObjectName("shapesMenu");
    shapesMenu->setToolTipsVisible(true);
    auto* shapesGroup = new QActionGroup(this);
    shapesGroup->setExclusive(true);
    const struct {
        const char* text;
        const char* name;
        const char* tip;
        ShapeMode mode;
    } shapeModes[] = {
        {"Standard", "shapesStandard",
         "Merged layer meshes, with the shape limits the notes name", ShapeMode::Standard},
        {"Fast", "shapesFast",
         "Every shape; tiles too small on screen are drawn as coverage", ShapeMode::Fast},
        {"Every shape", "shapesEvery",
         "Every shape at every zoom; a frame can take seconds", ShapeMode::Every},
    };
    for (const auto& m : shapeModes) {
        QAction* a = shapesMenu->addAction(m.text);
        a->setObjectName(m.name);
        a->setToolTip(m.tip);
        a->setCheckable(true);
        a->setChecked(m.mode == ShapeMode::Standard);
        shapesGroup->addAction(a);
        const ShapeMode mode = m.mode;
        connect(a, &QAction::triggered, this, [this, mode]() {
            m_assemblyView->setShapeMode(mode);
        });
    }

    // Top-down mini-map toggle
    QAction* toggleMiniMap = new QAction("3D Overview", this);
    toggleMiniMap->setCheckable(true);
    toggleMiniMap->setChecked(m_sceneMiniMapVisible);
    toggleMiniMap->setShortcut(QKeySequence("Ctrl+M"));
    connect(toggleMiniMap, &QAction::toggled, this, [this](bool on) {
        m_sceneMiniMapVisible = on;
        updateSceneMiniMapVisibility();
    });
    viewMenu->addAction(toggleMiniMap);
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
        if (m_flowPanelDock) m_flowPanelDock->hide();
        return;
    }

    m_flowEngine->clear_steps();

    if (!m_assembly->has_flow()) {
        m_flowPanel->set_flow_engine(nullptr);
        if (m_flowPanelDock) m_flowPanelDock->hide();
        return;
    }

    const FlowDefinition& def = m_assembly->flow_definition();

    // The document holds only what its author declared. When it declared no
    // working_directory, the effective one is the directory the .chiplet
    // itself lives in; that default is resolved HERE, at the point of running,
    // and deliberately never written back into the document.
    std::string workingDir = def.working_directory;
    if (workingDir.empty() && !m_currentFilePath.isEmpty()) {
        workingDir = QFileInfo(m_currentFilePath).absolutePath().toStdString();
    }
    m_flowEngine->set_working_directory(workingDir);

    for (const auto& [key, value] : def.environment) {
        m_flowEngine->set_environment(key, value);
    }

    for (const auto& step : def.steps) {
        m_flowEngine->add_step(step);
    }

    m_flowPanel->set_flow_engine(m_flowEngine);
    // The assembly defines a real flow: reveal the panel (it has no menu toggle).
    if (m_flowPanelDock) {
        m_flowPanelDock->show();
        m_flowPanelDock->raise();
    }
    qDebug() << "Loaded flow pipeline with"
             << m_flowEngine->step_count() << "steps";
}

void MainWindow::setupAutoSave()
{
    m_recoveryTimer = new QTimer(this);
    m_recoveryTimer->setSingleShot(true);
    m_recoveryTimer->setInterval(350);
    m_recoveryTimer->setObjectName("recoveryDebounceTimer");
    connect(m_recoveryTimer, &QTimer::timeout, this, &MainWindow::checkpointRecovery);
    m_autoSaveTimer = new QTimer(this);
    connect(m_autoSaveTimer, &QTimer::timeout,
            this, &MainWindow::onAutoSave);

    // A permanent status-bar marker rather than a transient message, because
    // the thing being reported stays true until somebody fixes it. It lives on
    // the right of the status bar, where addPermanentWidget keeps it clear of
    // the transient messages that would otherwise cover it.
    m_autoSaveWarning = new QLabel(this);
    m_autoSaveWarning->setText(tr("Auto-save FAILING"));
    m_autoSaveWarning->setStyleSheet("QLabel { color: #b00020; font-weight: bold; }");
    m_autoSaveWarning->setVisible(false);
    statusBar()->addPermanentWidget(m_autoSaveWarning);

    m_recoveryWarning = new QLabel(tr("Recovery copy STALE"), this);
    m_recoveryWarning->setStyleSheet("QLabel { color: #b00020; font-weight: bold; }");
    m_recoveryWarning->setVisible(false);
    statusBar()->addPermanentWidget(m_recoveryWarning);

    // Timer starts when an assembly is loaded/created
}

void MainWindow::onAutoSave()
{
    if (!m_assembly || m_installingAssembly) {
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
        format.save(*m_assembly, savePath.toStdString(), SavePurpose::Autosave);
        checkpointRecovery();
        applyAutoSaveNotice(m_autoSaveState.on_success(), savePath, QString());
        statusBar()->showMessage("Auto-saved to " + savePath, 3000);
        qDebug() << "Auto-saved to" << savePath;
    } catch (const std::exception& e) {
        const QString reason = QString::fromUtf8(e.what());
        qWarning() << "Auto-save failed:" << reason;
        applyAutoSaveNotice(m_autoSaveState.on_failure(), savePath, reason);
    }
}

void MainWindow::applyAutoSaveNotice(const AutoSaveNotice& notice,
                                     const QString& path,
                                     const QString& reason)
{
    if (m_autoSaveWarning) {
        m_autoSaveWarning->setVisible(notice.warn);
        m_autoSaveWarning->setToolTip(
            notice.warn ? tr("Auto-save to %1 is failing: %2").arg(path, reason)
                        : QString());
    }

    if (!notice.announce) {
        return;
    }

    // Once per run of failures. Every five minutes forever is not a stronger
    // warning, it is one people learn to dismiss without reading. The marker
    // above is what carries the state after this box is closed.
    QMessageBox::warning(
        this, tr("Auto-save is failing"),
        tr("Your work is NOT being auto-saved.\n\n"
           "Auto-save to %1 failed:\n%2\n\n"
           "Any file you saved yourself is untouched and still correct. Save "
           "somewhere you can write to, and this notice will clear on the next "
           "successful auto-save.").arg(path, reason));
}

QString MainWindow::autoSavePath() const
{
    if (m_assembly && m_assembly->document_persistence().autosave) {
        return QString::fromStdString(
            m_assembly->document_persistence().autosave->canonical_path());
    }
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
        return info.absolutePath() + "/" + info.completeBaseName() +
            ".autosave." + m_autoSaveIdentity + ".chiplet";
    }

    // Otherwise use the app data location
    return dataPath + "/autosave." + m_autoSaveIdentity + ".chiplet";
}

bool MainWindow::checkpointRecovery()
{
    if (!m_assembly || m_installingAssembly) return true;
    if (m_recoveryTimer) m_recoveryTimer->stop();
    QElapsedTimer timer;
    timer.start();
    try {
        const QString directory = QStandardPaths::writableLocation(
            QStandardPaths::AppDataLocation) + "/recovery";
        if (directory == "/recovery" || !QDir().mkpath(directory)) {
            throw std::runtime_error("The application recovery directory is unavailable.");
        }
        ChipletFormat format;
        const auto payload = format.encode_recovery(*m_assembly);
        if (m_recoveryCheckpoint) {
            RecoveryCheckpoint::update_or_recreate(*m_recoveryCheckpoint, payload);
        } else {
            m_recoveryCheckpoint = RecoveryCheckpoint::create(directory.toStdString(), payload);
        }
        m_recoveryFailed = false;
        if (m_recoveryWarning) m_recoveryWarning->setVisible(false);
        emit recoveryCheckpointSaved(QString::fromStdString(m_recoveryCheckpoint->path().string()));
        emit recoveryCheckpointDuration(timer.nsecsElapsed() / 1000, true);
        return true;
    } catch (const std::exception& e) {
        const QString reason = QString::fromUtf8(e.what());
        m_recoveryFailed = true;
        if (m_recoveryWarning) {
            m_recoveryWarning->setText(tr("Recovery copy STALE"));
            m_recoveryWarning->setToolTip(tr("Recent edits are not in the recovery copy: %1. "
                "The open model and any previous checkpoint are retained.").arg(reason));
            m_recoveryWarning->setVisible(true);
        }
        qWarning() << "Recovery checkpoint failed; the open model is retained:" << reason;
        emit recoveryCheckpointFailed(reason);
        emit recoveryCheckpointDuration(timer.nsecsElapsed() / 1000, false);
        return false;
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (m_installingAssembly) {
        event->ignore();
        return;
    }
    const bool checkpointed = checkpointRecovery();
    if (!checkpointed && QMessageBox::question(
            this, tr("Recovery copy is stale"),
            tr("The current state could not be checkpointed. Closing may lose edits "
               "that are only in memory. Close anyway?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
        event->ignore();
        return;
    }
    if (checkpointed) retireRecoveryCheckpoint();
    if (m_autoSaveTimer) m_autoSaveTimer->stop();
    if (m_recoveryTimer) m_recoveryTimer->stop();
    // A QWidget can be shown again after close. Never reuse its now-archived
    // handle: a later edit must allocate a separate active recovery identity.
    m_recoveryCheckpoint.reset();
    QMainWindow::closeEvent(event);
}

void MainWindow::retireRecoveryCheckpoint()
{
    if (!m_recoveryCheckpoint || m_recoveryCheckpoint->closed()) return;
    try {
        RecoveryCheckpoint::mark_closed(*m_recoveryCheckpoint);
    } catch (const std::exception& e) {
        // Leave the active copy discoverable if archival fails. No deletion or
        // hidden success: next startup may offer this retained checkpoint again.
        qWarning() << "Recovery copy retained as pending; could not archive:" << e.what();
    }
}

bool MainWindow::detectAndSelectCells()
{
    if (!m_assembly) {
        return false;
    }

    bool anyChanges = false;
    GDSAnalyzer analyzer;

    for (auto& compPtr : m_assembly->components()) {
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

void MainWindow::positionSceneMiniMap()
{
    if (!m_sceneMiniMap || !m_assemblyView) {
        return;
    }
    const int margin = 12;
    const int x = m_assemblyView->width()  - m_sceneMiniMap->width()  - margin;
    const int y = m_assemblyView->height() - m_sceneMiniMap->height() - margin;
    m_sceneMiniMap->move(qMax(0, x), qMax(0, y));
    m_sceneMiniMap->raise();
}

void MainWindow::updateSceneMiniMapVisibility()
{
    if (!m_sceneMiniMap) {
        return;
    }
    // Shown only when enabled AND there is an assembly with geometry to navigate;
    // an empty scene leaves the corner clean (matches the 2D navigator).
    if (m_sceneMiniMapVisible && m_sceneMiniMap->hasContent()) {
        positionSceneMiniMap();
        m_sceneMiniMap->show();
        m_sceneMiniMap->raise();
    } else {
        m_sceneMiniMap->hide();
    }
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_assemblyView && m_sceneMiniMapVisible &&
        (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
        positionSceneMiniMap();
    }
    return QMainWindow::eventFilter(obj, event);
}

} // namespace chiplet
