/**
 * HierarchyPanel.cpp - Implementation
 */

#include "HierarchyPanel.h"
#include <QTreeWidget>
#include <QVBoxLayout>

namespace chiplet {

HierarchyPanel::HierarchyPanel(QWidget* parent)
    : QWidget(parent)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabel("Components");
    layout->addWidget(m_tree);

    connect(m_tree, &QTreeWidget::itemClicked,
            [this](QTreeWidgetItem* item, int) {
        emit componentSelected(item->data(0, Qt::UserRole).toString());
    });
}

HierarchyPanel::~HierarchyPanel() = default;

void HierarchyPanel::setAssembly(Assembly* assembly)
{
    m_assembly = assembly;
    refresh();
}

void HierarchyPanel::refresh()
{
    m_tree->clear();

    if (!m_assembly) {
        return;
    }

    QTreeWidgetItem* root = new QTreeWidgetItem(m_tree);
    root->setText(0, QString::fromStdString(m_assembly->name()));

    for (const auto& component : m_assembly->components()) {
        QTreeWidgetItem* item = new QTreeWidgetItem(root);
        item->setText(0, QString::fromStdString(component->id()));
        item->setData(0, Qt::UserRole, QString::fromStdString(component->id()));
    }

    m_tree->expandAll();
}

} // namespace chiplet
