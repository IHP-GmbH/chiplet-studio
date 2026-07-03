// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * OverviewTransform.h - Micrometer <-> overview-pixel mapping
 *
 * Fits the full layout box into a pixel rectangle preserving aspect ratio
 * (letterboxed, centered) with the Y axis flipped (layout Y is up, pixel Y is
 * down). It is the SINGLE source of truth for both where the overview thumbnail
 * image is drawn and where the viewport rectangle lands, so the two can never
 * drift apart.
 *
 * Pure value type: no Qt, no KLayout -> fully unit-testable headless. The GL/
 * paint side (OverviewNavigator) is display-only; the correctness that matters
 * (click-to-navigate hits the right coordinate, the viewport rect aligns with
 * the rendered layout) lives here and is covered by ctest.
 */

#ifndef CHIPLET_VIEW2D_OVERVIEWTRANSFORM_H
#define CHIPLET_VIEW2D_OVERVIEWTRANSFORM_H

#include "ViewBox.h"

namespace chiplet {

/// Pixel rectangle (x, y = top-left; w, h = size). Kept Qt-free on purpose.
struct OverviewPixelRect {
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;
};

/// A point in pixels or micrometers depending on context.
struct OverviewPoint {
    double x = 0.0;
    double y = 0.0;
};

class OverviewTransform {
public:
    OverviewTransform() = default;

    /**
     * @param full The layout extent in micrometers (from KLayout full_box()).
     * @param area The pixel rectangle the thumbnail is drawn into.
     *
     * Invalid (valid() == false) when either input is degenerate; callers must
     * check before using the mapping.
     */
    OverviewTransform(const ViewBox& full, const OverviewPixelRect& area)
    {
        if (!full.valid() || area.w <= 0.0 || area.h <= 0.0) {
            return;
        }
        const double sx = area.w / full.width();
        const double sy = area.h / full.height();
        m_scale = sx < sy ? sx : sy;              // fit: smaller scale, no crop
        m_drawnW = full.width()  * m_scale;
        m_drawnH = full.height() * m_scale;
        m_offX = area.x + (area.w - m_drawnW) * 0.5;  // center (letterbox)
        m_offY = area.y + (area.h - m_drawnH) * 0.5;
        m_full = full;
        m_valid = true;
    }

    bool   valid() const { return m_valid; }
    double scale() const { return m_scale; }

    /// Pixel rectangle the thumbnail image occupies inside the draw area.
    OverviewPixelRect imageRect() const {
        return { m_offX, m_offY, m_drawnW, m_drawnH };
    }

    /// Micrometer point -> pixel point (Y flipped).
    OverviewPoint boxToPixel(double xUm, double yUm) const {
        return { m_offX + (xUm - m_full.xmin) * m_scale,
                 m_offY + (m_full.ymax - yUm) * m_scale };
    }

    /// Micrometer box -> pixel rectangle (normalized so w, h >= 0).
    OverviewPixelRect boxToPixel(const ViewBox& b) const {
        const OverviewPoint tl = boxToPixel(b.xmin, b.ymax);  // upper-left
        const OverviewPoint br = boxToPixel(b.xmax, b.ymin);  // lower-right
        return { tl.x, tl.y, br.x - tl.x, br.y - tl.y };
    }

    /// Pixel point -> micrometer point (Y flipped).
    OverviewPoint pixelToBox(double px, double py) const {
        if (!m_valid) {
            return {};
        }
        return { m_full.xmin + (px - m_offX) / m_scale,
                 m_full.ymax - (py - m_offY) / m_scale };
    }

private:
    ViewBox m_full;
    double  m_scale = 0.0;
    double  m_drawnW = 0.0;
    double  m_drawnH = 0.0;
    double  m_offX = 0.0;
    double  m_offY = 0.0;
    bool    m_valid = false;
};

} // namespace chiplet

#endif // CHIPLET_VIEW2D_OVERVIEWTRANSFORM_H
