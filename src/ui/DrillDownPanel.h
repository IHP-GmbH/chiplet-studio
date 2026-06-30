// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

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
#include <memory>

class QLabel;
class QComboBox;
class QPushButton;
class QToolButton;
class QHBoxLayout;
class QVBoxLayout;
class QSplitter;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace chiplet {

class Assembly;
class KLayout2DView;
class CellComponentMapper;

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
    enum class PanelMode { Empty, Assembly, DrillDown };

    explicit DrillDownPanel(QWidget* parent = nullptr);
    ~DrillDownPanel();

    /**
     * @brief Access the embedded KLayout2DView
     */
    KLayout2DView* view2d() const { return m_view2d; }

    /**
     * @brief Load the assembly GDS for the default 2D view
     */
    void setAssemblyGds(const QString& gdsPath, const QString& lypPath,
                        const Assembly& assembly);

    /**
     * @brief Return to assembly view from drill-down
     */
    void returnToAssembly();

    /**
     * @brief Reset the assembly view to the full top-level layout
     *
     * Undoes any in-place navigation done through KLayout's hierarchy panel
     * (descending into a cell, "show as new top", ...) by restoring the original
     * assembly top cell and zooming to fit. No-op outside Assembly mode.
     */
    void showFullAssembly();

    /**
     * @brief Current panel mode
     */
    PanelMode panelMode() const { return m_panelMode; }

    /**
     * @brief Access the cell-to-component mapper
     */
    const CellComponentMapper& cellMapper() const { return *m_cellMapper; }

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
     *
     * Note: in DrillDown mode this returns to the still-loaded assembly GDS (it
     * is the Back action). To forget the assembly GDS entirely, use reset().
     */
    void clearContext();

    /**
     * @brief Forget all loaded state (assembly GDS included) and go empty
     *
     * Unlike clearContext(), this does NOT reload the assembly GDS. Use it when
     * switching to a different assembly so the panel does not keep showing the
     * previous project's layout.
     */
    void reset();

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

    /**
     * @brief User navigated to a wrapper cell in assembly 2D view
     */
    void componentNavigated(const QString& componentId);

private slots:
    void onCellComboChanged(int index);
    void onPositionChanged(double x, double y);
    void onLayoutChanged(bool hasLayout);
    void onLayerItemChanged(QTreeWidgetItem* item, int column);
    void onLayerContextMenu(const QPoint& pos);
    void onLayerFilterChanged(const QString& text);

private:
    void setupUI();
    void updateSidePanels();
    void populateCellCombo();
    void populateLayerList();

    // Nav bar
    QPushButton* m_backButton = nullptr;
    QLabel* m_contextLabel = nullptr;
    QLabel* m_cellNavLabel = nullptr;
    QComboBox* m_cellCombo = nullptr;
    QToolButton* m_layerToggle = nullptr;
    QToolButton* m_hierToggle = nullptr;

    // Central
    KLayout2DView* m_view2d = nullptr;

    // Side panels
    QWidget* m_layerContainer = nullptr;
    QWidget* m_hierContainer = nullptr;
    QSplitter* m_splitter = nullptr;
    QLineEdit* m_layerFilter = nullptr;
    QTreeWidget* m_layerTree = nullptr;

    // Status bar
    QLabel* m_posLabel = nullptr;
    QLabel* m_cellLabel = nullptr;

    // State
    QString m_componentId;
    bool m_blockCellCombo = false;
    bool m_blockLayerSync = false;
    bool m_layerPanelVisible = true;
    bool m_hierPanelVisible = false;

    // Assembly mode state
    PanelMode m_panelMode = PanelMode::Empty;
    QString m_assemblyGdsPath;
    QString m_assemblyLypPath;
    QString m_assemblyTopCell;
    std::unique_ptr<CellComponentMapper> m_cellMapper;
};

} // namespace chiplet

#endif // CHIPLET_UI_DRILLDOWNPANEL_H
