/**
 * MainWindow.cpp - Implementation
 */

#include "MainWindow.h"
#include "HierarchyPanel.h"
#include "PropertiesPanel.h"
#include "view3d/AssemblyView.h"
#include "formats/ChipletFormat.h"
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QDockWidget>
#include <QFileDialog>
#include <QMessageBox>

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

    // Central 3D view widget
    m_assemblyView = new AssemblyView(this);
    setCentralWidget(m_assemblyView);

    // Connect signals
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
}

void MainWindow::onFileNew()
{
    m_assembly = std::make_unique<Assembly>();
    m_assembly->set_name("Untitled");
    m_assemblyView->setAssembly(m_assembly.get());
    m_propertiesPanel->setComponent(nullptr);
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
            m_propertiesPanel->setComponent(nullptr);
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

} // namespace chiplet
