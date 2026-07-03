// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * ViewBox.h - Axis-aligned layout box in micrometers
 *
 * Plain value type shared by the 2D view boundary (KLayout2DView) and the
 * overview navigator so neither has to pull KLayout's db::DBox into a header.
 * Keeping this KLayout-free is what lets the navigator and its geometry math
 * stay unit-testable without a display.
 */

#ifndef CHIPLET_VIEW2D_VIEWBOX_H
#define CHIPLET_VIEW2D_VIEWBOX_H

namespace chiplet {

/**
 * @brief Axis-aligned box in layout micrometers.
 */
struct ViewBox {
    double xmin = 0.0;
    double ymin = 0.0;
    double xmax = 0.0;
    double ymax = 0.0;

    double width()  const { return xmax - xmin; }
    double height() const { return ymax - ymin; }

    /// Valid = strictly positive extent in both axes (guards divide-by-zero fits).
    bool valid() const { return xmax > xmin && ymax > ymin; }
};

} // namespace chiplet

#endif // CHIPLET_VIEW2D_VIEWBOX_H
