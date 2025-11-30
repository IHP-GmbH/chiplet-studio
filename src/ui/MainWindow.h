/**
 * MainWindow.h - Main application window
 */

#ifndef CHIPLET_UI_MAINWINDOW_H
#define CHIPLET_UI_MAINWINDOW_H

#include <QMainWindow>
#include <memory>
#include "core/Assembly.h"

class QToolBar;
class QSlider;
class QLabel;
class QCheckBox;
class QPushButton;
class QButtonGroup;

namespace chiplet {

class HierarchyPanel;
class PropertiesPanel;
class AssemblyView;

/**
 * MainWindow is the main application window.
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

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

private:
    void setupMenus();
    void setupPanels();
    void setupClipToolbar();

    std::unique_ptr<Assembly> m_assembly;
    HierarchyPanel* m_hierarchyPanel = nullptr;
    PropertiesPanel* m_propertiesPanel = nullptr;
    AssemblyView* m_assemblyView = nullptr;

    // Clip plane toolbar widgets
    QToolBar* m_clipToolbar = nullptr;
    QCheckBox* m_clipEnable = nullptr;
    QButtonGroup* m_axisGroup = nullptr;
    QSlider* m_clipSlider = nullptr;
    QLabel* m_clipPosLabel = nullptr;
    QPushButton* m_flipButton = nullptr;
};

} // namespace chiplet

#endif // CHIPLET_UI_MAINWINDOW_H
