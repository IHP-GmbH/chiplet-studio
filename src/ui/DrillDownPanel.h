/**
 * DrillDownPanel.h - Composite widget for 2D drill-down with layer control
 *
 * Wraps KLayout2DView with navigation bar, cell selection, layer/hierarchy
 * side panels, and status bar for drill-down into component layouts.
 */

#ifndef CHIPLET_UI_DRILLDOWNPANEL_H
#define CHIPLET_UI_DRILLDOWNPANEL_H

#include <QWidget>
#include <QString>

class QLabel;
class QComboBox;
class QPushButton;
class QToolButton;
class QHBoxLayout;
class QVBoxLayout;
class QSplitter;

namespace chiplet {

class KLayout2DView;

/**
 * DrillDownPanel provides a complete 2D drill-down interface.
 *
 * Layout:
 *   [Back] Component: "name" (technology)  Cell: [combo] [Layers] [Hier]
 *   +--------+---------------------------+--------+
 *   | Layer  |    KLayout 2D View        |  Cell  |
 *   | Panel  |    (central, stretch)     |  Hier. |
 *   | (opt.) |                           | (opt.) |
 *   +--------+---------------------------+--------+
 *   x: 123.4  y: 567.8  Cell: TOP_CELL
 */
class DrillDownPanel : public QWidget {
    Q_OBJECT

public:
    explicit DrillDownPanel(QWidget* parent = nullptr);
    ~DrillDownPanel();

    /**
     * @brief Access the embedded KLayout2DView
     */
    KLayout2DView* view2d() const { return m_view2d; }

    /**
     * @brief Set navigation context after loading a layout
     * @param componentId Component being viewed
     * @param componentName Display name
     * @param technologyName Technology name (may be empty)
     */
    void setContext(const QString& componentId,
                    const QString& componentName,
                    const QString& technologyName);

    /**
     * @brief Clear context and reset to empty state
     */
    void clearContext();

    /**
     * @brief Whether the layer side panel is visible
     */
    bool isLayerPanelVisible() const;

    /**
     * @brief Whether the hierarchy side panel is visible
     */
    bool isHierarchyPanelVisible() const;

public slots:
    void setLayerPanelVisible(bool visible);
    void setHierarchyPanelVisible(bool visible);

signals:
    /**
     * @brief User clicked the Back button
     */
    void backRequested();

    /**
     * @brief User selected a different cell in the combo box
     */
    void cellSelected(const QString& cellName);

private slots:
    void onCellComboChanged(int index);
    void onPositionChanged(double x, double y);
    void onLayoutChanged(bool hasLayout);

private:
    void setupUI();
    void updateSidePanels();
    void populateCellCombo();

    // Nav bar
    QPushButton* m_backButton = nullptr;
    QLabel* m_contextLabel = nullptr;
    QComboBox* m_cellCombo = nullptr;
    QToolButton* m_layerToggle = nullptr;
    QToolButton* m_hierToggle = nullptr;

    // Central
    KLayout2DView* m_view2d = nullptr;

    // Side panels
    QWidget* m_layerContainer = nullptr;
    QWidget* m_hierContainer = nullptr;
    QSplitter* m_splitter = nullptr;

    // Status bar
    QLabel* m_posLabel = nullptr;
    QLabel* m_cellLabel = nullptr;

    // State
    QString m_componentId;
    bool m_blockCellCombo = false;
    bool m_layerPanelVisible = true;
    bool m_hierPanelVisible = false;
};

} // namespace chiplet

#endif // CHIPLET_UI_DRILLDOWNPANEL_H
