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
class QCheckBox;
class QPushButton;
class QButtonGroup;
class QDoubleSpinBox;
class QProgressDialog;
class QTimer;
class QStatusBar;

class QDockWidget;

namespace chiplet {

class HierarchyPanel;
class PropertiesPanel;
class AssemblyView;
class KLayout2DView;
class DrillDownPanel;
class ScriptConsole;
class ScriptEngine;

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
    void onFileSave();
    void onFileNew();

    // Clip plane controls
    void onClipToggle(bool enabled);
    void onClipAxisChanged(int axis);
    void onClipPositionChanged(int value);
    void onClipFlip();
    void updateClipPositionLabel();

    // 2D drill-down
    void onComponentDrillDown(const QString& componentId);

    // Async loading
    void onAssemblyLoadFinished();
    void onLoadCanceled();

    // Auto-save
    void onAutoSave();

private:
    void setupMenus();
    void setupPanels();
    void setupClipToolbar();
    void setupSnapToolbar();
    void setupRenderModeToolbar();
    void setupScriptConsole();

    std::unique_ptr<Assembly> m_assembly;
    HierarchyPanel* m_hierarchyPanel = nullptr;
    PropertiesPanel* m_propertiesPanel = nullptr;
    AssemblyView* m_assemblyView = nullptr;

    // 2D view dock
    DrillDownPanel* m_drillDownPanel = nullptr;
    KLayout2DView* m_klayout2DView = nullptr;  // convenience ptr, owned by DrillDownPanel
    QDockWidget* m_klayout2DDock = nullptr;
    QDockWidget* m_propertiesDock = nullptr;

    // Python scripting
    std::unique_ptr<ScriptEngine> m_scriptEngine;
    ScriptConsole* m_scriptConsole = nullptr;
    QDockWidget* m_scriptConsoleDock = nullptr;

    // Clip plane toolbar widgets
    QToolBar* m_clipToolbar = nullptr;
    QCheckBox* m_clipEnable = nullptr;
    QButtonGroup* m_axisGroup = nullptr;
    QSlider* m_clipSlider = nullptr;
    QLabel* m_clipPosLabel = nullptr;
    QPushButton* m_flipButton = nullptr;

    // Command system
    std::unique_ptr<CommandProcessor> m_commandProcessor;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;

    // Snapping state
    QCheckBox* m_snapEnable = nullptr;
    QDoubleSpinBox* m_gridSizeSpinBox = nullptr;
    bool m_snapEnabled = false;
    double m_gridSize = 10.0;  // Default: 10um

    // Render mode toolbar
    QButtonGroup* m_renderModeGroup = nullptr;
    QDoubleSpinBox* m_zOffsetSpinBox = nullptr;

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
    QString autoSavePath() const;

    /**
     * Detect GDS cells for components that need them.
     * Shows cell selection dialog if needed.
     * @return true if user made selections (file should be saved)
     */
    bool detectAndSelectCells();
};

} // namespace chiplet

#endif // CHIPLET_UI_MAINWINDOW_H
