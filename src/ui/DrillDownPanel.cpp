/**
 * DrillDownPanel.cpp - Implementation
 */

#include "DrillDownPanel.h"
#include "view2d/KLayout2DView.h"

#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QToolButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSplitter>

namespace chiplet {

DrillDownPanel::DrillDownPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
}

DrillDownPanel::~DrillDownPanel() = default;

void DrillDownPanel::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // -- Nav bar --
    auto* navBar = new QWidget(this);
    navBar->setStyleSheet("background-color: #353535; border-bottom: 1px solid #555;");
    auto* navLayout = new QHBoxLayout(navBar);
    navLayout->setContentsMargins(4, 2, 4, 2);
    navLayout->setSpacing(6);

    m_backButton = new QPushButton("<- Back", navBar);
    m_backButton->setToolTip("Return to 3D view");
    m_backButton->setFixedWidth(70);
    navLayout->addWidget(m_backButton);

    m_contextLabel = new QLabel(navBar);
    m_contextLabel->setStyleSheet("color: #ccc; font-weight: bold;");
    navLayout->addWidget(m_contextLabel);

    navLayout->addStretch();

    auto* cellLabel = new QLabel("Cell:", navBar);
    cellLabel->setStyleSheet("color: #aaa;");
    navLayout->addWidget(cellLabel);

    m_cellCombo = new QComboBox(navBar);
    m_cellCombo->setMinimumWidth(120);
    m_cellCombo->setToolTip("Select cell to display");
    navLayout->addWidget(m_cellCombo);

    m_layerToggle = new QToolButton(navBar);
    m_layerToggle->setText("Layers");
    m_layerToggle->setCheckable(true);
    m_layerToggle->setChecked(true);
    m_layerToggle->setToolTip("Toggle layer control panel");
    navLayout->addWidget(m_layerToggle);

    m_hierToggle = new QToolButton(navBar);
    m_hierToggle->setText("Hierarchy");
    m_hierToggle->setCheckable(true);
    m_hierToggle->setChecked(false);
    m_hierToggle->setToolTip("Toggle hierarchy control panel");
    navLayout->addWidget(m_hierToggle);

    mainLayout->addWidget(navBar);

    // -- Central area with splitter --
    m_splitter = new QSplitter(Qt::Horizontal, this);

    // Layer panel container (left sidebar)
    m_layerContainer = new QWidget(m_splitter);
    auto* layerLayout = new QVBoxLayout(m_layerContainer);
    layerLayout->setContentsMargins(0, 0, 0, 0);
    auto* layerHeader = new QLabel("Layers", m_layerContainer);
    layerHeader->setStyleSheet(
        "background-color: #404040; color: #ccc; padding: 2px 6px; font-weight: bold;");
    layerLayout->addWidget(layerHeader);
    // KLayout's layer_control_frame() will be reparented here after loadLayout()

    m_splitter->addWidget(m_layerContainer);

    // KLayout 2D View (center)
    m_view2d = new KLayout2DView(m_splitter);
    m_splitter->addWidget(m_view2d);

    // Hierarchy panel container (right sidebar)
    m_hierContainer = new QWidget(m_splitter);
    auto* hierLayout = new QVBoxLayout(m_hierContainer);
    hierLayout->setContentsMargins(0, 0, 0, 0);
    auto* hierHeader = new QLabel("Cell Hierarchy", m_hierContainer);
    hierHeader->setStyleSheet(
        "background-color: #404040; color: #ccc; padding: 2px 6px; font-weight: bold;");
    hierLayout->addWidget(hierHeader);
    // KLayout's hierarchy_control_frame() will be reparented here after loadLayout()

    m_splitter->addWidget(m_hierContainer);

    // Set splitter proportions: layer(1) : view(4) : hier(1)
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 4);
    m_splitter->setStretchFactor(2, 1);

    // Initial visibility: layers shown, hierarchy hidden
    m_hierContainer->setVisible(false);

    mainLayout->addWidget(m_splitter, 1);

    // -- Status bar --
    auto* statusBar = new QWidget(this);
    statusBar->setStyleSheet("background-color: #353535; border-top: 1px solid #555;");
    auto* statusLayout = new QHBoxLayout(statusBar);
    statusLayout->setContentsMargins(6, 2, 6, 2);

    m_posLabel = new QLabel("x: --  y: --", statusBar);
    m_posLabel->setStyleSheet("color: #aaa;");
    statusLayout->addWidget(m_posLabel);

    statusLayout->addStretch();

    m_cellLabel = new QLabel(statusBar);
    m_cellLabel->setStyleSheet("color: #aaa;");
    statusLayout->addWidget(m_cellLabel);

    mainLayout->addWidget(statusBar);

    // -- Connect signals --
    connect(m_backButton, &QPushButton::clicked,
            this, &DrillDownPanel::backRequested);

    connect(m_cellCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DrillDownPanel::onCellComboChanged);

    connect(m_layerToggle, &QToolButton::toggled,
            this, &DrillDownPanel::setLayerPanelVisible);

    connect(m_hierToggle, &QToolButton::toggled,
            this, &DrillDownPanel::setHierarchyPanelVisible);

    connect(m_view2d, &KLayout2DView::positionChanged,
            this, &DrillDownPanel::onPositionChanged);

    connect(m_view2d, &KLayout2DView::layoutChanged,
            this, &DrillDownPanel::onLayoutChanged);

    connect(m_view2d, &KLayout2DView::cellChanged,
            this, [this](const QString& cellName) {
                m_cellLabel->setText("Cell: " + cellName);
            });
}

void DrillDownPanel::setContext(const QString& componentId,
                                const QString& componentName,
                                const QString& technologyName)
{
    m_componentId = componentId;

    QString label = "Component: " + componentName;
    if (!technologyName.isEmpty()) {
        label += " (" + technologyName + ")";
    }
    m_contextLabel->setText(label);

    updateSidePanels();
    populateCellCombo();
}

void DrillDownPanel::clearContext()
{
    m_componentId.clear();
    m_contextLabel->setText("");
    m_cellCombo->clear();
    m_cellLabel->setText("");
    m_posLabel->setText("x: --  y: --");
}

bool DrillDownPanel::isLayerPanelVisible() const
{
    return m_layerPanelVisible;
}

bool DrillDownPanel::isHierarchyPanelVisible() const
{
    return m_hierPanelVisible;
}

void DrillDownPanel::setLayerPanelVisible(bool visible)
{
    m_layerPanelVisible = visible;
    m_layerContainer->setVisible(visible);
    if (m_layerToggle->isChecked() != visible) {
        m_layerToggle->setChecked(visible);
    }
}

void DrillDownPanel::setHierarchyPanelVisible(bool visible)
{
    m_hierPanelVisible = visible;
    m_hierContainer->setVisible(visible);
    if (m_hierToggle->isChecked() != visible) {
        m_hierToggle->setChecked(visible);
    }
}

void DrillDownPanel::onCellComboChanged(int index)
{
    if (m_blockCellCombo || index < 0) {
        return;
    }
    QString cellName = m_cellCombo->currentText();
    if (!cellName.isEmpty()) {
        m_view2d->setCurrentCell(cellName);
        m_cellLabel->setText("Cell: " + cellName);
        emit cellSelected(cellName);
    }
}

void DrillDownPanel::onPositionChanged(double x, double y)
{
    m_posLabel->setText(QString("x: %1  y: %2")
        .arg(x, 0, 'f', 2)
        .arg(y, 0, 'f', 2));
}

void DrillDownPanel::onLayoutChanged(bool hasLayout)
{
    if (hasLayout) {
        updateSidePanels();
        populateCellCombo();
    } else {
        m_cellCombo->clear();
        m_cellLabel->setText("");
    }
}

void DrillDownPanel::updateSidePanels()
{
    // Reparent KLayout's layer control frame into our container
    QWidget* layerFrame = m_view2d->layerControlFrame();
    if (layerFrame) {
        QVBoxLayout* layerLayout = qobject_cast<QVBoxLayout*>(m_layerContainer->layout());
        if (layerLayout) {
            // Remove any previously reparented frame (index 1+)
            while (layerLayout->count() > 1) {
                QLayoutItem* item = layerLayout->takeAt(1);
                if (item->widget()) {
                    item->widget()->setParent(nullptr);
                }
                delete item;
            }
            layerFrame->setParent(m_layerContainer);
            layerLayout->addWidget(layerFrame);
            layerFrame->show();
        }
    }

    // Reparent KLayout's hierarchy control frame into our container
    QWidget* hierFrame = m_view2d->hierarchyControlFrame();
    if (hierFrame) {
        QVBoxLayout* hierLayout = qobject_cast<QVBoxLayout*>(m_hierContainer->layout());
        if (hierLayout) {
            while (hierLayout->count() > 1) {
                QLayoutItem* item = hierLayout->takeAt(1);
                if (item->widget()) {
                    item->widget()->setParent(nullptr);
                }
                delete item;
            }
            hierFrame->setParent(m_hierContainer);
            hierLayout->addWidget(hierFrame);
            hierFrame->show();
        }
    }
}

void DrillDownPanel::populateCellCombo()
{
    m_blockCellCombo = true;
    m_cellCombo->clear();

    QStringList cells = m_view2d->cellNames();
    if (!cells.isEmpty()) {
        m_cellCombo->addItems(cells);

        // Set current cell in combo
        QString current = m_view2d->currentCellName();
        if (!current.isEmpty()) {
            int idx = m_cellCombo->findText(current);
            if (idx >= 0) {
                m_cellCombo->setCurrentIndex(idx);
            }
            m_cellLabel->setText("Cell: " + current);
        }
    }

    m_blockCellCombo = false;
}

} // namespace chiplet
