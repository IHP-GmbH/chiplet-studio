/**
 * NetGraphPanel.cpp - Net connectivity graph visualization
 */

#include "NetGraphPanel.h"
#include "core/Assembly.h"
#include "core/Component.h"
#include "core/Netlist.h"
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsRectItem>
#include <QGraphicsLineItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsTextItem>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QLabel>
#include <QFrame>
#include <QPen>
#include <QBrush>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <cmath>
#include <set>
#include <algorithm>

namespace chiplet {

namespace {

// Custom graphics item for component nodes
class ComponentNode : public QGraphicsRectItem {
public:
    ComponentNode(const QString& componentId, const QString& displayName,
                  ComponentType type, QColor color, QGraphicsItem* parent = nullptr)
        : QGraphicsRectItem(0, 0, NetGraphPanel::NODE_WIDTH, NetGraphPanel::NODE_HEIGHT, parent)
        , m_componentId(componentId)
        , m_displayName(displayName)
        , m_typeLabel(NetGraphPanel::labelForComponentType(type))
        , m_fillColor(color)
        , m_highlighted(false)
    {
        setData(NetGraphPanel::ROLE_COMPONENT_ID, componentId);
        setData(NetGraphPanel::ROLE_ITEM_TYPE, NetGraphPanel::TYPE_NODE);
        setFlag(ItemIsSelectable, true);
        setAcceptHoverEvents(true);
        setPen(QPen(Qt::black, 1.5));
        setBrush(QBrush(m_fillColor));
    }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* /*option*/,
               QWidget* /*widget*/) override
    {
        painter->setRenderHint(QPainter::Antialiasing);

        // Background
        QRectF r = rect();
        if (m_highlighted) {
            painter->setPen(QPen(QColor(255, 200, 0), 3.0));
        } else if (isSelected()) {
            painter->setPen(QPen(QColor(0, 120, 215), 2.5));
        } else {
            painter->setPen(QPen(Qt::black, 1.5));
        }
        painter->setBrush(QBrush(m_fillColor));
        painter->drawRoundedRect(r, 8.0, 8.0);

        // Name (bold, centered)
        QFont nameFont;
        nameFont.setBold(true);
        nameFont.setPointSize(10);
        painter->setFont(nameFont);
        painter->setPen(Qt::black);
        QRectF nameRect(r.x(), r.y() + 8, r.width(), r.height() / 2 - 4);
        painter->drawText(nameRect, Qt::AlignCenter, m_displayName);

        // Type label (smaller, below name)
        QFont typeFont;
        typeFont.setPointSize(8);
        typeFont.setItalic(true);
        painter->setFont(typeFont);
        painter->setPen(QColor(60, 60, 60));
        QRectF typeRect(r.x(), r.y() + r.height() / 2 + 2, r.width(), r.height() / 2 - 6);
        painter->drawText(typeRect, Qt::AlignCenter, m_typeLabel);
    }

    void setHighlighted(bool h)
    {
        m_highlighted = h;
        update();
    }

    bool isHighlighted() const { return m_highlighted; }
    const QString& componentId() const { return m_componentId; }

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override
    {
        setCursor(Qt::PointingHandCursor);
        QGraphicsRectItem::hoverEnterEvent(event);
    }

    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override
    {
        setCursor(Qt::ArrowCursor);
        QGraphicsRectItem::hoverLeaveEvent(event);
    }

private:
    QString m_componentId;
    QString m_displayName;
    QString m_typeLabel;
    QColor m_fillColor;
    bool m_highlighted;
};

// Custom graphics item for net edges
class NetEdge : public QGraphicsLineItem {
public:
    NetEdge(const QPointF& p1, const QPointF& p2, NetClass nc,
            const QString& tooltipText, QGraphicsItem* parent = nullptr)
        : QGraphicsLineItem(p1.x(), p1.y(), p2.x(), p2.y(), parent)
        , m_netClass(nc)
        , m_baseColor(NetGraphPanel::colorForNetClass(nc))
    {
        setData(NetGraphPanel::ROLE_NET_CLASS, static_cast<int>(nc));
        setData(NetGraphPanel::ROLE_ITEM_TYPE, NetGraphPanel::TYPE_EDGE);
        setPen(QPen(m_baseColor, 2.0));
        setAcceptHoverEvents(true);
        setToolTip(tooltipText);
    }

    void setHighlighted(bool h)
    {
        if (h) {
            QPen p = pen();
            p.setWidthF(4.0);
            p.setColor(m_baseColor.lighter(130));
            setPen(p);
        } else {
            setPen(QPen(m_baseColor, 2.0));
        }
    }

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override
    {
        QPen p = pen();
        p.setWidthF(3.5);
        setPen(p);
        QGraphicsLineItem::hoverEnterEvent(event);
    }

    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override
    {
        QPen p = pen();
        p.setWidthF(2.0);
        setPen(p);
        QGraphicsLineItem::hoverLeaveEvent(event);
    }

private:
    NetClass m_netClass;
    QColor m_baseColor;
};

} // anonymous namespace


// --- NetGraphPanel implementation ---

NetGraphPanel::NetGraphPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
}

NetGraphPanel::~NetGraphPanel() = default;

void NetGraphPanel::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    m_stack = new QStackedWidget(this);

    // Page 0: empty state
    m_emptyLabel = new QLabel("No netlist defined.\nLoad a .chiplet file with a netlist section.");
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setStyleSheet("QLabel { color: #888; font-size: 12px; }");
    m_stack->addWidget(m_emptyLabel);

    // Page 1: populated state
    auto* populatedWidget = new QWidget();
    auto* popLayout = new QVBoxLayout(populatedWidget);
    popLayout->setContentsMargins(4, 4, 4, 4);
    popLayout->setSpacing(4);

    // Filter bar
    auto* filterBar = new QHBoxLayout();
    filterBar->setSpacing(8);
    auto* filterLabel = new QLabel("Filter:");
    filterLabel->setStyleSheet("font-weight: bold;");
    filterBar->addWidget(filterLabel);

    m_filterSignal = new QCheckBox("Signal");
    m_filterSignal->setChecked(true);
    filterBar->addWidget(m_filterSignal);

    m_filterPower = new QCheckBox("Power");
    m_filterPower->setChecked(true);
    filterBar->addWidget(m_filterPower);

    m_filterGround = new QCheckBox("Ground");
    m_filterGround->setChecked(true);
    filterBar->addWidget(m_filterGround);

    m_filterDiffPair = new QCheckBox("DiffPair");
    m_filterDiffPair->setChecked(true);
    filterBar->addWidget(m_filterDiffPair);

    m_filterInterface = new QCheckBox("Interface");
    m_filterInterface->setChecked(true);
    filterBar->addWidget(m_filterInterface);

    filterBar->addStretch();
    popLayout->addLayout(filterBar);

    // Connect filter checkboxes
    auto filterSlot = [this](int) { applyFilters(); };
    connect(m_filterSignal, &QCheckBox::stateChanged, this, filterSlot);
    connect(m_filterPower, &QCheckBox::stateChanged, this, filterSlot);
    connect(m_filterGround, &QCheckBox::stateChanged, this, filterSlot);
    connect(m_filterDiffPair, &QCheckBox::stateChanged, this, filterSlot);
    connect(m_filterInterface, &QCheckBox::stateChanged, this, filterSlot);

    // Graphics view
    m_scene = new QGraphicsScene(this);
    m_view = new QGraphicsView(m_scene);
    m_view->setRenderHint(QPainter::Antialiasing);
    m_view->setDragMode(QGraphicsView::ScrollHandDrag);
    m_view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    m_view->setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    popLayout->addWidget(m_view);

    m_stack->addWidget(populatedWidget);

    mainLayout->addWidget(m_stack);
    m_stack->setCurrentIndex(0);

    // Scene selection -> componentSelected signal
    connect(m_scene, &QGraphicsScene::selectionChanged,
            this, &NetGraphPanel::onSceneSelectionChanged);
}

void NetGraphPanel::setAssembly(Assembly* assembly)
{
    m_assembly = assembly;

    if (!m_assembly || m_assembly->netlist().empty()) {
        clearGraph();
        m_stack->setCurrentIndex(0);
        return;
    }

    clearGraph();
    buildGraph();
    m_stack->setCurrentIndex(1);

    // Fit the view to the graph
    if (m_view && m_scene && !m_scene->items().isEmpty()) {
        m_view->fitInView(m_scene->sceneRect().adjusted(-30, -30, 30, 30),
                          Qt::KeepAspectRatio);
    }
}

Assembly* NetGraphPanel::assembly() const
{
    return m_assembly;
}

QGraphicsView* NetGraphPanel::graphicsView() const { return m_view; }
QGraphicsScene* NetGraphPanel::graphicsScene() const { return m_scene; }
QStackedWidget* NetGraphPanel::stack() const { return m_stack; }

int NetGraphPanel::nodeCount() const
{
    int count = 0;
    for (auto* item : m_scene->items()) {
        if (item->data(ROLE_ITEM_TYPE).toInt() == TYPE_NODE) {
            ++count;
        }
    }
    return count;
}

int NetGraphPanel::edgeCount() const
{
    int count = 0;
    for (auto* item : m_scene->items()) {
        if (item->data(ROLE_ITEM_TYPE).toInt() == TYPE_EDGE) {
            ++count;
        }
    }
    return count;
}

int NetGraphPanel::hubCount() const
{
    int count = 0;
    for (auto* item : m_scene->items()) {
        if (item->data(ROLE_ITEM_TYPE).toInt() == TYPE_HUB) {
            ++count;
        }
    }
    return count;
}

void NetGraphPanel::clearGraph()
{
    m_scene->clear();
    m_nodeMap.clear();
    m_highlightedComponent.clear();
    m_legendFrame = nullptr;
}

void NetGraphPanel::buildGraph()
{
    if (!m_assembly) return;

    const Netlist& netlist = m_assembly->netlist();

    // Collect unique component IDs from net connections
    std::set<std::string> componentIds;
    for (const auto& net : netlist.nets()) {
        for (const auto& conn : net.connections()) {
            componentIds.insert(conn.component);
        }
    }

    if (componentIds.empty()) return;

    // Create nodes for each component
    for (const auto& id : componentIds) {
        QString qid = QString::fromStdString(id);
        ComponentType type = ComponentType::Die; // default
        const Component* comp = m_assembly->component(id);
        if (comp) {
            type = comp->type();
        }

        QColor color = colorForComponentType(type);
        auto* node = new ComponentNode(qid, qid, type, color);
        m_scene->addItem(node);
        m_nodeMap[qid] = node;
    }

    layoutNodes();
    createEdges();
    createLegend();
}

void NetGraphPanel::layoutNodes()
{
    if (m_nodeMap.empty()) return;

    // Sort nodes: Substrate first, then Interposer, then Dies
    std::vector<std::pair<QString, QGraphicsItem*>> sorted(
        m_nodeMap.begin(), m_nodeMap.end());

    std::sort(sorted.begin(), sorted.end(),
        [this](const auto& a, const auto& b) {
            auto typeOf = [this](const QString& id) -> int {
                if (!m_assembly) return 3;
                const Component* c = m_assembly->component(id.toStdString());
                if (!c) return 3;
                switch (c->type()) {
                    case ComponentType::Substrate:  return 0;
                    case ComponentType::Interposer: return 1;
                    case ComponentType::DieArray:   return 2;
                    case ComponentType::Die:        return 3;
                }
                return 3;
            };
            int ta = typeOf(a.first);
            int tb = typeOf(b.first);
            if (ta != tb) return ta < tb;
            return a.first < b.first;
        });

    int n = static_cast<int>(sorted.size());
    double radius = LAYOUT_RADIUS;
    if (n <= 2) radius = LAYOUT_RADIUS * 0.5;

    double cx = 0.0;
    double cy = 0.0;

    for (int i = 0; i < n; ++i) {
        // Start from top, go clockwise
        double angle = -M_PI / 2.0 + (2.0 * M_PI * i) / n;
        double x = cx + radius * std::cos(angle) - NODE_WIDTH / 2.0;
        double y = cy + radius * std::sin(angle) - NODE_HEIGHT / 2.0;
        sorted[i].second->setPos(x, y);
    }
}

void NetGraphPanel::createEdges()
{
    if (!m_assembly) return;

    const Netlist& netlist = m_assembly->netlist();

    for (const auto& net : netlist.nets()) {
        const auto& conns = net.connections();
        if (conns.size() < 2) continue;

        // Collect unique components for this net
        std::vector<QString> netComponents;
        std::map<QString, std::vector<std::string>> pinsByComponent;
        for (const auto& conn : conns) {
            QString qid = QString::fromStdString(conn.component);
            pinsByComponent[qid].push_back(conn.pin);
            if (m_nodeMap.count(qid) &&
                std::find(netComponents.begin(), netComponents.end(), qid) == netComponents.end()) {
                netComponents.push_back(qid);
            }
        }

        if (netComponents.size() < 2) continue;

        QString netName = QString::fromStdString(net.name());
        NetClass nc = net.net_class();

        auto centerOf = [this](const QString& id) -> QPointF {
            auto it = m_nodeMap.find(id);
            if (it == m_nodeMap.end()) return QPointF(0, 0);
            QRectF r = it->second->sceneBoundingRect();
            return r.center();
        };

        auto buildTooltip = [&](const QString& comp1, const QString& comp2) -> QString {
            QString tip = "Net: " + netName + "\n";
            auto it1 = pinsByComponent.find(comp1);
            if (it1 != pinsByComponent.end()) {
                for (const auto& p : it1->second) {
                    tip += comp1 + "." + QString::fromStdString(p) + "\n";
                }
            }
            auto it2 = pinsByComponent.find(comp2);
            if (it2 != pinsByComponent.end()) {
                for (const auto& p : it2->second) {
                    tip += comp2 + "." + QString::fromStdString(p) + "\n";
                }
            }
            return tip.trimmed();
        };

        if (netComponents.size() == 2) {
            // Simple edge between two nodes
            QPointF p1 = centerOf(netComponents[0]);
            QPointF p2 = centerOf(netComponents[1]);
            QString tooltip = buildTooltip(netComponents[0], netComponents[1]);
            auto* edge = new NetEdge(p1, p2, nc, tooltip);
            m_scene->addItem(edge);
        } else {
            // Multi-drop: star topology with hub at centroid
            QPointF centroid(0, 0);
            for (const auto& comp : netComponents) {
                centroid += centerOf(comp);
            }
            centroid /= static_cast<double>(netComponents.size());

            // Create hub dot
            double hubRadius = 3.0;
            auto* hub = new QGraphicsEllipseItem(
                centroid.x() - hubRadius, centroid.y() - hubRadius,
                hubRadius * 2, hubRadius * 2);
            QColor hubColor = colorForNetClass(nc);
            hubColor.setAlpha(128);
            hub->setBrush(QBrush(hubColor));
            hub->setPen(QPen(colorForNetClass(nc), 1.0));
            hub->setData(ROLE_NET_CLASS, static_cast<int>(nc));
            hub->setData(ROLE_ITEM_TYPE, TYPE_HUB);
            hub->setToolTip(QString("Net: %1 (multi-drop, %2 connections)")
                            .arg(netName).arg(netComponents.size()));
            m_scene->addItem(hub);

            // Edges from each component to hub
            for (const auto& comp : netComponents) {
                QPointF p = centerOf(comp);
                QString tip = "Net: " + netName + "\n";
                auto it = pinsByComponent.find(comp);
                if (it != pinsByComponent.end()) {
                    for (const auto& pin : it->second) {
                        tip += comp + "." + QString::fromStdString(pin) + "\n";
                    }
                }
                auto* edge = new NetEdge(p, centroid, nc, tip.trimmed());
                m_scene->addItem(edge);
            }
        }
    }
}

void NetGraphPanel::createLegend()
{
    // Build legend as scene items in top-right area
    struct LegendEntry {
        QString label;
        QColor color;
    };

    std::vector<LegendEntry> entries = {
        {"Signal",    colorForNetClass(NetClass::Signal)},
        {"Power",     colorForNetClass(NetClass::Power)},
        {"Ground",    colorForNetClass(NetClass::Ground)},
        {"DiffPair",  colorForNetClass(NetClass::DiffPair)},
        {"Interface", colorForNetClass(NetClass::Interface)},
    };

    QRectF sceneRect = m_scene->itemsBoundingRect();
    double legendX = sceneRect.right() + 20;
    double legendY = sceneRect.top();
    double rowHeight = 20.0;
    double swatchSize = 12.0;

    // Legend title
    auto* title = m_scene->addText("Net Classes");
    title->setFont(QFont("sans-serif", 9, QFont::Bold));
    title->setPos(legendX, legendY);

    legendY += rowHeight + 4;

    for (const auto& entry : entries) {
        // Color swatch
        auto* swatch = m_scene->addRect(legendX, legendY + 2, swatchSize, swatchSize,
                                         QPen(Qt::black, 0.5), QBrush(entry.color));
        swatch->setData(ROLE_ITEM_TYPE, 0); // not node/edge/hub

        // Label
        auto* label = m_scene->addText(entry.label);
        label->setFont(QFont("sans-serif", 8));
        label->setPos(legendX + swatchSize + 6, legendY - 2);

        legendY += rowHeight;
    }
}

void NetGraphPanel::applyFilters()
{
    auto isVisible = [this](int ncInt) -> bool {
        auto nc = static_cast<NetClass>(ncInt);
        switch (nc) {
            case NetClass::Signal:    return m_filterSignal->isChecked();
            case NetClass::Power:     return m_filterPower->isChecked();
            case NetClass::Ground:    return m_filterGround->isChecked();
            case NetClass::DiffPair:  return m_filterDiffPair->isChecked();
            case NetClass::Interface: return m_filterInterface->isChecked();
            case NetClass::NC:        return true; // always show NC (rare)
        }
        return true;
    };

    for (auto* item : m_scene->items()) {
        int itemType = item->data(ROLE_ITEM_TYPE).toInt();
        if (itemType == TYPE_EDGE || itemType == TYPE_HUB) {
            int nc = item->data(ROLE_NET_CLASS).toInt();
            item->setVisible(isVisible(nc));
        }
    }
}

void NetGraphPanel::onSceneSelectionChanged()
{
    if (m_blockSelectionSignal) return;

    auto selected = m_scene->selectedItems();
    for (auto* item : selected) {
        if (item->data(ROLE_ITEM_TYPE).toInt() == TYPE_NODE) {
            QString id = item->data(ROLE_COMPONENT_ID).toString();
            if (!id.isEmpty()) {
                emit componentSelected(id);
                return;
            }
        }
    }
}

void NetGraphPanel::highlightComponent(const QString& componentId)
{
    clearHighlight();
    m_highlightedComponent = componentId;

    auto it = m_nodeMap.find(componentId);
    if (it == m_nodeMap.end()) return;

    auto* node = dynamic_cast<ComponentNode*>(it->second);
    if (node) {
        node->setHighlighted(true);
    }

    // Highlight connected edges: find all edges touching this node's center
    QPointF nodeCenter = it->second->sceneBoundingRect().center();
    for (auto* item : m_scene->items()) {
        if (item->data(ROLE_ITEM_TYPE).toInt() == TYPE_EDGE) {
            auto* edge = dynamic_cast<NetEdge*>(item);
            if (!edge) continue;
            QLineF line = edge->line();
            QPointF ep1 = edge->mapToScene(line.p1());
            QPointF ep2 = edge->mapToScene(line.p2());
            // Check if either endpoint is near this node's center
            double d1 = QLineF(ep1, nodeCenter).length();
            double d2 = QLineF(ep2, nodeCenter).length();
            if (d1 < NODE_WIDTH || d2 < NODE_WIDTH) {
                edge->setHighlighted(true);
            }
        }
    }

    // Also select the node in the scene (for visual feedback)
    m_blockSelectionSignal = true;
    m_scene->clearSelection();
    it->second->setSelected(true);
    m_blockSelectionSignal = false;
}

void NetGraphPanel::clearHighlight()
{
    for (auto& [id, item] : m_nodeMap) {
        auto* node = dynamic_cast<ComponentNode*>(item);
        if (node) {
            node->setHighlighted(false);
        }
    }

    for (auto* item : m_scene->items()) {
        if (item->data(ROLE_ITEM_TYPE).toInt() == TYPE_EDGE) {
            auto* edge = dynamic_cast<NetEdge*>(item);
            if (edge) {
                edge->setHighlighted(false);
            }
        }
    }

    m_blockSelectionSignal = true;
    m_scene->clearSelection();
    m_blockSelectionSignal = false;

    m_highlightedComponent.clear();
}

// Static color helpers

QColor NetGraphPanel::colorForNetClass(NetClass nc)
{
    switch (nc) {
        case NetClass::Power:     return QColor(220, 50, 50);     // Red
        case NetClass::Ground:    return QColor(40, 40, 40);      // Near-black
        case NetClass::Signal:    return QColor(50, 180, 50);     // Green
        case NetClass::DiffPair:  return QColor(230, 150, 30);    // Orange
        case NetClass::Interface: return QColor(140, 50, 200);    // Purple
        case NetClass::NC:        return QColor(180, 180, 180);   // Gray
    }
    return QColor(128, 128, 128);
}

QColor NetGraphPanel::colorForComponentType(ComponentType type)
{
    switch (type) {
        case ComponentType::Die:        return QColor(100, 150, 220);  // Blue
        case ComponentType::DieArray:   return QColor(80, 130, 200);   // Darker blue
        case ComponentType::Interposer: return QColor(180, 220, 100);  // Green-yellow
        case ComponentType::Substrate:  return QColor(220, 180, 100);  // Sandy/gold
    }
    return QColor(180, 180, 180);
}

QString NetGraphPanel::labelForComponentType(ComponentType type)
{
    switch (type) {
        case ComponentType::Die:        return "Die";
        case ComponentType::DieArray:   return "Die Array";
        case ComponentType::Interposer: return "Interposer";
        case ComponentType::Substrate:  return "Substrate";
    }
    return "Unknown";
}

} // namespace chiplet
