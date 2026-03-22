/**
 * HierarchyPanel.h - Tree view of assembly components
 */

#ifndef CHIPLET_UI_HIERARCHYPANEL_H
#define CHIPLET_UI_HIERARCHYPANEL_H

#include <QWidget>
#include <QIcon>
#include "core/Assembly.h"
#include "core/Component.h"
#include "ui/ComponentStyle.h"

class QTreeWidget;
class QTreeWidgetItem;
class QMenu;

namespace chiplet {

/**
 * HierarchyPanel displays the assembly structure as a tree.
 *
 * Features:
 * - Icons per component type (Die, Interposer, Substrate)
 * - Bidirectional selection sync with 3D view
 * - Context menu for component operations
 * - Multi-column display (Name, Type, Technology)
 */
class HierarchyPanel : public QWidget {
    Q_OBJECT

public:
    HierarchyPanel(QWidget* parent = nullptr);
    ~HierarchyPanel();

    void setAssembly(Assembly* assembly);
    Assembly* assembly() const { return m_assembly; }

    /**
     * Update the render mode display for a specific component
     */
    void updateRenderModeDisplay(const QString& componentId);

public slots:
    /**
     * Select a component by ID (for external sync from 3D view)
     */
    void selectComponent(const QString& componentId);

signals:
    /**
     * Emitted when a component is selected in the tree
     */
    void componentSelected(const QString& componentId);

    /**
     * Emitted when user double-clicks a component (for drill-down to 2D view)
     */
    void componentDoubleClicked(const QString& componentId);

    /**
     * Emitted when user requests to zoom to a component
     */
    void zoomToComponentRequested(const QString& componentId);

    /**
     * Emitted when user requests to show component properties
     */
    void showPropertiesRequested(const QString& componentId);

    /**
     * Emitted when component visibility is toggled
     */
    void componentVisibilityChanged(const QString& componentId, bool visible);

private slots:
    void onItemClicked(QTreeWidgetItem* item, int column);
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onCustomContextMenu(const QPoint& pos);
    void onItemChanged(QTreeWidgetItem* item, int column);

private:
    void refresh();
    void setupContextMenu();
    QIcon iconForComponent(const Component& comp) const;
    QIcon createIconFromStyle(const ComponentStyle* style) const;
    QString typeToString(ComponentType type) const;
    static QString renderModeToString(RenderMode mode);
    QTreeWidgetItem* findItemById(const QString& componentId) const;

    QTreeWidget* m_tree = nullptr;
    QMenu* m_contextMenu = nullptr;
    Assembly* m_assembly = nullptr;
    ComponentStyleFile m_styles;

    // Track currently selected item to avoid signal loops
    bool m_blockSignals = false;
};

} // namespace chiplet

#endif // CHIPLET_UI_HIERARCHYPANEL_H
