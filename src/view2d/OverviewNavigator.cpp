// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

#include "OverviewNavigator.h"
#include "KLayout2DView.h"

#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPaintEvent>
#include <QResizeEvent>

#include <algorithm>
#include <cmath>

namespace chiplet {

namespace {
constexpr int kPad = 4;            // inner frame padding (pixels)
constexpr int kRubberThreshold = 6; // drag distance that turns a click into a zoom
constexpr double kWheelZoomIn = 0.8;
constexpr double kWheelZoomOut = 1.25;
} // namespace

OverviewNavigator::OverviewNavigator(KLayout2DView* view, QWidget* parent)
    : QWidget(parent)
    , m_view(view)
{
    setFixedSize(240, 190);
    setCursor(Qt::CrossCursor);
    setToolTip(tr("Overview: click to recenter, drag to zoom, wheel to zoom"));
}

OverviewPixelRect OverviewNavigator::drawArea() const
{
    return { static_cast<double>(kPad),
             static_cast<double>(kPad),
             static_cast<double>(width()  - 2 * kPad),
             static_cast<double>(height() - 2 * kPad) };
}

OverviewTransform OverviewNavigator::transform() const
{
    return OverviewTransform(m_fullBox, drawArea());
}

void OverviewNavigator::refreshThumbnail()
{
    m_fullBox = m_view ? m_view->fullBox() : ViewBox{};

    const OverviewPixelRect a = drawArea();
    if (!m_view || !m_fullBox.valid() || a.w <= 0.0 || a.h <= 0.0) {
        m_thumb = QImage();
        return;
    }

    // Render at physical resolution for crispness on HiDPI; the transform works
    // in logical pixels and QPainter scales the image into the logical rect.
    const qreal dpr = devicePixelRatioF();
    const int w = std::max(1, static_cast<int>(std::lround(a.w * dpr)));
    const int h = std::max(1, static_cast<int>(std::lround(a.h * dpr)));
    m_thumb = m_view->renderOverview(w, h);
}

void OverviewNavigator::onContentChanged()
{
    refreshThumbnail();
    update();
}

void OverviewNavigator::onViewportChanged()
{
    update();  // only the viewport rectangle moves; the thumbnail is unchanged
}

void OverviewNavigator::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // Translucent backdrop so the map reads as an overlay on the layout.
    p.fillRect(rect(), QColor(24, 24, 28, 205));

    const OverviewTransform t = transform();

    if (t.valid() && !m_thumb.isNull()) {
        const OverviewPixelRect ir = t.imageRect();
        p.drawImage(QRectF(ir.x, ir.y, ir.w, ir.h), m_thumb);
    } else {
        p.setPen(QColor(150, 150, 150));
        p.drawText(rect(), Qt::AlignCenter, tr("No layout"));
    }

    // Outer frame.
    p.setPen(QColor(110, 110, 120));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(0.5, 0.5, width() - 1.0, height() - 1.0));

    // Current viewport rectangle, clipped to the thumbnail area.
    if (t.valid()) {
        const ViewBox vb = m_view ? m_view->visibleBox() : ViewBox{};
        if (vb.valid()) {
            const OverviewPixelRect vr = t.boxToPixel(vb);
            const OverviewPixelRect ir = t.imageRect();
            QRectF r = QRectF(vr.x, vr.y, vr.w, vr.h)
                           .intersected(QRectF(ir.x, ir.y, ir.w, ir.h));
            if (!r.isEmpty()) {
                p.setPen(QPen(QColor(255, 90, 70), 1.5));
                p.setBrush(QColor(255, 90, 70, 45));
                p.drawRect(r);
            }
        }
    }

    // Zoom rubber-band while dragging.
    if (m_rubber) {
        p.setPen(QPen(QColor(90, 180, 255), 1.0, Qt::DashLine));
        p.setBrush(QColor(90, 180, 255, 30));
        p.drawRect(QRectF(m_pressPos, m_curPos).normalized());
    }
}

void OverviewNavigator::resizeEvent(QResizeEvent* /*event*/)
{
    refreshThumbnail();
}

void OverviewNavigator::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    m_pressed = true;
    m_rubber = false;
    m_pressPos = event->position().toPoint();
    m_curPos = m_pressPos;
}

void OverviewNavigator::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_pressed) {
        return;
    }
    m_curPos = event->position().toPoint();
    if (!m_rubber &&
        (m_curPos - m_pressPos).manhattanLength() > kRubberThreshold) {
        m_rubber = true;
    }
    if (m_rubber) {
        update();
    }
}

void OverviewNavigator::mouseReleaseEvent(QMouseEvent* event)
{
    if (!m_pressed || event->button() != Qt::LeftButton) {
        return;
    }
    m_pressed = false;

    const OverviewTransform t = transform();
    if (!t.valid() || !m_view) {
        m_rubber = false;
        return;
    }

    if (m_rubber) {
        // Zoom to the dragged region.
        const OverviewPoint a = t.pixelToBox(m_pressPos.x(), m_pressPos.y());
        const OverviewPoint b = t.pixelToBox(m_curPos.x(), m_curPos.y());
        ViewBox box{ std::min(a.x, b.x), std::min(a.y, b.y),
                     std::max(a.x, b.x), std::max(a.y, b.y) };
        if (box.valid()) {
            m_view->zoomToBox(box);
        }
    } else {
        // Simple click: recenter, keeping the zoom level.
        const OverviewPoint c = t.pixelToBox(m_pressPos.x(), m_pressPos.y());
        m_view->centerOn(c.x, c.y);
    }
    m_rubber = false;
    update();
}

void OverviewNavigator::wheelEvent(QWheelEvent* event)
{
    const OverviewTransform t = transform();
    if (!t.valid() || !m_view) {
        return;
    }
    const ViewBox vb = m_view->visibleBox();
    if (!vb.valid()) {
        return;
    }

    const double f = event->angleDelta().y() > 0 ? kWheelZoomIn : kWheelZoomOut;
    const QPointF pos = event->position();
    const OverviewPoint c = t.pixelToBox(pos.x(), pos.y());

    // Scale the current viewport about the cursor point.
    ViewBox nb{ c.x + (vb.xmin - c.x) * f, c.y + (vb.ymin - c.y) * f,
                c.x + (vb.xmax - c.x) * f, c.y + (vb.ymax - c.y) * f };
    if (nb.valid()) {
        m_view->zoomToBox(nb);
    }
    event->accept();
}

} // namespace chiplet
