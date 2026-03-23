/**
 * NetGraphPanel.h - Net connectivity graph visualization
 *
 * Displays assembly netlist as a graph: component nodes connected by net edges.
 * Uses QGraphicsView/QGraphicsScene with custom items for nodes, edges, and hubs.
 * Supports NetClass filtering, selection sync, and hover tooltips.
 */

#ifndef CHIPLET_UI_NETGRAPHPANEL_H
#define CHIPLET_UI_NETGRAPHPANEL_H

#include <QWidget>
#include <QString>
#include <map>

class QGraphicsView;
class QGraphicsScene;
class QGraphicsItem;
class QStackedWidget;
class QCheckBox;
class QLabel;

namespace chiplet {

class Assembly;
enum class NetClass;
enum class ComponentType;

class NetGraphPanel : public QWidget {
    Q_OBJECT

public:
    explicit NetGraphPanel(QWidget* parent = nullptr);
    ~NetGraphPanel() override;

    void setAssembly(Assembly* assembly);
    Assembly* assembly() const;

    // Test accessors
    QGraphicsView* graphicsView() const;
    QGraphicsScene* graphicsScene() const;
    QStackedWidget* stack() const;
    int nodeCount() const;
    int edgeCount() const;
    int hubCount() const;

    // Layout constants (public for use by graphics item subclasses)
    static constexpr double NODE_WIDTH = 120.0;
    static constexpr double NODE_HEIGHT = 60.0;
    static constexpr double LAYOUT_RADIUS = 200.0;

    // Custom data roles
    static constexpr int ROLE_COMPONENT_ID = Qt::UserRole;
    static constexpr int ROLE_NET_CLASS = Qt::UserRole + 1;
    static constexpr int ROLE_ITEM_TYPE = Qt::UserRole + 2;

    // Item type values stored in ROLE_ITEM_TYPE
    static constexpr int TYPE_NODE = 1;
    static constexpr int TYPE_EDGE = 2;
    static constexpr int TYPE_HUB = 3;

    // Color/label helpers (public for use by graphics item subclasses)
    static QColor colorForNetClass(NetClass nc);
    static QColor colorForComponentType(ComponentType type);
    static QString labelForComponentType(ComponentType type);

public slots:
    void highlightComponent(const QString& componentId);
    void clearHighlight();

signals:
    void componentSelected(const QString& componentId);

private:
    void setupUI();
    void buildGraph();
    void clearGraph();
    void layoutNodes();
    void createEdges();
    void createLegend();
    void applyFilters();
    void onSceneSelectionChanged();

    // Widgets
    QStackedWidget* m_stack = nullptr;
    QLabel* m_emptyLabel = nullptr;
    QGraphicsView* m_view = nullptr;
    QGraphicsScene* m_scene = nullptr;
    QWidget* m_legendFrame = nullptr;

    // Filter checkboxes
    QCheckBox* m_filterSignal = nullptr;
    QCheckBox* m_filterPower = nullptr;
    QCheckBox* m_filterGround = nullptr;
    QCheckBox* m_filterDiffPair = nullptr;
    QCheckBox* m_filterInterface = nullptr;

    // State
    Assembly* m_assembly = nullptr;
    std::map<QString, QGraphicsItem*> m_nodeMap;
    QString m_highlightedComponent;
    bool m_blockSelectionSignal = false;
};

} // namespace chiplet

#endif // CHIPLET_UI_NETGRAPHPANEL_H
