/**
 * HierarchyPanel.cpp - Implementation
 */

#include "HierarchyPanel.h"
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QMenu>
#include <QAction>
#include <QPainter>
#include <QHeaderView>

namespace chiplet {

HierarchyPanel::HierarchyPanel(QWidget* parent)
    : QWidget(parent)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({"Name", "Type", "Technology"});
    m_tree->setColumnCount(3);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setAlternatingRowColors(true);
    layout->addWidget(m_tree);

    // Setup context menu
    setupContextMenu();

    // Connect signals
    connect(m_tree, &QTreeWidget::itemClicked,
            this, &HierarchyPanel::onItemClicked);
    connect(m_tree, &QTreeWidget::itemDoubleClicked,
            this, &HierarchyPanel::onItemDoubleClicked);
    connect(m_tree, &QTreeWidget::customContextMenuRequested,
            this, &HierarchyPanel::onCustomContextMenu);
    connect(m_tree, &QTreeWidget::itemChanged,
            this, &HierarchyPanel::onItemChanged);
}

HierarchyPanel::~HierarchyPanel() = default;

void HierarchyPanel::setAssembly(Assembly* assembly)
{
    m_assembly = assembly;
    refresh();
}

void HierarchyPanel::selectComponent(const QString& componentId)
{
    if (m_blockSignals) {
        return;
    }

    m_blockSignals = true;

    QTreeWidgetItem* item = findItemById(componentId);
    if (item) {
        m_tree->setCurrentItem(item);
        m_tree->scrollToItem(item);
    } else {
        m_tree->clearSelection();
    }

    m_blockSignals = false;
}

void HierarchyPanel::refresh()
{
    // Block signals during refresh to avoid spurious itemChanged signals
    m_tree->blockSignals(true);

    m_tree->clear();

    if (!m_assembly) {
        m_tree->blockSignals(false);
        return;
    }

    // Create root node for assembly
    QTreeWidgetItem* root = new QTreeWidgetItem(m_tree);
    root->setText(0, QString::fromStdString(m_assembly->name()));
    root->setText(1, "Assembly");
    root->setText(2, "");
    root->setFlags(root->flags() & ~Qt::ItemIsSelectable);

    // Add component items with visibility checkbox
    for (const auto& comp : m_assembly->components()) {
        QTreeWidgetItem* item = new QTreeWidgetItem(root);
        item->setText(0, QString::fromStdString(comp->id()));
        item->setText(1, typeToString(comp->type()));
        item->setText(2, QString::fromStdString(comp->technology()));
        item->setIcon(0, iconForComponent(*comp));
        item->setData(0, Qt::UserRole, QString::fromStdString(comp->id()));

        // Add checkbox for visibility toggle
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, Qt::Checked);
    }

    m_tree->expandAll();

    // Resize columns to fit content
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    m_tree->blockSignals(false);
}

void HierarchyPanel::setupContextMenu()
{
    m_contextMenu = new QMenu(this);

    QAction* zoomAction = m_contextMenu->addAction("Zoom To");
    connect(zoomAction, &QAction::triggered, this, [this]() {
        QTreeWidgetItem* item = m_tree->currentItem();
        if (item && item->parent()) {
            emit zoomToComponentRequested(item->data(0, Qt::UserRole).toString());
        }
    });

    QAction* propsAction = m_contextMenu->addAction("Properties");
    connect(propsAction, &QAction::triggered, this, [this]() {
        QTreeWidgetItem* item = m_tree->currentItem();
        if (item && item->parent()) {
            emit showPropertiesRequested(item->data(0, Qt::UserRole).toString());
        }
    });
}

void HierarchyPanel::onItemClicked(QTreeWidgetItem* item, int /*column*/)
{
    if (m_blockSignals || !item || !item->parent()) {
        return;
    }

    m_blockSignals = true;
    emit componentSelected(item->data(0, Qt::UserRole).toString());
    m_blockSignals = false;
}

void HierarchyPanel::onItemDoubleClicked(QTreeWidgetItem* item, int /*column*/)
{
    if (!item || !item->parent()) {
        return;
    }

    QString componentId = item->data(0, Qt::UserRole).toString();
    emit componentDoubleClicked(componentId);  // For 2D drill-down
    emit zoomToComponentRequested(componentId);  // For 3D zoom
}

void HierarchyPanel::onCustomContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = m_tree->itemAt(pos);
    if (item && item->parent()) {
        m_contextMenu->exec(m_tree->viewport()->mapToGlobal(pos));
    }
}

void HierarchyPanel::onItemChanged(QTreeWidgetItem* item, int column)
{
    // Only handle checkbox changes in the name column
    if (!item || !item->parent() || column != 0) {
        return;
    }

    QString componentId = item->data(0, Qt::UserRole).toString();
    bool visible = (item->checkState(0) == Qt::Checked);
    emit componentVisibilityChanged(componentId, visible);
}

QIcon HierarchyPanel::iconForComponent(const Component& comp) const
{
    const ComponentStyle* style = m_styles.style_for(comp);
    return createIconFromStyle(style);
}

QIcon HierarchyPanel::createIconFromStyle(const ComponentStyle* style) const
{
    if (!style) {
        // Fallback: gray square
        QPixmap pix(16, 16);
        pix.fill(QColor(128, 128, 128));
        return QIcon(pix);
    }

    QPixmap pix(16, 16);
    pix.fill(Qt::transparent);

    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    QColor fill(style->fill_color.r, style->fill_color.g,
                style->fill_color.b, style->fill_color.a);
    QColor frame(style->frame_color.r, style->frame_color.g,
                 style->frame_color.b);

    p.setPen(QPen(frame, 1));
    p.setBrush(fill);

    // Draw shape based on icon type
    switch (style->icon) {
        case IconShape::Circle:
            p.drawEllipse(1, 1, 14, 14);
            break;

        case IconShape::Diamond:
            {
                QPolygon diamond;
                diamond << QPoint(8, 1) << QPoint(15, 8)
                        << QPoint(8, 15) << QPoint(1, 8);
                p.drawPolygon(diamond);
            }
            break;

        case IconShape::Grid:
            // 2x2 grid of squares
            p.drawRect(1, 1, 6, 6);
            p.drawRect(9, 1, 6, 6);
            p.drawRect(1, 9, 6, 6);
            p.drawRect(9, 9, 6, 6);
            break;

        case IconShape::Layers:
            // Three stacked rectangles
            p.setBrush(fill.lighter(120));
            p.drawRect(3, 1, 10, 4);
            p.setBrush(fill);
            p.drawRect(2, 5, 12, 4);
            p.setBrush(fill.darker(120));
            p.drawRect(1, 10, 14, 5);
            break;

        case IconShape::Rectangle:
            p.drawRect(1, 3, 14, 10);
            break;

        case IconShape::Square:
        default:
            p.drawRect(1, 1, 14, 14);
            break;
    }

    p.end();
    return QIcon(pix);
}

QString HierarchyPanel::typeToString(ComponentType type) const
{
    switch (type) {
        case ComponentType::Die:
            return "Die";
        case ComponentType::DieArray:
            return "Die Array";
        case ComponentType::Interposer:
            return "Interposer";
        case ComponentType::Substrate:
            return "Substrate";
    }
    return "Unknown";
}

QTreeWidgetItem* HierarchyPanel::findItemById(const QString& componentId) const
{
    if (!m_tree->topLevelItemCount()) {
        return nullptr;
    }

    QTreeWidgetItem* root = m_tree->topLevelItem(0);
    if (!root) {
        return nullptr;
    }

    for (int i = 0; i < root->childCount(); ++i) {
        QTreeWidgetItem* item = root->child(i);
        if (item->data(0, Qt::UserRole).toString() == componentId) {
            return item;
        }
    }

    return nullptr;
}

} // namespace chiplet
