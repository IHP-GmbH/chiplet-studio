// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * MainWindow.h - Main application window
 */

#ifndef CHIPLET_UI_MAINWINDOW_H
#define CHIPLET_UI_MAINWINDOW_H

#include <QMainWindow>
#include <QFutureWatcher>
#include <memory>
#include "core/Assembly.h"
#include "core/CommandProcessor.h"

class QToolBar;
class QAction;
class QSlider;
class QLabel;
class QDoubleSpinBox;
class QProgressDialog;
class QTimer;
class QStatusBar;

class QDockWidget;

namespace chiplet {

class HierarchyPanel;
class PropertiesPanel;
class AssemblyView;
class SceneMiniMap;
class KLayout2DView;
class DrillDownPanel;
class ScriptConsole;
class ScriptEngine;
class FlowEngine;
class FlowPanel;
class NetGraphPanel;

/**
 * MainWindow is the main application window.
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

    /**
     * Set a recovered assembly from crash recovery.
     * Called from main() if crash recovery was successful.
     */
    void setRecoveredAssembly(std::unique_ptr<Assembly> assembly);

    /**
     * Open a .chiplet file programmatically (for CLI argument support).
     */
    void openFile(const QString& path);

signals:
    /**
     * Emitted when async loading starts (for testing)
     */
    void loadingStarted(const QString& path);

    /**
     * Emitted when async loading finishes successfully (for testing)
     */
    void loadingFinished();

    /**
     * Emitted when async loading fails (for testing)
     */
    void loadingError(const QString& error);

private slots:
    void onFileOpen();
    void onFileReload();
    void onFileSave();
    void onFileNew();
    void onImportGds();
    void onExportPng();

    // 2D drill-down
    void onComponentDrillDown(const QString& componentId);

    // Async loading
    void onAssemblyLoadFinished();
    void onLoadCanceled();

    // Auto-save
    void onAutoSave();

protected:
    // Keeps the 3D top-down mini-map pinned to a corner of the AssemblyView as
    // it resizes (the mini-map is a child overlay of the central view).
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void setupMenus();
    void setupPanels();
    void setupViewModeToolbar();
    // Enable the Layer-Z spread controls only when the assembly is a single
    // imported die (AssemblyView::layerZSpacingApplicable) and reset them to the
    // 1.0x default; call after every assembly swap.
    void updateLayerZSpacingControls();
    void setupScriptConsole();
    void setupFlowPanel();
    void setupNetGraphPanel();
    void populateFlowEngine();

    std::unique_ptr<Assembly> m_assembly;
    HierarchyPanel* m_hierarchyPanel = nullptr;
    QDockWidget* m_hierarchyDock = nullptr;
    PropertiesPanel* m_propertiesPanel = nullptr;
    AssemblyView* m_assemblyView = nullptr;

    // 3D top-down mini-map / navigator (corner overlay child of m_assemblyView).
    SceneMiniMap* m_sceneMiniMap = nullptr;
    bool m_sceneMiniMapVisible = true;
    void positionSceneMiniMap();
    void updateSceneMiniMapVisibility();

    // 2D view dock
    DrillDownPanel* m_drillDownPanel = nullptr;
    KLayout2DView* m_klayout2DView = nullptr;  // convenience ptr, owned by DrillDownPanel
    QDockWidget* m_klayout2DDock = nullptr;
    QDockWidget* m_propertiesDock = nullptr;

    // Python scripting
    std::unique_ptr<ScriptEngine> m_scriptEngine;
    ScriptConsole* m_scriptConsole = nullptr;
    QDockWidget* m_scriptConsoleDock = nullptr;

    // Flow pipeline
    FlowEngine* m_flowEngine = nullptr;
    FlowPanel* m_flowPanel = nullptr;
    QDockWidget* m_flowPanelDock = nullptr;

    // Net graph
    NetGraphPanel* m_netGraphPanel = nullptr;
    QDockWidget* m_netGraphDock = nullptr;

    // Command system
    std::unique_ptr<CommandProcessor> m_commandProcessor;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;

    // View mode toolbar
    QSlider* m_shapeFilterSlider = nullptr;
    QLabel* m_shapeFilterLabel = nullptr;
    QDoubleSpinBox* m_layerZSpacingSpin = nullptr;  // visualization-only Z exaggeration
    QLabel* m_layerZLabel = nullptr;                 // "Layer Z:" caption (greyed with the spin)
    QAction* m_layerZResetAction = nullptr;          // "Reset Z" (greyed with the spin)

    // Async loading state
    struct LoadResult {
        std::unique_ptr<Assembly> assembly;
        QString error;
    };
    // Using QFuture directly (not via result()) avoids copyability requirement
    QFutureWatcher<LoadResult>* m_loadWatcher = nullptr;
    QProgressDialog* m_loadProgress = nullptr;
    QString m_pendingLoadPath;  // Path being loaded
    bool m_loadCanceled = false;

    // Auto-save state
    QTimer* m_autoSaveTimer = nullptr;
    QString m_currentFilePath;  // Path to current file for auto-save
    static constexpr int AUTO_SAVE_INTERVAL_MS = 5 * 60 * 1000;  // 5 minutes

    void setupAutoSave();
    void initializeCommandProcessor();
    // Detach every view/processor/script binding from the current assembly
    // before it is replaced or freed, and clear the 2D dock. Used by the load,
    // New and crash-recovery paths so they all share the same safe ordering.
    void detachAssemblyFromViews();
    QString autoSavePath() const;
    // Resolve the GDS to show in the 2D panel. If interposerOnly is non-null it
    // is set true when the result is the interposer's own layout used as a
    // fallback (no merged/complete assembly GDS was found), false otherwise.
    QString resolveAssemblyGdsPath(bool* interposerOnly = nullptr) const;
    void loadAssemblyGds();

    /**
     * Detect GDS cells for components that need them.
     * Shows cell selection dialog if needed.
     * @return true if user made selections (file should be saved)
     */
    bool detectAndSelectCells();
};

} // namespace chiplet

#endif // CHIPLET_UI_MAINWINDOW_H
