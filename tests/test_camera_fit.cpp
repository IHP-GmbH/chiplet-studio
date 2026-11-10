// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>
#include "view3d/Camera.h"
#include "view3d/SceneManager.h"
#include <cmath>
#include <limits>

namespace chiplet {
namespace {
AA_BOUNDING_BOX couponBox()
{
    AA_BOUNDING_BOX box;
    box.SetFromMinsMaxes(VECTOR3D(0, 0, -3.2), VECTOR3D(5, 1.1, 0));
    return box;
}

void expectCornersInside(const Camera& camera, const AA_BOUNDING_BOX& box, float aspect)
{
    const auto matrix = camera.projectionMatrix(aspect) * camera.viewMatrix();
    for (int i = 0; i < 8; ++i) {
        const auto clip = matrix * VECTOR4D(
            (i & 1) ? box.maxes.x : box.mins.x,
            (i & 2) ? box.maxes.y : box.mins.y,
            (i & 4) ? box.maxes.z : box.mins.z, 1);
        ASSERT_TRUE(std::isfinite(clip.w));
        ASSERT_GT(clip.w, 0);
        EXPECT_LE(std::abs(clip.x / clip.w), 1.0);
        EXPECT_LE(std::abs(clip.y / clip.w), 1.0);
        EXPECT_LE(std::abs(clip.z / clip.w), 1.0);
    }
}

void expectSameNavigation(const Camera& camera, const Camera& before)
{
    EXPECT_EQ(camera.target().x, before.target().x);
    EXPECT_EQ(camera.target().y, before.target().y);
    EXPECT_EQ(camera.target().z, before.target().z);
    EXPECT_EQ(camera.distance(), before.distance());
    EXPECT_EQ(camera.yaw(), before.yaw());
    EXPECT_EQ(camera.pitch(), before.pitch());
}
} // namespace

TEST(CameraFit, ActualNarrowPaneAndWideCaptureContainAllCorners)
{
    Camera camera;
    for (float aspect : {316.0f / 929, 1.5f, 3.0f, 0.1f}) {
        camera.fitToBox(couponBox(), aspect);
        expectCornersInside(camera, couponBox(), aspect);
    }
}

TEST(CameraFit, ObliquePerspectiveIncludesBoxDepthAndPreservesOrbit)
{
    Camera camera;
    camera.orbit(113, -120);
    const auto yaw = camera.yaw(), pitch = camera.pitch();
    for (float aspect : {0.2f, 1.0f, 3.0f}) {
        camera.fitToBox(couponBox(), aspect);
        EXPECT_EQ(camera.yaw(), yaw);
        EXPECT_EQ(camera.pitch(), pitch);
        expectCornersInside(camera, couponBox(), aspect);
    }
}

TEST(CameraFit, ResizeFollowsExplicitFitAndCanFitWideAgain)
{
    SceneManager scene;
    scene.setSceneBounds(couponBox());
    scene.fitToScene(1.5f);
    const auto wideDistance = scene.camera().distance();
    scene.camera().setViewportAspect(316.0f / 929);
    EXPECT_GT(scene.camera().distance(), wideDistance);
    expectCornersInside(scene.camera(), couponBox(), 316.0f / 929);
    scene.camera().setViewportAspect(1.5f);
    EXPECT_FLOAT_EQ(scene.camera().distance(), wideDistance);
    expectCornersInside(scene.camera(), couponBox(), 1.5f);
}

TEST(CameraFit, ResizePreservesEachDeliberateNavigationOperation)
{
    for (int operation = 0; operation < 5; ++operation) {
        Camera camera;
        camera.fitToBox(couponBox(), 1.5f);
        switch (operation) {
        case 0: camera.orbit(20, -30); break;
        case 1: camera.pan(15, 8); break;
        case 2: camera.zoom(2); break;
        case 3: camera.setTarget(VECTOR3D(1, 2, 3)); break;
        case 4: camera.setDistance(2); break;
        }
        const Camera before = camera;
        camera.setViewportAspect(0.2f);
        expectSameNavigation(camera, before);
        camera.fitToBox(couponBox(), 0.2f);
        camera.setViewportAspect(0.1f);
        expectCornersInside(camera, couponBox(), 0.1f);
    }
}

TEST(CameraFit, InvalidFitsAndResetCannotPoisonResizeFollowing)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (int invalid = 0; invalid < 5; ++invalid) {
        Camera camera;
        camera.fitToBox(couponBox(), 1.5f);
        auto box = couponBox();
        float aspect = 0.3f;
        if (invalid == 0) box.mins.x = nan;
        if (invalid == 1) box.mins = box.maxes;
        if (invalid == 2) box.mins.x = box.maxes.x + 1;
        if (invalid == 3) aspect = nan;
        if (invalid == 4) aspect = 0;
        camera.fitToBox(box, aspect);
        EXPECT_TRUE(std::isfinite(camera.distance()));
        EXPECT_GT(camera.distance(), 0);
        EXPECT_TRUE(std::isfinite(camera.target().x));
        const Camera before = camera;
        camera.setViewportAspect(0.1f);
        expectSameNavigation(camera, before);
        camera.fitToBox(couponBox(), 1.5f);
        camera.reset();
        const Camera reset = camera;
        camera.setViewportAspect(0.1f);
        expectSameNavigation(camera, reset);
    }
}

TEST(CameraFit, TransientInvalidResizePreservesFitState)
{
    Camera camera;
    camera.fitToBox(couponBox(), 1.5f);
    const Camera before = camera;
    camera.setViewportAspect(0);
    camera.setViewportAspect(std::numeric_limits<float>::infinity());
    expectSameNavigation(camera, before);
    camera.setViewportAspect(0.2f);
    expectCornersInside(camera, couponBox(), 0.2f);
}
} // namespace chiplet
