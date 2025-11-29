/**
 * HierarchyPanel.h - Tree view of assembly components
 */

#ifndef CHIPLET_UI_HIERARCHYPANEL_H
#define CHIPLET_UI_HIERARCHYPANEL_H

#include <QWidget>
#include "core/Assembly.h"

class QTreeWidget;

namespace chiplet {

/**
 * HierarchyPanel displays the assembly structure as a tree.
 */
class HierarchyPanel : public QWidget {
    Q_OBJECT

public:
    HierarchyPanel(QWidget* parent = nullptr);
    ~HierarchyPanel();

    void setAssembly(Assembly* assembly);

signals:
    void componentSelected(const QString& componentId);

private:
    void refresh();

    QTreeWidget* m_tree = nullptr;
    Assembly* m_assembly = nullptr;
};

} // namespace chiplet

#endif // CHIPLET_UI_HIERARCHYPANEL_H
