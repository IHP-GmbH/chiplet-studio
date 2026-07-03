// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * KLayout2DView.h - Wrapper widget for KLayout's 2D layout view
 *
 * Embeds lay::LayoutViewWidget with layer panel for 2D GDS visualization.
 * Used for drill-down from 3D view to see component details.
 */

#ifndef CHIPLET_VIEW2D_KLAYOUT2DVIEW_H
#define CHIPLET_VIEW2D_KLAYOUT2DVIEW_H

#include <QWidget>
#include <QString>
#include <QStringList>
#include <QColor>
#include <QImage>
#include <QVBoxLayout>
#include <QVector>
#include <memory>

#include "ViewBox.h"

// Forward declarations (avoid KLayout headers in our header)
namespace lay {
    class LayoutViewWidget;
    class LayoutView;
}

namespace chiplet {
    class EmbeddedDispatcher;
}

namespace chiplet {

/**
 * @brief Information about a single layer in the view
 */
struct LayerInfo {
    QString name;       // Display name (e.g., "Metal1" or "67/0")
    int layer = -1;     // GDS layer number
    int datatype = -1;  // GDS datatype
    bool visible = true;
    QColor fillColor;   // Fill color for display
    QColor frameColor;  // Frame/outline color
    int index = -1;     // Index in the layer list (for setLayerVisible)
};

/**
 * @brief Wrapper widget for KLayout's 2D layout view
 *
 * Provides a Qt widget that embeds KLayout's LayoutViewWidget for viewing
 * GDS/OASIS layout files with layer visibility controls.
 */
class KLayout2DView : public QWidget {
    Q_OBJECT

public:
    explicit KLayout2DView(QWidget* parent = nullptr);
    ~KLayout2DView() override;

    /**
     * @brief Load a GDS/OASIS layout file
     * @param path Path to layout file
     * @param lypPath Optional path to layer properties file (.lyp)
     * @return true if load succeeded
     */
    bool loadLayout(const QString& path, const QString& lypPath = QString());

    /**
     * @brief Clear the current layout
     */
    void clearLayout();

    /**
     * @brief Check if a layout is currently loaded
     */
    bool hasLayout() const;

    /**
     * @brief Zoom to fit the entire layout
     */
    void zoomFit();

    /**
     * @brief Get the current layout path
     */
    QString currentPath() const { return m_currentPath; }

    /**
     * @brief Get the layer control frame widget
     */
    QWidget* layerControlFrame();

    /**
     * @brief Get the hierarchy control frame widget
     */
    QWidget* hierarchyControlFrame();

    // -- Cell navigation --

    /**
     * @brief Get all cell names in the loaded layout
     */
    QStringList cellNames() const;

    /**
     * @brief Get the name of the currently active cell
     */
    QString currentCellName() const;

    /**
     * @brief Set the active cell by name
     * @return true if cell was found and set
     */
    bool setCurrentCell(const QString& name);

    // -- Layer control --

    /**
     * @brief Get number of layer entries in the view
     */
    int layerCount() const;

    /**
     * @brief Set visibility of all layers
     */
    void setAllLayersVisible(bool visible);

    /**
     * @brief Set visibility of a single layer by index
     */
    void setLayerVisible(int index, bool visible);

    /**
     * @brief Get information about all layers in the view
     */
    QVector<LayerInfo> layerInfos() const;

    /**
     * @brief Check if the view is functional (KLayout available and display present)
     */
    bool isViewAvailable() const { return m_viewAvailable; }

    // -- Hierarchy depth control --

    /**
     * @brief Get the maximum hierarchy level currently displayed (-1 if no view)
     */
    int maxHierLevels() const;

    /**
     * @brief Set the maximum hierarchy level displayed (clamped to >= 0)
     *
     * Mirrors KLayout's "Increment/Decrement Hierarchy" actions. Safe no-op when
     * no layout is loaded or the view is unavailable.
     */
    void setMaxHierLevels(int levels);

    // -- Overview / navigator support --
    //
    // A small, KLayout-type-free surface the overview navigator (corner
    // mini-map) drives. All KLayout types stay inside the .cpp; the navigator
    // only ever sees ViewBox / QImage / doubles, which keeps it decoupled and
    // the geometry math unit-testable.

    /**
     * @brief Full extent of the current layout in micrometers (invalid if none)
     */
    ViewBox fullBox() const;

    /**
     * @brief Current visible viewport in micrometers (invalid if no layout)
     */
    ViewBox visibleBox() const;

    /**
     * @brief Render the WHOLE layout to an overview thumbnail
     *
     * Uses KLayout's own renderer (faithful layer colors/styles) at the layout's
     * aspect ratio, capped to fit within maxWidth x maxHeight. Returns a null
     * image when no layout is loaded or the view is unavailable (headless).
     */
    QImage renderOverview(int maxWidth, int maxHeight) const;

    /**
     * @brief Zoom the view to the given box (micrometers)
     */
    void zoomToBox(const ViewBox& box);

    /**
     * @brief Recenter the view on a point (micrometers), keeping the zoom level
     */
    void centerOn(double xUm, double yUm);

signals:
    /**
     * @brief Emitted when mouse position changes in layout coordinates
     */
    void positionChanged(double x, double y);

    /**
     * @brief Emitted when a layout is loaded or cleared
     */
    void layoutChanged(bool hasLayout);

    /**
     * @brief Emitted when the active cell changes
     */
    void cellChanged(const QString& cellName);

    /**
     * @brief Emitted when user navigates to a cell via the hierarchy panel
     *
     * Unlike cellChanged (which fires on programmatic setCurrentCell),
     * this fires from KLayout's cellview_changed_event when the user
     * interacts with the hierarchy panel.
     */
    void cellNavigated(const QString& cellName);

    /**
     * @brief Emitted whenever the visible viewport changes (zoom/pan)
     *
     * Bridged from KLayout's viewport_changed_event. The overview navigator
     * uses it to move its viewport rectangle without re-rendering the thumbnail.
     */
    void viewportChanged();

private:
    void setupUI();
    void connectSignals();
    bool ensureViewWidget();  // Lazy init, returns true if widget is ready
    static bool isDisplayAvailable();

    QVBoxLayout* m_layout = nullptr;

#ifdef HAVE_KLAYOUT
    std::unique_ptr<EmbeddedDispatcher> m_dispatcher;
    lay::LayoutViewWidget* m_viewWidget = nullptr;
    class CellViewEventBridge;
    std::unique_ptr<CellViewEventBridge> m_cellViewBridge;
    class ViewportEventBridge;
    std::unique_ptr<ViewportEventBridge> m_viewportBridge;
#endif
    QString m_currentPath;
    bool m_viewAvailable = false;
    bool m_initAttempted = false;
    bool m_blockCellNavigation = false;
};

} // namespace chiplet

#endif // CHIPLET_VIEW2D_KLAYOUT2DVIEW_H
