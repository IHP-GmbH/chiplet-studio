// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later
//
// OverviewTransform is the geometry core of the 2D overview navigator (the
// corner mini-map). It maps layout micrometers to overview pixels and back:
// this is what makes the viewport rectangle land on the rendered layout and
// click-to-navigate hit the right coordinate. The math is pure (no Qt, no
// KLayout), so it is fully testable without a display -- unlike the widget's
// paint path, which is display-only (verified on real hardware GL).

#include <gtest/gtest.h>
#include "view2d/OverviewTransform.h"

using namespace chiplet;

TEST(OverviewTransform, InvalidWhenBoxDegenerate)
{
    ViewBox box;  // zero extent
    OverviewTransform t(box, { 0, 0, 200, 200 });
    EXPECT_FALSE(t.valid());
}

TEST(OverviewTransform, InvalidWhenAreaEmpty)
{
    ViewBox box{ 0, 0, 10, 10 };
    OverviewTransform t(box, { 0, 0, 0, 0 });
    EXPECT_FALSE(t.valid());
}

// A square layout in a square area fills it edge to edge, and the layout origin
// (xmin, ymin) is bottom-left in micrometers -> bottom-left in pixels (Y down).
TEST(OverviewTransform, SquareBoxFillsSquareArea)
{
    ViewBox box{ 0, 0, 10, 10 };
    OverviewTransform t(box, { 0, 0, 200, 200 });
    ASSERT_TRUE(t.valid());

    const auto r = t.imageRect();
    EXPECT_DOUBLE_EQ(r.x, 0.0);
    EXPECT_DOUBLE_EQ(r.y, 0.0);
    EXPECT_DOUBLE_EQ(r.w, 200.0);
    EXPECT_DOUBLE_EQ(r.h, 200.0);

    const auto bl = t.boxToPixel(0.0, 0.0);
    EXPECT_DOUBLE_EQ(bl.x, 0.0);
    EXPECT_DOUBLE_EQ(bl.y, 200.0);  // ymin -> bottom of the pixel rect

    const auto tr = t.boxToPixel(10.0, 10.0);
    EXPECT_DOUBLE_EQ(tr.x, 200.0);
    EXPECT_DOUBLE_EQ(tr.y, 0.0);    // ymax -> top of the pixel rect
}

// A wide layout is letterboxed vertically: full width, reduced height, centered.
TEST(OverviewTransform, WideBoxLetterboxedVertically)
{
    ViewBox box{ 0, 0, 20, 10 };            // 2:1
    OverviewTransform t(box, { 0, 0, 200, 200 });
    ASSERT_TRUE(t.valid());
    EXPECT_DOUBLE_EQ(t.scale(), 10.0);

    const auto r = t.imageRect();
    EXPECT_DOUBLE_EQ(r.w, 200.0);
    EXPECT_DOUBLE_EQ(r.h, 100.0);
    EXPECT_DOUBLE_EQ(r.x, 0.0);
    EXPECT_DOUBLE_EQ(r.y, 50.0);            // (200 - 100) / 2
}

// The full box maps exactly onto the image rectangle -> the viewport rectangle
// equals the thumbnail bounds when the whole layout is in view.
TEST(OverviewTransform, ViewportBoxMapsToImageRectWhenFull)
{
    ViewBox box{ -5, -5, 15, 25 };          // 20 x 30, offset origin
    OverviewTransform t(box, { 10, 10, 120, 240 });
    ASSERT_TRUE(t.valid());

    const auto full = t.boxToPixel(box);
    const auto img = t.imageRect();
    EXPECT_NEAR(full.x, img.x, 1e-9);
    EXPECT_NEAR(full.y, img.y, 1e-9);
    EXPECT_NEAR(full.w, img.w, 1e-9);
    EXPECT_NEAR(full.h, img.h, 1e-9);
}

// pixel -> um -> pixel round-trips for a non-square layout in a square area.
TEST(OverviewTransform, RoundTripPixelUm)
{
    ViewBox box{ 100, 200, 900, 1200 };
    OverviewTransform t(box, { 0, 0, 256, 256 });
    ASSERT_TRUE(t.valid());

    const double xs[] = { 123.4, 500.0, 850.0 };
    const double ys[] = { 250.0, 700.0, 1150.0 };
    for (double x : xs) {
        for (double y : ys) {
            const auto px = t.boxToPixel(x, y);
            const auto um = t.pixelToBox(px.x, px.y);
            EXPECT_NEAR(um.x, x, 1e-6);
            EXPECT_NEAR(um.y, y, 1e-6);
        }
    }
}

// The draw area may be offset inside the widget (frame padding); the origin
// translates with it.
TEST(OverviewTransform, OffsetAreaTranslatesOrigin)
{
    ViewBox box{ 0, 0, 10, 10 };
    OverviewTransform t(box, { 30, 40, 100, 100 });
    ASSERT_TRUE(t.valid());

    const auto bl = t.boxToPixel(0.0, 0.0);
    EXPECT_DOUBLE_EQ(bl.x, 30.0);
    EXPECT_DOUBLE_EQ(bl.y, 140.0);  // 40 + 100 (ymin at the bottom of the area)
}
