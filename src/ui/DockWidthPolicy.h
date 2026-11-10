// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * DockWidthPolicy.h - initial width for the 2D dock, as arithmetic
 *
 * The 2D dock and the 3D viewport share one row with the left dock, so the
 * dock's opening width is not a preference: it decides how much viewport is
 * left. It has to be able to move in both directions. The case that sent us
 * here was a dock that opened too WIDE, leaving a 156 px viewport in a 1280 px
 * window, and a rule that only ever grows cannot correct that.
 *
 * MainWindow is not in the test target, so the arithmetic lives here where it
 * can be checked directly against the cases it was specified with.
 */

#ifndef CHIPLET_UI_DOCKWIDTHPOLICY_H
#define CHIPLET_UI_DOCKWIDTHPOLICY_H

namespace chiplet {

/// The 3D viewport we refuse to squeeze below when the 2D dock first opens.
constexpr int kMinCentralWidth = 320;

/**
 * Width to ask for the 2D dock the first time it opens.
 *
 * @param windowWidth   the whole window
 * @param dockWidth     what the dock currently occupies
 * @param centralWidth  what the 3D viewport currently occupies
 * @param minCentral    the viewport floor
 *
 * The dock may occupy its own width plus whatever the viewport has above the
 * floor; that term is NEGATIVE when the viewport is already below it, which is
 * how a shrink comes out of the same expression as a grow. Two fifths of the
 * window when that fits, less when it does not. A zero result means there is
 * nothing to give and the caller should leave the layout alone.
 */
constexpr int initial2DDockWidth(int windowWidth, int dockWidth, int centralWidth,
                                 int minCentral = kMinCentralWidth)
{
    const int available = dockWidth + (centralWidth - minCentral);
    const int room = available > 0 ? available : 0;
    const int desired = windowWidth * 2 / 5;
    return desired < room ? desired : room;
}

} // namespace chiplet

#endif // CHIPLET_UI_DOCKWIDTHPOLICY_H
