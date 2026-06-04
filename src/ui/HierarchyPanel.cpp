// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * HierarchyPanel.cpp - Implementation
 */

#include "HierarchyPanel.h"
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QMenu>
#include <QAction>
#include <QFont>
#include <QPainter>
#include <QHeaderView>

namespace chiplet {

HierarchyPanel::HierarchyPanel(QWidget* parent)
    : QWidget(parent)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({"Name", "Type", "Technology", "Mode"});
    m_tree->setColumnCount(4);
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
        item->setText(3, renderModeToString(comp->render_mode()));
        item->setIcon(0, iconForComponent(*comp));
        item->setData(0, Qt::UserRole, QString::fromStdString(comp->id()));

        // Add checkbox for visibility toggle
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, Qt::Checked);

        // Child row: the die's interconnect method (its per-die
        // `connection:` id). Selectable -- the properties panel shows the
        // method's fragment provenance -- but not checkable: the bodies
        // render merged into the interposer, so show/hide stays with the
        // assembly-level interconnect row below.
        if (comp->type() != ComponentType::Interposer
                && !comp->connection().empty()) {
            QTreeWidgetItem* conn = new QTreeWidgetItem(item);
            conn->setText(0, "interconnect");
            conn->setText(1, "Interconnect");
            conn->setText(2, QString::fromStdString(comp->connection()));
            conn->setText(3, "");
            QFont cf = conn->font(0);
            cf.setItalic(true);
            for (int col = 0; col < 4; ++col) {
                conn->setFont(col, cf);
            }
            // No Qt::UserRole component id (component handlers skip it);
            // the marker routes clicks to interconnectMethodSelected.
            conn->setData(0, Qt::UserRole + 1,
                          QString("interconnect-method"));
            conn->setData(0, Qt::UserRole + 2,
                          QString::fromStdString(comp->id()));
        }
    }

    // Row for the assembly-level interconnect (the bumping methods). Not a
    // component: its 3D bodies merge into the interposer's layer render. It
    // is selectable (provenance in the properties panel) and its checkbox
    // shows/hides the methods' body layers on the interposer. The tech
    // column lists the methods the dies use (per-die connection ids); the
    // legacy adapter id shows when no method is declared.
    const std::vector<std::string> methodIds =
        m_assembly->interconnect_method_ids();
    if (!m_assembly->interconnect_adapter().empty() || !methodIds.empty()) {
        QStringList methods;
        for (const auto& id : methodIds) {
            methods << QString::fromStdString(id);
        }
        QTreeWidgetItem* ic = new QTreeWidgetItem(root);
        ic->setText(0, "interconnect");
        ic->setText(1, "Interconnect");
        ic->setText(2, methods.isEmpty()
                           ? QString::fromStdString(m_assembly->interconnect_adapter())
                           : methods.join(", "));
        ic->setText(3, "");
        ic->setFlags(ic->flags() | Qt::ItemIsUserCheckable);
        ic->setCheckState(0, Qt::Checked);
        QFont f = ic->font(0);
        f.setItalic(true);
        for (int col = 0; col < 4; ++col) {
            ic->setFont(col, f);
        }
        // No Qt::UserRole component id: the component handlers skip rows
        // without one. The marker below routes clicks/toggles to the
        // interconnect signals.
        ic->setData(0, Qt::UserRole + 1, QString("interconnect"));
    }

    m_tree->expandAll();

    // Resize columns to fit content
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);

    m_tree->blockSignals(false);
}

void HierarchyPanel::setupContextMenu()
{
    m_contextMenu = new QMenu(this);
    // Menu is built dynamically in onCustomContextMenu
}

void HierarchyPanel::onItemClicked(QTreeWidgetItem* item, int /*column*/)
{
    if (m_blockSignals || !item || !item->parent()) {
        return;
    }

    QString componentId = item->data(0, Qt::UserRole).toString();
    if (componentId.isEmpty()) {
        // Non-component rows route through their marker.
        const QString marker = item->data(0, Qt::UserRole + 1).toString();
        if (marker == "interconnect") {
            // Assembly-level interconnect row.
            m_blockSignals = true;
            emit interconnectSelected();
            m_blockSignals = false;
        } else if (marker == "interconnect-method") {
            // A die's per-die method child row.
            m_blockSignals = true;
            emit interconnectMethodSelected(
                item->data(0, Qt::UserRole + 2).toString(),
                item->text(2));
            m_blockSignals = false;
        }
        return;
    }

    m_blockSignals = true;
    emit componentSelected(componentId);
    m_blockSignals = false;
}

void HierarchyPanel::onItemDoubleClicked(QTreeWidgetItem* item, int /*column*/)
{
    if (!item || !item->parent()) {
        return;
    }

    QString componentId = item->data(0, Qt::UserRole).toString();
    if (componentId.isEmpty()) {
        return;  // informative row (interconnect adapter), not a component
    }
    emit componentDoubleClicked(componentId);  // For 2D drill-down
    emit zoomToComponentRequested(componentId);  // For 3D zoom
}

void HierarchyPanel::onCustomContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = m_tree->itemAt(pos);
    if (item && item->parent() && m_assembly &&
        !item->data(0, Qt::UserRole).toString().isEmpty()) {
        QString componentId = item->data(0, Qt::UserRole).toString();
        Component* comp = m_assembly->component(componentId.toStdString());

        // Rebuild context menu with render mode submenu
        m_contextMenu->clear();

        QAction* zoomAction = m_contextMenu->addAction("Zoom To");
        connect(zoomAction, &QAction::triggered, this, [this, componentId]() {
            emit zoomToComponentRequested(componentId);
        });

        QAction* propsAction = m_contextMenu->addAction("Properties");
        connect(propsAction, &QAction::triggered, this, [this, componentId]() {
            emit showPropertiesRequested(componentId);
        });

        if (comp) {
            m_contextMenu->addSeparator();
            QMenu* modeMenu = m_contextMenu->addMenu("Render Mode");
            RenderMode currentMode = comp->render_mode();

            struct ModeEntry { RenderMode mode; const char* label; };
            ModeEntry modes[] = {
                {RenderMode::Wireframe,           "Wireframe"},
                {RenderMode::Transparent,         "Transparent"},
                {RenderMode::Detailed,            "Detailed (with Si bulk)"},
                {RenderMode::DetailedNoSubstrate, "Detailed (no Si bulk)"},
            };

            for (const auto& entry : modes) {
                QAction* action = modeMenu->addAction(entry.label);
                action->setCheckable(true);
                action->setChecked(entry.mode == currentMode);
                RenderMode targetMode = entry.mode;
                connect(action, &QAction::triggered, this, [this, componentId, targetMode]() {
                    emit renderModeChangeRequested(componentId, targetMode);
                });
            }
        }

        m_contextMenu->exec(m_tree->viewport()->mapToGlobal(pos));
    }
}

void HierarchyPanel::onItemChanged(QTreeWidgetItem* item, int column)
{
    // Only handle checkbox changes in the name column
    if (!item || !item->parent() || column != 0) {
        return;
    }

    bool visible = (item->checkState(0) == Qt::Checked);

    QString componentId = item->data(0, Qt::UserRole).toString();
    if (componentId.isEmpty()) {
        // Interconnect adapter row: toggle the method's body layers.
        if (item->data(0, Qt::UserRole + 1).toString() == "interconnect") {
            emit interconnectVisibilityChanged(visible);
        }
        return;
    }
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

void HierarchyPanel::updateRenderModeDisplay(const QString& componentId)
{
    QTreeWidgetItem* item = findItemById(componentId);
    if (!item || !m_assembly) return;

    Component* comp = m_assembly->component(componentId.toStdString());
    if (!comp) return;

    m_tree->blockSignals(true);
    item->setText(3, renderModeToString(comp->render_mode()));
    m_tree->blockSignals(false);
}

QString HierarchyPanel::renderModeToString(RenderMode mode)
{
    switch (mode) {
        case RenderMode::Hidden:               return "Hidden";
        case RenderMode::Wireframe:            return "Wire";
        case RenderMode::Transparent:          return "Trans";
        case RenderMode::Solid:                return "Solid";
        case RenderMode::Detailed:             return "Detail";
        case RenderMode::DetailedNoSubstrate:  return "Detail-NoSi";
    }
    return "?";
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
