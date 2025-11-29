/**
 * MainWindow.h - Main application window
 */

#ifndef CHIPLET_UI_MAINWINDOW_H
#define CHIPLET_UI_MAINWINDOW_H

#include <QMainWindow>
#include <memory>
#include "core/Assembly.h"

namespace chiplet {

class HierarchyPanel;
class PropertiesPanel;

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

private:
    void setupMenus();
    void setupPanels();

    std::unique_ptr<Assembly> m_assembly;
    HierarchyPanel* m_hierarchyPanel = nullptr;
    PropertiesPanel* m_propertiesPanel = nullptr;
};

} // namespace chiplet

#endif // CHIPLET_UI_MAINWINDOW_H
