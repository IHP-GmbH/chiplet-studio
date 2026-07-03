// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * OverviewNavigator.h - Corner mini-map (bird's-eye) for the 2D view
 *
 * Shows the whole layout as a thumbnail with a rectangle marking the current
 * viewport, and drives the 2D view on interaction (click to recenter, drag to
 * zoom to a region, wheel to zoom). Modeled on the navigator/overview panel of
 * commercial PCB/IC layout tools.
 *
 * Design: this widget is pure Qt. It talks to the 2D view only through
 * KLayout2DView's KLayout-free API (ViewBox / QImage / doubles) and delegates
 * all coordinate math to OverviewTransform, so no KLayout type ever reaches
 * here. The thumbnail is a static image refreshed on discrete content changes
 * (layout/cell/layer); only the viewport rectangle moves during zoom/pan.
 */

#ifndef CHIPLET_VIEW2D_OVERVIEWNAVIGATOR_H
#define CHIPLET_VIEW2D_OVERVIEWNAVIGATOR_H

#include <QWidget>
#include <QImage>
#include <QPoint>

#include "ViewBox.h"
#include "OverviewTransform.h"

namespace chiplet {

class KLayout2DView;

/**
 * @brief Corner overview / mini-map widget for a KLayout2DView.
 */
class OverviewNavigator : public QWidget {
    Q_OBJECT

public:
    /**
     * @param view The 2D view this navigator mirrors and drives (not owned).
     */
    explicit OverviewNavigator(KLayout2DView* view, QWidget* parent = nullptr);

public slots:
    /// Re-render the thumbnail (layout loaded/cleared, cell or layer change).
    void onContentChanged();

    /// Cheap repaint of the viewport rectangle only (zoom/pan).
    void onViewportChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    /// Inner pixel rectangle the thumbnail is drawn into (minus the frame pad).
    OverviewPixelRect drawArea() const;

    /// Transform for the current full box and draw area (invalid if no layout).
    OverviewTransform transform() const;

    void refreshThumbnail();

    KLayout2DView* m_view = nullptr;

    QImage  m_thumb;        // overview image, sized to the layout aspect ratio
    ViewBox m_fullBox;      // layout extent the thumbnail was rendered for

    bool   m_pressed = false;   // a mouse button is down
    bool   m_rubber = false;    // the drag has become a zoom rubber-band
    QPoint m_pressPos;          // press position (widget pixels)
    QPoint m_curPos;            // current drag position (widget pixels)
};

} // namespace chiplet

#endif // CHIPLET_VIEW2D_OVERVIEWNAVIGATOR_H
