// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SceneMiniMap.h"
#include "AssemblyView.h"

#include <QPainter>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QFont>

#include <algorithm>
#include <cmath>

namespace chiplet {

namespace {
constexpr int    kPad     = 8;    // frame padding (px)
constexpr int    kTitleH  = 15;   // title strip height (px)
const QColor kPanelBg   (20, 22, 28, 205);
const QColor kPanelEdge (255, 255, 255, 40);
const QColor kFloorBg   (38, 42, 50);
const QColor kFloorEdge (255, 255, 255, 30);
const QColor kTitle     (205, 208, 218);
const QColor kSelected  (255, 205, 0);
const QColor kCamera    (255, 90, 70);
}

SceneMiniMap::SceneMiniMap(AssemblyView* view, QWidget* parent)
    : QWidget(parent)
    , m_view(view)
{
    setFixedSize(240, 190);
    setCursor(Qt::CrossCursor);
    setToolTip(tr("Top view: click to recenter, drag to move, wheel to zoom"));
    if (m_view) {
        m_data = m_view->buildSceneOverview();
    }
}

OverviewPixelRect SceneMiniMap::drawArea() const
{
    return { static_cast<double>(kPad),
             static_cast<double>(kPad + kTitleH),
             static_cast<double>(width()  - 2 * kPad),
             static_cast<double>(height() - 2 * kPad - kTitleH) };
}

OverviewTransform SceneMiniMap::transform() const
{
    return OverviewTransform(m_data.floorBounds, drawArea());
}

void SceneMiniMap::onContentChanged()
{
    if (m_view) {
        m_data = m_view->buildSceneOverview();
    }
    update();
}

void SceneMiniMap::onCameraChanged()
{
    if (m_view) {
        m_data.camera = m_view->overviewCameraMarker();
    }
    update();
}

void SceneMiniMap::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Panel
    p.setPen(kPanelEdge);
    p.setBrush(kPanelBg);
    p.drawRoundedRect(QRectF(0.5, 0.5, width() - 1.0, height() - 1.0), 6.0, 6.0);

    // Title
    QFont f = p.font();
    f.setPointSizeF(std::max(7.0, f.pointSizeF() - 1.0));
    p.setFont(f);
    p.setPen(kTitle);
    p.drawText(QRectF(kPad, 3, width() - 2 * kPad, kTitleH),
               Qt::AlignVCenter | Qt::AlignLeft, tr("Top view"));

    const OverviewPixelRect da = drawArea();
    const QRectF daRect(da.x, da.y, da.w, da.h);

    const OverviewTransform xf = transform();
    if (!xf.valid()) {
        p.setPen(QColor(150, 150, 160));
        p.drawText(daRect, Qt::AlignCenter, tr("No assembly"));
        return;
    }

    // The assembly floor extent as a subtle "board" backdrop.
    const OverviewPixelRect ir = xf.imageRect();
    const QRectF imgRect(ir.x, ir.y, ir.w, ir.h);
    p.setPen(kFloorEdge);
    p.setBrush(kFloorBg);
    p.drawRect(imgRect);

    p.save();
    p.setClipRect(imgRect);

    // Component footprints (top-down).
    for (const auto& fp : m_data.footprints) {
        const OverviewPixelRect r = xf.boxToPixel(fp.rect);
        QRectF fr(r.x, r.y, std::max(1.0, r.w), std::max(1.0, r.h));
        const QColor fill(static_cast<int>((fp.rgb >> 16) & 0xFF),
                          static_cast<int>((fp.rgb >> 8) & 0xFF),
                          static_cast<int>(fp.rgb & 0xFF));
        p.setBrush(QColor(fill.red(), fill.green(), fill.blue(), 165));
        if (fp.selected) {
            p.setPen(QPen(kSelected, 2.0));
        } else {
            p.setPen(QPen(fill.lighter(140), 1.0));
        }
        p.drawRect(fr);
    }

    // Camera marker: visible-area box + look-at crosshair + heading.
    const OverviewCameraMarker& cam = m_data.camera;
    if (cam.valid) {
        const ViewBox vb{ cam.x - cam.halfW, cam.y - cam.halfH,
                          cam.x + cam.halfW, cam.y + cam.halfH };
        const OverviewPixelRect vr = xf.boxToPixel(vb);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(kCamera, 1.5));
        p.drawRect(QRectF(vr.x, vr.y, vr.w, vr.h));

        const OverviewPoint c = xf.boxToPixel(cam.x, cam.y);
        p.setPen(QPen(kCamera, 1.5));
        p.drawLine(QPointF(c.x - 5, c.y), QPointF(c.x + 5, c.y));
        p.drawLine(QPointF(c.x, c.y - 5), QPointF(c.x, c.y + 5));
        p.setBrush(kCamera);
        p.drawEllipse(QPointF(c.x, c.y), 1.6, 1.6);

        if (cam.dirValid) {
            // floor direction -> pixel direction (pixel Y is down).
            double dx = cam.dirX;
            double dy = -cam.dirY;
            const double len = std::sqrt(dx * dx + dy * dy);
            if (len > 1e-9) {
                dx /= len; dy /= len;
                const QPointF tip(c.x + dx * 15.0, c.y + dy * 15.0);
                p.setPen(QPen(kCamera, 2.0));
                p.drawLine(QPointF(c.x, c.y), tip);
                // small arrow head
                const double a = std::atan2(dy, dx);
                const double ah = 4.0;
                p.drawLine(tip, QPointF(tip.x() - ah * std::cos(a - 0.5),
                                        tip.y() - ah * std::sin(a - 0.5)));
                p.drawLine(tip, QPointF(tip.x() - ah * std::cos(a + 0.5),
                                        tip.y() - ah * std::sin(a + 0.5)));
            }
        }
    }

    p.restore();
}

void SceneMiniMap::navigateFromPixel(const QPoint& pos)
{
    if (!m_view) {
        return;
    }
    const OverviewTransform xf = transform();
    if (!xf.valid()) {
        return;
    }
    const OverviewPixelRect ir = xf.imageRect();
    const double px = std::clamp(static_cast<double>(pos.x()), ir.x, ir.x + ir.w);
    const double py = std::clamp(static_cast<double>(pos.y()), ir.y, ir.y + ir.h);
    const OverviewPoint fpnt = xf.pixelToBox(px, py);
    m_view->navigateFloorTo(fpnt.x, fpnt.y);
}

void SceneMiniMap::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_pressed = true;
        navigateFromPixel(event->pos());
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void SceneMiniMap::mouseMoveEvent(QMouseEvent* event)
{
    if (m_pressed) {
        navigateFromPixel(event->pos());
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void SceneMiniMap::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_pressed) {
        m_pressed = false;
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void SceneMiniMap::wheelEvent(QWheelEvent* event)
{
    if (!m_view) {
        return;
    }
    // Wheel up -> zoom in (smaller camera distance).
    const double factor = event->angleDelta().y() > 0 ? 0.8 : 1.25;
    m_view->zoomOverview(factor);
    event->accept();
}

} // namespace chiplet
