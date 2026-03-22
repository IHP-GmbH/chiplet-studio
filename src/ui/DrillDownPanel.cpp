/**
 * DrillDownPanel.cpp - Implementation
 */

#include "DrillDownPanel.h"
#include "view2d/KLayout2DView.h"
#include "view2d/CellComponentMapper.h"

#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QToolButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSplitter>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QLineEdit>
#include <QMenu>
#include <QFile>

namespace chiplet {

DrillDownPanel::DrillDownPanel(QWidget* parent)
    : QWidget(parent)
    , m_cellMapper(std::make_unique<CellComponentMapper>())
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
    navBar->setStyleSheet(
        "QWidget { background-color: #353535; border-bottom: 1px solid #555; }"
        "QLabel { color: #ccc; border: none; }"
        "QPushButton { color: #ddd; background-color: #4a4a4a; border: 1px solid #666; "
        "  border-radius: 3px; padding: 2px 8px; }"
        "QPushButton:hover { background-color: #5a5a5a; }"
        "QPushButton:pressed { background-color: #3a3a3a; }"
        "QComboBox { color: #ddd; background-color: #4a4a4a; border: 1px solid #666; "
        "  border-radius: 3px; padding: 2px 6px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox::down-arrow { image: none; border-left: 4px solid transparent; "
        "  border-right: 4px solid transparent; border-top: 5px solid #ccc; }"
        "QComboBox QAbstractItemView { color: #ddd; background-color: #4a4a4a; "
        "  selection-background-color: #3daee9; selection-color: #fff; }"
        "QToolButton { color: #ddd; background-color: #4a4a4a; border: 1px solid #666; "
        "  border-radius: 3px; padding: 2px 8px; }"
        "QToolButton:hover { background-color: #5a5a5a; }"
        "QToolButton:checked { background-color: #3daee9; color: #fff; border-color: #2d9ed9; }"
    );
    auto* navLayout = new QHBoxLayout(navBar);
    navLayout->setContentsMargins(4, 2, 4, 2);
    navLayout->setSpacing(6);

    m_backButton = new QPushButton("<- Back", navBar);
    m_backButton->setToolTip("Return to 3D view");
    m_backButton->setFixedWidth(70);
    navLayout->addWidget(m_backButton);

    m_contextLabel = new QLabel(navBar);
    m_contextLabel->setStyleSheet("font-weight: bold;");
    navLayout->addWidget(m_contextLabel);

    navLayout->addStretch();

    m_cellNavLabel = new QLabel("Cell:", navBar);
    navLayout->addWidget(m_cellNavLabel);

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

    // Layer panel container (left sidebar) with custom layer tree
    m_layerContainer = new QWidget(m_splitter);
    auto* layerLayout = new QVBoxLayout(m_layerContainer);
    layerLayout->setContentsMargins(0, 0, 0, 0);
    layerLayout->setSpacing(0);
    auto* layerHeader = new QLabel("Layers", m_layerContainer);
    layerHeader->setStyleSheet(
        "background-color: #404040; color: #ccc; padding: 2px 6px; font-weight: bold;");
    layerLayout->addWidget(layerHeader);

    // Layer search filter
    m_layerFilter = new QLineEdit(m_layerContainer);
    m_layerFilter->setPlaceholderText("Filter layers...");
    m_layerFilter->setClearButtonEnabled(true);
    m_layerFilter->setStyleSheet(
        "QLineEdit { background-color: #3a3a3a; color: #ddd; border: 1px solid #555; "
        "  border-radius: 3px; padding: 3px 6px; }"
        "QLineEdit:focus { border-color: #3daee9; }");
    layerLayout->addWidget(m_layerFilter);

    // Custom layer tree widget (replaces KLayout's native layer_control_frame)
    m_layerTree = new QTreeWidget(m_layerContainer);
    m_layerTree->setHeaderHidden(true);
    m_layerTree->setRootIsDecorated(false);
    m_layerTree->setIndentation(0);
    m_layerTree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_layerTree->setStyleSheet(
        "QTreeWidget { background-color: #2a2a2a; color: #ddd; border: none; }"
        "QTreeWidget::item { padding: 2px 4px; }"
        "QTreeWidget::item:selected { background-color: #3daee9; color: #fff; }"
        "QTreeWidget::item:hover { background-color: #404040; }"
        "QTreeWidget::indicator { width: 14px; height: 14px; }"
        "QTreeWidget::indicator:checked { background-color: #3daee9; border: 1px solid #2d9ed9; border-radius: 2px; }"
        "QTreeWidget::indicator:unchecked { background-color: #555; border: 1px solid #666; border-radius: 2px; }"
    );
    layerLayout->addWidget(m_layerTree);

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
    statusBar->setStyleSheet(
        "QWidget { background-color: #353535; border-top: 1px solid #555; }"
        "QLabel { color: #aaa; border: none; }");
    auto* statusLayout = new QHBoxLayout(statusBar);
    statusLayout->setContentsMargins(6, 2, 6, 2);

    m_posLabel = new QLabel("x: --  y: --", statusBar);
    statusLayout->addWidget(m_posLabel);

    statusLayout->addStretch();

    m_cellLabel = new QLabel(statusBar);
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

    // Layer tree: checkbox toggling
    connect(m_layerTree, &QTreeWidget::itemChanged,
            this, &DrillDownPanel::onLayerItemChanged);

    // Layer tree: right-click context menu
    connect(m_layerTree, &QTreeWidget::customContextMenuRequested,
            this, &DrillDownPanel::onLayerContextMenu);

    // Layer filter
    connect(m_layerFilter, &QLineEdit::textChanged,
            this, &DrillDownPanel::onLayerFilterChanged);

    // Cell navigation in 2D -> component mapping for assembly mode
    connect(m_view2d, &KLayout2DView::cellNavigated,
            this, [this](const QString& cellName) {
                if (m_panelMode != PanelMode::Assembly || !m_cellMapper) return;
                QString compId = m_cellMapper->componentForCell(cellName);
                if (!compId.isEmpty()) {
                    emit componentNavigated(compId);
                }
            });
}

void DrillDownPanel::setAssemblyGds(const QString& gdsPath, const QString& lypPath,
                                     const Assembly& assembly)
{
    m_assemblyGdsPath = gdsPath;
    m_assemblyLypPath = lypPath;

    if (!QFile::exists(gdsPath)) {
        qWarning("DrillDownPanel: Assembly GDS not found: %s", qPrintable(gdsPath));
        return;
    }

    if (m_view2d->loadLayout(gdsPath, lypPath)) {
        m_cellMapper->build(assembly, m_view2d->cellNames());
        m_panelMode = PanelMode::Assembly;
        m_backButton->setVisible(false);
        m_cellNavLabel->setVisible(false);
        m_cellCombo->setVisible(false);
        m_contextLabel->setText("Assembly");
        populateLayerList();
    }
}

void DrillDownPanel::returnToAssembly()
{
    if (m_assemblyGdsPath.isEmpty()) {
        return;
    }

    m_view2d->loadLayout(m_assemblyGdsPath, m_assemblyLypPath);
    m_panelMode = PanelMode::Assembly;
    m_backButton->setVisible(false);
    m_cellNavLabel->setVisible(false);
    m_cellCombo->setVisible(false);
    m_contextLabel->setText("Assembly");
    m_componentId.clear();
    populateLayerList();
}

void DrillDownPanel::setContext(const QString& componentId,
                                const QString& componentName,
                                const QString& technologyName)
{
    m_componentId = componentId;
    m_panelMode = PanelMode::DrillDown;
    m_backButton->setVisible(true);
    m_cellNavLabel->setVisible(true);
    m_cellCombo->setVisible(true);

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
    m_cellCombo->clear();
    m_cellLabel->setText("");
    m_posLabel->setText("x: --  y: --");
    m_layerFilter->clear();
    m_layerTree->clear();

    // If we have an assembly GDS, return to assembly mode instead of going empty
    if (!m_assemblyGdsPath.isEmpty()) {
        returnToAssembly();
    } else {
        m_panelMode = PanelMode::Empty;
        m_contextLabel->setText("");
        m_backButton->setVisible(true);
    }
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
        m_layerTree->clear();
    }
}

void DrillDownPanel::onLayerItemChanged(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column);
    if (m_blockLayerSync || !item) {
        return;
    }

    int layerIndex = item->data(0, Qt::UserRole).toInt();
    bool visible = (item->checkState(0) == Qt::Checked);
    m_view2d->setLayerVisible(layerIndex, visible);
}

void DrillDownPanel::onLayerContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = m_layerTree->itemAt(pos);

    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background-color: #3a3a3a; color: #ddd; border: 1px solid #555; }"
        "QMenu::item { padding: 4px 20px; }"
        "QMenu::item:selected { background-color: #3daee9; color: #fff; }"
        "QMenu::separator { height: 1px; background: #555; margin: 2px 8px; }"
    );

    if (item) {
        bool isVisible = (item->checkState(0) == Qt::Checked);
        int layerIndex = item->data(0, Qt::UserRole).toInt();

        QAction* toggleAction = menu.addAction(isVisible ? "Hide" : "Show");
        connect(toggleAction, &QAction::triggered, this, [this, item, isVisible]() {
            item->setCheckState(0, isVisible ? Qt::Unchecked : Qt::Checked);
        });

        QAction* showOnlyAction = menu.addAction("Show Only This");
        connect(showOnlyAction, &QAction::triggered, this, [this, layerIndex]() {
            m_blockLayerSync = true;
            m_view2d->setAllLayersVisible(false);
            m_view2d->setLayerVisible(layerIndex, true);
            // Update all checkboxes
            for (int i = 0; i < m_layerTree->topLevelItemCount(); ++i) {
                QTreeWidgetItem* it = m_layerTree->topLevelItem(i);
                int idx = it->data(0, Qt::UserRole).toInt();
                it->setCheckState(0, (idx == layerIndex) ? Qt::Checked : Qt::Unchecked);
            }
            m_blockLayerSync = false;
        });

        menu.addSeparator();
    }

    QAction* showAllAction = menu.addAction("Show All");
    connect(showAllAction, &QAction::triggered, this, [this]() {
        m_blockLayerSync = true;
        m_view2d->setAllLayersVisible(true);
        for (int i = 0; i < m_layerTree->topLevelItemCount(); ++i) {
            m_layerTree->topLevelItem(i)->setCheckState(0, Qt::Checked);
        }
        m_blockLayerSync = false;
    });

    QAction* hideAllAction = menu.addAction("Hide All");
    connect(hideAllAction, &QAction::triggered, this, [this]() {
        m_blockLayerSync = true;
        m_view2d->setAllLayersVisible(false);
        for (int i = 0; i < m_layerTree->topLevelItemCount(); ++i) {
            m_layerTree->topLevelItem(i)->setCheckState(0, Qt::Unchecked);
        }
        m_blockLayerSync = false;
    });

    menu.exec(m_layerTree->viewport()->mapToGlobal(pos));
}

void DrillDownPanel::onLayerFilterChanged(const QString& text)
{
    for (int i = 0; i < m_layerTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = m_layerTree->topLevelItem(i);
        bool match = text.isEmpty() ||
                     item->text(0).contains(text, Qt::CaseInsensitive);
        item->setHidden(!match);
    }
}

void DrillDownPanel::updateSidePanels()
{
    populateLayerList();

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
            hierFrame->setStyleSheet(
                "QWidget { background-color: #f0f0f0; color: #1a1a1a; }"
                "QTreeView, QListView, QTableView { "
                "  background-color: #ffffff; color: #1a1a1a; "
                "  selection-background-color: #3daee9; selection-color: #ffffff; }"
                "QHeaderView::section { background-color: #e0e0e0; color: #1a1a1a; "
                "  border: 1px solid #c0c0c0; padding: 2px; }"
                "QScrollBar { background-color: #e8e8e8; }");
            hierLayout->addWidget(hierFrame);
            hierFrame->show();
        }
    }
}

void DrillDownPanel::populateLayerList()
{
    m_blockLayerSync = true;
    m_layerTree->clear();

    QVector<LayerInfo> layers = m_view2d->layerInfos();
    for (const LayerInfo& info : layers) {
        auto* item = new QTreeWidgetItem(m_layerTree);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, info.visible ? Qt::Checked : Qt::Unchecked);
        item->setData(0, Qt::UserRole, info.index);

        // Build display text: "layer/datatype - name" or just name
        QString text;
        if (info.layer >= 0 && info.datatype >= 0) {
            text = QString("%1/%2").arg(info.layer).arg(info.datatype);
            if (!info.name.isEmpty() && info.name != text) {
                text += " " + info.name;
            }
        } else {
            text = info.name;
        }
        item->setText(0, text);

        // Set color swatch via decoration role
        QColor color = info.fillColor.isValid() ? info.fillColor :
                       (info.frameColor.isValid() ? info.frameColor : QColor(128, 128, 128));
        QPixmap swatch(14, 14);
        swatch.fill(color);
        item->setIcon(0, QIcon(swatch));

        item->setToolTip(0, QString("Layer %1/%2%3")
            .arg(info.layer).arg(info.datatype)
            .arg(info.name.isEmpty() ? "" : " - " + info.name));
    }

    m_blockLayerSync = false;
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
