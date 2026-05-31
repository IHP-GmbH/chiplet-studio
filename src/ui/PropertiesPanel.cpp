/**
 * PropertiesPanel.cpp - Implementation
 */

#include "PropertiesPanel.h"
#include "UnitConverter.h"
#include "core/Assembly.h"
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QComboBox>
#include <QLineEdit>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QMenu>
#include <QPixmap>
#include <QIcon>
#include <QFileInfo>
#include <QPainter>

namespace chiplet {

PropertiesPanel::PropertiesPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
}

PropertiesPanel::~PropertiesPanel() = default;

void PropertiesPanel::setupUI()
{
    QVBoxLayout* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(4, 4, 4, 4);
    rootLayout->setSpacing(4);

    createUnitSelector();
    rootLayout->addLayout(static_cast<QLayout*>(m_unitCombo->parentWidget()->layout()));

    // Scroll area for content
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setFrameShape(QFrame::NoFrame);

    m_contentWidget = new QWidget();
    m_mainLayout = new QVBoxLayout(m_contentWidget);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(2);

    createGroupBoxes();
    m_mainLayout->addStretch();

    m_scrollArea->setWidget(m_contentWidget);
    rootLayout->addWidget(m_scrollArea);
}

void PropertiesPanel::createUnitSelector()
{
    QWidget* unitWidget = new QWidget(this);
    QHBoxLayout* unitLayout = new QHBoxLayout(unitWidget);
    unitLayout->setContentsMargins(0, 0, 0, 4);

    QLabel* unitLabel = new QLabel("Unit:", unitWidget);
    m_unitCombo = new QComboBox(unitWidget);
    m_unitCombo->addItems(UnitConverter::availableUnits());
    m_unitCombo->setCurrentIndex(UnitConverter::indexFromUnit(
        UnitConverter::instance().currentUnit()));
    m_unitCombo->setFixedWidth(60);

    connect(m_unitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PropertiesPanel::onUnitChanged);

    unitLayout->addWidget(unitLabel);
    unitLayout->addWidget(m_unitCombo);
    unitLayout->addStretch();
}

QGroupBox* PropertiesPanel::createCollapsibleGroup(const QString& title)
{
    QGroupBox* group = new QGroupBox(title);
    group->setCheckable(true);
    group->setChecked(true);
    connect(group, &QGroupBox::toggled, this, &PropertiesPanel::onGroupToggled);
    return group;
}

void PropertiesPanel::createGroupBoxes()
{
    // Component Group
    m_componentGroup = createCollapsibleGroup("Component");
    QFormLayout* compLayout = new QFormLayout(m_componentGroup);
    compLayout->setContentsMargins(8, 8, 8, 8);
    compLayout->setSpacing(4);
    addPropertyRow(compLayout, "ID:", m_idLabel);
    addPropertyRow(compLayout, "Type:", m_typeLabel);
    addPropertyRow(compLayout, "Technology:", m_techLabel);
    addPropertyRow(compLayout, "Connection:", m_connectionLabel);
    m_mainLayout->addWidget(m_componentGroup);

    // Position Group
    m_positionGroup = createCollapsibleGroup("Position");
    QFormLayout* posLayout = new QFormLayout(m_positionGroup);
    posLayout->setContentsMargins(8, 8, 8, 8);
    posLayout->setSpacing(4);
    addPropertyRow(posLayout, "X:", m_posXLabel);
    addPropertyRow(posLayout, "Y:", m_posYLabel);
    addPropertyRow(posLayout, "Z:", m_posZLabel);
    addPropertyRow(posLayout, "Rotation Z:", m_rotZLabel);
    m_mainLayout->addWidget(m_positionGroup);

    // Dimensions Group
    m_dimensionsGroup = createCollapsibleGroup("Dimensions");
    QFormLayout* dimLayout = new QFormLayout(m_dimensionsGroup);
    dimLayout->setContentsMargins(8, 8, 8, 8);
    dimLayout->setSpacing(4);
    addPropertyRow(dimLayout, "Width:", m_widthLabel);
    addPropertyRow(dimLayout, "Height:", m_heightLabel);
    addPropertyRow(dimLayout, "Thickness:", m_thicknessLabel);
    m_mainLayout->addWidget(m_dimensionsGroup);

    // Layout Group
    m_layoutGroup = createCollapsibleGroup("Layout");
    QFormLayout* layoutLayout = new QFormLayout(m_layoutGroup);
    layoutLayout->setContentsMargins(8, 8, 8, 8);
    layoutLayout->setSpacing(4);
    addPropertyRow(layoutLayout, "File:", m_layoutPathLabel);
    addPropertyRow(layoutLayout, "Top Cell:", m_topCellLabel);
    m_mainLayout->addWidget(m_layoutGroup);

    // Layers Group
    m_layersGroup = createCollapsibleGroup("Layers (0)");
    QVBoxLayout* layersLayout = new QVBoxLayout(m_layersGroup);
    layersLayout->setContentsMargins(8, 8, 8, 8);

    m_layerFilter = new QLineEdit();
    m_layerFilter->setObjectName("layerFilter");
    m_layerFilter->setPlaceholderText("Filter layers (name or L/D)...");
    m_layerFilter->setClearButtonEnabled(true);
    layersLayout->addWidget(m_layerFilter);

    m_layerTree = new QTreeWidget();
    m_layerTree->setObjectName("layerTree");
    m_layerTree->setHeaderLabels({"Layer", "L/D"});
    m_layerTree->setRootIsDecorated(false);
    m_layerTree->setMaximumHeight(150);
    m_layerTree->header()->setStretchLastSection(false);
    m_layerTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_layerTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_layerTree->setContextMenuPolicy(Qt::CustomContextMenu);
    layersLayout->addWidget(m_layerTree);
    m_mainLayout->addWidget(m_layersGroup);

    connect(m_layerFilter, &QLineEdit::textChanged,
            this, &PropertiesPanel::onLayerFilterChanged);
    connect(m_layerTree, &QTreeWidget::itemChanged,
            this, &PropertiesPanel::onLayerItemChanged);
    connect(m_layerTree, &QTreeWidget::customContextMenuRequested,
            this, &PropertiesPanel::onLayerContextMenu);

    // Array Group (hidden by default)
    m_arrayGroup = createCollapsibleGroup("Array Configuration");
    QFormLayout* arrayLayout = new QFormLayout(m_arrayGroup);
    arrayLayout->setContentsMargins(8, 8, 8, 8);
    arrayLayout->setSpacing(4);
    addPropertyRow(arrayLayout, "Pattern:", m_arrayPatternLabel);
    addPropertyRow(arrayLayout, "Count:", m_arrayCountLabel);
    addPropertyRow(arrayLayout, "Pitch:", m_arrayPitchLabel);
    m_arrayGroup->setVisible(false);
    m_mainLayout->addWidget(m_arrayGroup);

    // Metadata Group
    m_metadataGroup = createCollapsibleGroup("Metadata");
    QVBoxLayout* metaLayout = new QVBoxLayout(m_metadataGroup);
    metaLayout->setContentsMargins(8, 8, 8, 8);
    m_metadataTree = new QTreeWidget();
    m_metadataTree->setHeaderLabels({"Key", "Value"});
    m_metadataTree->setRootIsDecorated(false);
    m_metadataTree->setMaximumHeight(100);
    m_metadataTree->header()->setStretchLastSection(true);
    metaLayout->addWidget(m_metadataTree);
    m_mainLayout->addWidget(m_metadataGroup);
}

void PropertiesPanel::addPropertyRow(QFormLayout* layout, const QString& label, QLabel*& valueLabel)
{
    valueLabel = createValueLabel();
    layout->addRow(label, valueLabel);
}

QLabel* PropertiesPanel::createValueLabel(const QString& text)
{
    QLabel* label = new QLabel(text);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setWordWrap(true);
    return label;
}

void PropertiesPanel::setComponent(const ComponentID& componentId, Assembly* assembly)
{
    m_selectedComponentId = componentId;
    m_assembly = assembly;
    updateDisplay();
}

void PropertiesPanel::setAssembly(Assembly* assembly)
{
    m_assembly = assembly;
}

void PropertiesPanel::setLayerVisibilityResolver(std::function<bool(const QString&, int, int)> resolver)
{
    m_layerVisibilityResolver = std::move(resolver);
}

void PropertiesPanel::clearSelection()
{
    m_selectedComponentId = INVALID_COMPONENT_ID;

    // Reset all labels
    m_idLabel->setText("-");
    m_typeLabel->setText("-");
    m_techLabel->setText("-");
    m_connectionLabel->setText("-");
    m_posXLabel->setText("-");
    m_posYLabel->setText("-");
    m_posZLabel->setText("-");
    m_rotZLabel->setText("-");
    m_widthLabel->setText("-");
    m_heightLabel->setText("-");
    m_thicknessLabel->setText("-");
    m_layoutPathLabel->setText("-");
    m_topCellLabel->setText("-");

    // Clear trees
    m_blockLayerSync = true;
    m_layerTree->clear();
    if (m_layerFilter) {
        m_layerFilter->clear();
    }
    m_layersForComponent = INVALID_COMPONENT_ID;
    m_blockLayerSync = false;
    m_layersGroup->setTitle("Layers (0)");
    m_metadataTree->clear();

    // Hide array group
    m_arrayGroup->setVisible(false);
}

void PropertiesPanel::onUnitChanged(int index)
{
    UnitConverter::instance().setUnit(UnitConverter::unitFromIndex(index));
    updateDisplay();
}

void PropertiesPanel::onGroupToggled(bool checked)
{
    QGroupBox* group = qobject_cast<QGroupBox*>(sender());
    if (group) {
        // Find the content widget and toggle visibility
        QLayout* layout = group->layout();
        if (layout) {
            for (int i = 0; i < layout->count(); ++i) {
                QWidget* w = layout->itemAt(i)->widget();
                if (w) {
                    w->setVisible(checked);
                }
            }
        }
    }
}

void PropertiesPanel::updateDisplay()
{
    if (!m_assembly || !is_valid_id(m_selectedComponentId)) {
        clearSelection();
        return;
    }

    Component* comp = m_assembly->component(m_selectedComponentId);
    if (!comp) {
        clearSelection();
        return;
    }

    updateComponentGroup();
    updatePositionGroup();
    updateDimensionsGroup();
    updateLayoutGroup();
    updateLayersGroup();
    updateArrayGroup();
    updateMetadataGroup();
}

void PropertiesPanel::updateComponentGroup()
{
    Component* comp = m_assembly->component(m_selectedComponentId);
    if (!comp) return;

    m_idLabel->setText(QString::fromStdString(comp->id()));
    m_typeLabel->setText(typeToString(comp->type()));
    m_techLabel->setText(comp->technology().empty() ?
                         "-" : QString::fromStdString(comp->technology()));
    m_connectionLabel->setText(comp->connection().empty() ?
                         "-" : QString::fromStdString(comp->connection()));
}

void PropertiesPanel::updatePositionGroup()
{
    Component* comp = m_assembly->component(m_selectedComponentId);
    if (!comp) return;

    const Position3D& pos = comp->position();
    m_posXLabel->setText(formatValue(pos.x));
    m_posYLabel->setText(formatValue(pos.y));
    QString zText = formatValue(pos.z);
    if (!comp->connection().empty()) {
        zText += " (auto)";
    }
    m_posZLabel->setText(zText);
    m_rotZLabel->setText(formatDegrees(comp->rotation().z));
}

void PropertiesPanel::updateDimensionsGroup()
{
    Component* comp = m_assembly->component(m_selectedComponentId);
    if (!comp) return;

    const Dimensions3D& dims = comp->dimensions();
    m_widthLabel->setText(formatValue(dims.width));
    m_heightLabel->setText(formatValue(dims.height));
    m_thicknessLabel->setText(formatValue(dims.thickness));
}

void PropertiesPanel::updateLayoutGroup()
{
    Component* comp = m_assembly->component(m_selectedComponentId);
    if (!comp) return;

    const std::string& layoutPath = comp->layout_path();
    if (layoutPath.empty()) {
        m_layoutPathLabel->setText("-");
        m_topCellLabel->setText("-");
    } else {
        // Show just filename, not full path
        QFileInfo fi(QString::fromStdString(layoutPath));
        m_layoutPathLabel->setText(fi.fileName());
        m_layoutPathLabel->setToolTip(QString::fromStdString(layoutPath));

        m_topCellLabel->setText(comp->top_cell().empty() ?
                                "-" : QString::fromStdString(comp->top_cell()));
    }
}

namespace {

// Generate KLayout-style pattern swatch (16x16 scaled to swatchSize)
QPixmap createPatternSwatch(const LayerStyle& layer, int swatchSize = 16)
{
    QPixmap pix(swatchSize, swatchSize);
    QColor fillColor(layer.fill_color.r, layer.fill_color.g,
                     layer.fill_color.b, layer.fill_color.a);
    QColor bgColor = Qt::white;

    // Solid fill for C0 or no pattern
    if (!layer.has_pattern()) {
        pix.fill(fillColor);
        return pix;
    }

    // Parse pattern ID (e.g., "C27", "I3")
    QString patternId = QString::fromStdString(layer.dither_pattern);
    bool inverted = patternId.startsWith("I");
    int patternNum = patternId.mid(1).toInt();

    // Fill with background
    pix.fill(bgColor);
    QPainter painter(&pix);
    painter.setPen(Qt::NoPen);

    // Generate pattern based on ID
    // KLayout patterns: C1=horiz lines, C2=vert lines, C3=diag LR, C4=diag RL,
    // C5=grid, C6=crosshatch, C7=dots, C8=checker 1px, C9=checker 2px, etc.
    auto setPixel = [&](int x, int y, bool on) {
        if (inverted) on = !on;
        if (on) {
            painter.fillRect(x, y, 1, 1, fillColor);
        }
    };

    for (int y = 0; y < swatchSize; ++y) {
        for (int x = 0; x < swatchSize; ++x) {
            bool on = false;

            switch (patternNum) {
                case 0:  // Solid
                    on = true;
                    break;
                case 1:  // Horizontal lines, spacing 2
                    on = (y % 2) == 0;
                    break;
                case 2:  // Vertical lines, spacing 2
                    on = (x % 2) == 0;
                    break;
                case 3:  // Diagonal LR, spacing 2
                    on = ((x + y) % 2) == 0;
                    break;
                case 4:  // Diagonal RL, spacing 2
                    on = ((x - y + swatchSize) % 2) == 0;
                    break;
                case 5:  // Grid, spacing 4
                    on = (x % 4 == 0) || (y % 4 == 0);
                    break;
                case 6:  // Crosshatch, spacing 4
                    on = ((x + y) % 4 == 0) || ((x - y + swatchSize) % 4 == 0);
                    break;
                case 7:  // Dots, 4x4
                    on = (x % 4 == 0) && (y % 4 == 0);
                    break;
                case 8:  // Checkerboard 1px
                    on = ((x + y) % 2) == 0;
                    break;
                case 9:  // Checkerboard 2px
                    on = ((x / 2 + y / 2) % 2) == 0;
                    break;
                case 10: // Horizontal lines, spacing 4
                    on = (y % 4) == 0;
                    break;
                case 11: // Vertical lines, spacing 4
                    on = (x % 4) == 0;
                    break;
                case 12: // Diagonal LR, spacing 4
                    on = ((x + y) % 4) == 0;
                    break;
                case 13: // Diagonal RL, spacing 4
                    on = ((x - y + swatchSize) % 4) == 0;
                    break;
                case 14: // Sparse stipple
                    on = ((x + y * 2) % 4) == 0;
                    break;
                case 15: // Dense stipple
                    on = !((x + y * 2) % 4 == 0);
                    break;
                default:
                    // For higher patterns, use modular patterns
                    {
                        int variant = (patternNum - 16) % 8;
                        int spacing = 8;
                        switch (variant) {
                            case 0: on = (y % spacing) == 0; break;
                            case 1: on = (x % spacing) == 0; break;
                            case 2: on = ((x + y) % spacing) == 0; break;
                            case 3: on = ((x - y + swatchSize) % spacing) == 0; break;
                            case 4: on = (x % 2 == 0) && (y % 2 == 0); break;
                            case 5: on = (x % spacing == 0) && (y % spacing == 0); break;
                            case 6: on = ((x / 4 + y / 4) % 2) == 0; break;
                            case 7: on = ((x / 8 + y / 8) % 2) == 0; break;
                        }
                    }
                    break;
            }

            setPixel(x, y, on);
        }
    }

    painter.end();
    return pix;
}

} // anonymous namespace

void PropertiesPanel::updateLayersGroup()
{
    // Layers depend only on the component's technology, not on the display unit.
    // Skip the (hundreds-of-rows) rebuild when the selected component is unchanged
    // so the user's show/hide checkbox states survive unit-change refreshes.
    if (m_selectedComponentId == m_layersForComponent &&
        m_layerTree->topLevelItemCount() > 0) {
        return;
    }

    m_blockLayerSync = true;
    m_layerTree->clear();
    if (m_layerFilter) {
        m_layerFilter->clear();
    }
    loadLayerProperties();
    m_layersForComponent = m_selectedComponentId;

    if (m_layerProps.layers().empty()) {
        m_layersGroup->setTitle("Layers (0)");
        m_blockLayerSync = false;
        return;
    }

    m_layersGroup->setTitle(QString("Layers (%1)").arg(m_layerProps.layer_count()));

    for (const auto& layer : m_layerProps.layers()) {
        QTreeWidgetItem* item = new QTreeWidgetItem();

        // Pattern swatch as icon (renders pattern if present)
        QPixmap pix = createPatternSwatch(layer, 16);
        item->setIcon(0, QIcon(pix));

        // Layer name with pattern indicator
        QString name = QString::fromStdString(layer.name);
        if (layer.has_pattern()) {
            name += QString(" [%1]").arg(QString::fromStdString(layer.dither_pattern));
        }
        item->setText(0, name);
        item->setText(1, QString("%1/%2").arg(layer.key.layer).arg(layer.key.datatype));

        // Per-layer show/hide checkbox; the layer/datatype is stashed so the
        // toggle slot can address the matching LayerMesh in the 3D view. Initial
        // state mirrors the 3D view's current visibility (via the resolver), so
        // it stays correct when revisiting a component with hidden layers.
        bool visible = m_layerVisibilityResolver
            ? m_layerVisibilityResolver(QString::fromStdString(m_selectedComponentId),
                                        layer.key.layer, layer.key.datatype)
            : true;
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, visible ? Qt::Checked : Qt::Unchecked);
        item->setData(0, Qt::UserRole, layer.key.layer);
        item->setData(0, Qt::UserRole + 1, layer.key.datatype);

        // Tooltip with full layer info
        QString tooltip = QString("Layer: %1\nL/D: %2/%3\nPattern: %4")
            .arg(QString::fromStdString(layer.name))
            .arg(layer.key.layer)
            .arg(layer.key.datatype)
            .arg(layer.has_pattern() ?
                 QString::fromStdString(layer.dither_pattern) : "Solid");
        item->setToolTip(0, tooltip);
        item->setToolTip(1, tooltip);

        m_layerTree->addTopLevelItem(item);
    }

    m_blockLayerSync = false;
}

void PropertiesPanel::onLayerItemChanged(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column);
    if (m_blockLayerSync || !item) {
        return;
    }
    if (!is_valid_id(m_selectedComponentId)) {
        return;
    }

    int layer = item->data(0, Qt::UserRole).toInt();
    int datatype = item->data(0, Qt::UserRole + 1).toInt();
    bool visible = (item->checkState(0) == Qt::Checked);

    emit layerVisibilityChanged(QString::fromStdString(m_selectedComponentId),
                                layer, datatype, visible);
}

void PropertiesPanel::onLayerFilterChanged(const QString& text)
{
    for (int i = 0; i < m_layerTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = m_layerTree->topLevelItem(i);
        bool match = text.isEmpty() ||
                     item->text(0).contains(text, Qt::CaseInsensitive) ||
                     item->text(1).contains(text, Qt::CaseInsensitive);
        item->setHidden(!match);
    }
}

void PropertiesPanel::setAllLayersVisible(bool visible)
{
    const Qt::CheckState target = visible ? Qt::Checked : Qt::Unchecked;
    for (int i = 0; i < m_layerTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = m_layerTree->topLevelItem(i);
        if (item->isHidden()) continue;  // filter-hidden rows untouched
        if (item->checkState(0) != target) {
            item->setCheckState(0, target);
        }
    }
}

void PropertiesPanel::showOnlyLayer(int layer, int datatype)
{
    // Operates on every row (not the filtered set): the user picked one specific
    // layer to isolate, so filter-hidden rows must also go off in 3D — otherwise
    // "only this" would silently leave them visible.
    for (int i = 0; i < m_layerTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = m_layerTree->topLevelItem(i);
        const int l = item->data(0, Qt::UserRole).toInt();
        const int d = item->data(0, Qt::UserRole + 1).toInt();
        const Qt::CheckState target = (l == layer && d == datatype)
                                          ? Qt::Checked : Qt::Unchecked;
        if (item->checkState(0) != target) {
            item->setCheckState(0, target);
        }
    }
}

void PropertiesPanel::invertLayerVisibility()
{
    for (int i = 0; i < m_layerTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = m_layerTree->topLevelItem(i);
        if (item->isHidden()) continue;  // filter-hidden rows untouched
        const Qt::CheckState target =
            item->checkState(0) == Qt::Checked ? Qt::Unchecked : Qt::Checked;
        item->setCheckState(0, target);
    }
}

void PropertiesPanel::showOnlyMatchingFilter()
{
    // Forces 3D visibility to mirror the filter: filter-visible rows get checked,
    // filter-hidden rows get unchecked. The one bulk op that does touch hidden rows.
    for (int i = 0; i < m_layerTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = m_layerTree->topLevelItem(i);
        const Qt::CheckState target =
            item->isHidden() ? Qt::Unchecked : Qt::Checked;
        if (item->checkState(0) != target) {
            item->setCheckState(0, target);
        }
    }
}

void PropertiesPanel::onLayerContextMenu(const QPoint& pos)
{
    if (!m_layerTree || m_layerTree->topLevelItemCount() == 0) {
        return;
    }

    QTreeWidgetItem* item = m_layerTree->itemAt(pos);

    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background-color: #3a3a3a; color: #ddd; border: 1px solid #555; }"
        "QMenu::item { padding: 4px 20px; }"
        "QMenu::item:selected { background-color: #3daee9; color: #fff; }"
        "QMenu::separator { height: 1px; background: #555; margin: 2px 8px; }"
    );

    if (item) {
        const bool isVisible = (item->checkState(0) == Qt::Checked);
        const int layer = item->data(0, Qt::UserRole).toInt();
        const int datatype = item->data(0, Qt::UserRole + 1).toInt();

        QAction* toggle = menu.addAction(isVisible ? "Hide" : "Show");
        connect(toggle, &QAction::triggered, this, [item, isVisible]() {
            item->setCheckState(0, isVisible ? Qt::Unchecked : Qt::Checked);
        });

        QAction* only = menu.addAction("Show only this");
        connect(only, &QAction::triggered, this, [this, layer, datatype]() {
            showOnlyLayer(layer, datatype);
        });

        menu.addSeparator();
    }

    QAction* showAll = menu.addAction("Show all");
    connect(showAll, &QAction::triggered, this, [this]() {
        setAllLayersVisible(true);
    });

    QAction* hideAll = menu.addAction("Hide all");
    connect(hideAll, &QAction::triggered, this, [this]() {
        setAllLayersVisible(false);
    });

    QAction* invert = menu.addAction("Invert visibility");
    connect(invert, &QAction::triggered, this, [this]() {
        invertLayerVisibility();
    });

    const bool hasFilter = m_layerFilter && !m_layerFilter->text().isEmpty();
    QAction* matchFilter = menu.addAction("Show only matching filter");
    matchFilter->setEnabled(hasFilter);
    connect(matchFilter, &QAction::triggered, this, [this]() {
        showOnlyMatchingFilter();
    });

    menu.exec(m_layerTree->viewport()->mapToGlobal(pos));
}

void PropertiesPanel::updateArrayGroup()
{
    Component* comp = m_assembly->component(m_selectedComponentId);
    if (!comp) return;

    bool isArray = comp->is_array();
    m_arrayGroup->setVisible(isArray);

    if (isArray && comp->array().has_value()) {
        const ComponentArray& arr = comp->array().value();
        m_arrayPatternLabel->setText(QString::fromStdString(arr.pattern));
        m_arrayCountLabel->setText(QString("%1 x %2").arg(arr.countX).arg(arr.countY));
        m_arrayPitchLabel->setText(QString("%1 x %2")
            .arg(formatValue(arr.pitchX))
            .arg(formatValue(arr.pitchY)));
    }
}

void PropertiesPanel::updateMetadataGroup()
{
    m_metadataTree->clear();

    Component* comp = m_assembly->component(m_selectedComponentId);
    if (!comp) {
        m_metadataGroup->setVisible(false);
        return;
    }

    const auto& metadata = comp->all_metadata();
    if (metadata.empty()) {
        m_metadataGroup->setVisible(false);
        return;
    }

    m_metadataGroup->setVisible(true);
    for (const auto& [key, value] : metadata) {
        QTreeWidgetItem* item = new QTreeWidgetItem();
        item->setText(0, QString::fromStdString(key));
        item->setText(1, QString::fromStdString(value));
        m_metadataTree->addTopLevelItem(item);
    }
}

void PropertiesPanel::loadLayerProperties()
{
    m_layerProps = LayerPropertiesFile();  // Reset

    if (!m_assembly || !is_valid_id(m_selectedComponentId)) {
        return;
    }

    Component* comp = m_assembly->component(m_selectedComponentId);
    if (!comp || comp->technology().empty()) {
        return;
    }

    Technology* tech = m_assembly->resolve_component_technology(m_selectedComponentId);
    if (!tech) {
        return;
    }

    const std::string& lypPath = tech->layer_properties_path();
    if (!lypPath.empty()) {
        m_layerProps.load(lypPath);
    }
}

QString PropertiesPanel::formatValue(double um) const
{
    return UnitConverter::instance().toDisplayString(um);
}

QString PropertiesPanel::formatDegrees(double degrees) const
{
    return QString::number(degrees, 'f', 2) + QString::fromUtf8("\u00B0");  // degree symbol
}

QString PropertiesPanel::typeToString(ComponentType type) const
{
    switch (type) {
        case ComponentType::Die: return "Die";
        case ComponentType::DieArray: return "Die Array";
        case ComponentType::Interposer: return "Interposer";
        case ComponentType::Substrate: return "Substrate";
    }
    return "Unknown";
}

} // namespace chiplet
