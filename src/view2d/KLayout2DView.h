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
#include <QVBoxLayout>
#include <memory>

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
     * @brief Check if the view is functional (KLayout available and display present)
     */
    bool isViewAvailable() const { return m_viewAvailable; }

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

private:
    void setupUI();
    void connectSignals();
    bool ensureViewWidget();  // Lazy init, returns true if widget is ready
    static bool isDisplayAvailable();

    QVBoxLayout* m_layout = nullptr;

#ifdef HAVE_KLAYOUT
    std::unique_ptr<EmbeddedDispatcher> m_dispatcher;
    lay::LayoutViewWidget* m_viewWidget = nullptr;
#endif
    QString m_currentPath;
    bool m_viewAvailable = false;
    bool m_initAttempted = false;
};

} // namespace chiplet

#endif // CHIPLET_VIEW2D_KLAYOUT2DVIEW_H
