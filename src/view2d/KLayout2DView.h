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

// Forward declarations (avoid KLayout headers in our header)
namespace lay {
    class LayoutViewWidget;
    class LayoutView;
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
     * @brief Get the layer control frame (for separate dock if needed)
     */
    QWidget* layerControlFrame();

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

private:
    void setupUI();
    void connectSignals();
    bool ensureViewWidget();  // Lazy init, returns true if widget is ready
    static bool isDisplayAvailable();

#ifdef HAVE_KLAYOUT
    lay::LayoutViewWidget* m_viewWidget = nullptr;
#endif
    QString m_currentPath;
    bool m_viewAvailable = false;
    bool m_initAttempted = false;
};

} // namespace chiplet

#endif // CHIPLET_VIEW2D_KLAYOUT2DVIEW_H
