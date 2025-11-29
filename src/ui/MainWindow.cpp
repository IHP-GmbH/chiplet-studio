/**
 * MainWindow.cpp - Implementation
 */

#include "MainWindow.h"
#include "HierarchyPanel.h"
#include "PropertiesPanel.h"
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QDockWidget>
#include <QFileDialog>

namespace chiplet {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("Chiplet Studio");
    resize(1280, 800);

    setupMenus();
    setupPanels();
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
    QDockWidget* propertiesDock = new QDockWidget("Properties", this);
    m_propertiesPanel = new PropertiesPanel(propertiesDock);
    propertiesDock->setWidget(m_propertiesPanel);
    addDockWidget(Qt::RightDockWidgetArea, propertiesDock);

    // TODO: Add central 3D view widget
}

void MainWindow::onFileNew()
{
    m_assembly = std::make_unique<Assembly>();
    m_assembly->setName("Untitled");
}

void MainWindow::onFileOpen()
{
    QString path = QFileDialog::getOpenFileName(
        this,
        "Open Assembly",
        QString(),
        "Chiplet Files (*.chiplet);;All Files (*)"
    );

    if (!path.isEmpty()) {
        // TODO: Load assembly from file
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

} // namespace chiplet
