/**
 * test_dock_width_policy.cpp - initial width for the 2D dock
 *
 * The cases below are the ones the policy was specified with, including the
 * observed failure: a 1280 px window whose 2D dock had taken 856 px and left the
 * 3D viewport at 156. The rule this replaced could only grow, so it computed a
 * target of 512, saw 512 <= 856, and returned having changed nothing while
 * marking itself done.
 */

#include <gtest/gtest.h>
#include "ui/DockWidthPolicy.h"

namespace chiplet {

TEST(DockWidthPolicy, ShrinksADockThatAlreadyStarvedTheViewport) {
    // Observed once: window 1280, dock 856, viewport 156.
    EXPECT_EQ(initial2DDockWidth(1280, 856, 156), 512);
    // And the viewport it leaves behind clears the floor: 156 + (856 - 512).
    EXPECT_EQ(156 + (856 - initial2DDockWidth(1280, 856, 156)), 500);
}

TEST(DockWidthPolicy, GrowsOnlyAsFarAsTheViewportCanAfford) {
    // Two fifths would be 512, but the viewport only has 30 px above the floor.
    EXPECT_EQ(initial2DDockWidth(1280, 400, 350), 430);
    EXPECT_EQ(350 - (430 - 400), kMinCentralWidth);
}

TEST(DockWidthPolicy, ShrinksToRepayAViewportDeficit) {
    EXPECT_EQ(initial2DDockWidth(1280, 400, 200), 280);
    EXPECT_EQ(200 + (400 - 280), kMinCentralWidth);
}

TEST(DockWidthPolicy, AsksForNoChangeWhenTheLayoutAlreadyMatches) {
    EXPECT_EQ(initial2DDockWidth(1280, 512, 320), 512);
}

TEST(DockWidthPolicy, ZeroWhenThereIsNothingToGive) {
    // A window too small to seat the floor at all: the caller leaves it alone
    // rather than asking for a nonsense width.
    EXPECT_EQ(initial2DDockWidth(640, 100, 60), 0);
}

} // namespace chiplet
