// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * test_box_mesh.cpp - The box the 3D view draws for a component that is not
 * drawn from its layout.
 *
 * Back faces are culled, so a face shows only where its triangles run
 * counter-clockwise as the camera sees them. Each face of the box must wind
 * that way seen from outside; one wound the other way shows from inside the
 * box instead. An opaque box seen from above then shows its bottom face
 * through a missing top: a Detailed die whose layout did not load, drawn as
 * its box, or a Solid one.
 *
 * Each face is lit by its angle to the scene's light, the same from every
 * camera.
 */

#include <gtest/gtest.h>

#include "core/Assembly.h"
#include "core/Component.h"
#include "view3d/AssemblyView.h"
#include "view3d/MeshBuilder.h"
#include "view3d/SceneManager.h"

#include <QColor>
#include <QImage>
#include <QMatrix4x4>
#include <QTest>
#include <QVector3D>
#include <QVector4D>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace chiplet;

namespace {

QVector3D positionOf(const Vertex& v)
{
    return {v.position[0], v.position[1], v.position[2]};
}

QVector3D normalOf(const Vertex& v)
{
    return {v.normal[0], v.normal[1], v.normal[2]};
}

// "+Y", "-Z", ...: the face a declared normal names.
std::string faceOf(const QVector3D& n)
{
    const char* axis = n.x() != 0.0f ? "X" : n.y() != 0.0f ? "Y" : "Z";
    const float sign = n.x() + n.y() + n.z();
    return std::string(sign > 0.0f ? "+" : "-") + axis;
}

} // namespace

// Every size and offset differs, so no face passes by symmetry.
TEST(BoxMesh, EveryFaceWindsCounterClockwiseFromOutside)
{
    const auto box = MeshBuilder::buildBox(2.0f, 0.3f, 1.5f, 4.0f, 5.0f, 6.0f);
    const QVector3D centre(4.0f, 5.0f, 6.0f - 0.75f);  // the box spans Z [6 - 1.5, 6]
    const auto& vertices = box.vertices();
    const auto& indices = box.indices();
    ASSERT_EQ(indices.size(), 36u);

    for (std::size_t i = 0; i < indices.size(); i += 3) {
        const Vertex& first = vertices[indices[i]];
        const QVector3D a = positionOf(first);
        const QVector3D b = positionOf(vertices[indices[i + 1]]);
        const QVector3D c = positionOf(vertices[indices[i + 2]]);
        const QVector3D normal = normalOf(first);
        const std::string face = faceOf(normal);
        EXPECT_GT(QVector3D::dotProduct(normal, (a + b + c) / 3.0f - centre), 0.0f)
            << "the " << face << " face's normal points into the box";
        EXPECT_GT(QVector3D::dotProduct(QVector3D::crossProduct(b - a, c - a), normal), 0.0f)
            << "triangle " << i / 3 << " of the " << face
            << " face runs clockwise seen from outside";
    }
}

// Two dies of one size and colour, Detailed with no layout to draw, so each is
// drawn as its box, opaque: one face-up and one face-down, seen from above as
// the view opens. Each shows its top face, lit alike: the face-down die's box
// is built in place, not mirrored, so it is culled as the face-up one is.
TEST(BoxMesh, OpaqueBoxesShowTheirTopFaceUpAndFaceDown)
{
    Assembly assembly;
    const std::pair<const char*, Orientation> dies[] = {
        {"U1", Orientation::FaceUp}, {"U2", Orientation::FaceDown}};
    double x = -600.0;
    for (const auto& [id, orientation] : dies) {
        auto die = std::make_unique<Component>(id, ComponentType::Die);
        die->set_dimensions({1000.0, 1000.0, 200.0});
        die->set_position({x, 0.0, 0.0});
        die->set_orientation(orientation);
        die->set_render_mode(RenderMode::DetailedNoSubstrate);
        assembly.add_component(std::move(die));
        x += 1200.0;
    }

    AssemblyView view;
    view.setBasePlaneVisible(false);
    view.setAssembly(&assembly);
    view.resize(400, 300);
    view.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&view));
    const QImage frame = view.grabFramebuffer();

    // Along the middle row, the boxes are the runs that are not background.
    const int y = frame.height() / 2;
    const QRgb background = frame.pixel(0, y);
    std::vector<std::pair<int, int>> runs;
    for (int px = 0; px < frame.width(); ++px) {
        if (frame.pixel(px, y) == background) continue;
        if (runs.empty() || runs.back().second != px - 1) {
            runs.emplace_back(px, px);
        } else {
            runs.back().second = px;
        }
    }
    ASSERT_EQ(runs.size(), 2u) << "boxes along the middle row";
    const QColor one = frame.pixelColor((runs[0].first + runs[0].second) / 2, y);
    const QColor two = frame.pixelColor((runs[1].first + runs[1].second) / 2, y);

    // A die is blue 204. Ambient light alone gives 0.3 of it, about 61: what
    // a bottom face seen from inside the box shows, its normal turned away.
    EXPECT_GT(std::min(one.blue(), two.blue()), 71)
        << "blue " << one.blue() << " and " << two.blue() << ": a box shows its inside";
    const int differ = std::abs(one.red() - two.red()) + std::abs(one.green() - two.green())
                       + std::abs(one.blue() - two.blue());
    EXPECT_LE(differ, 3) << "the boxes differ: " << one.name().toStdString() << " and "
                         << two.name().toStdString();
}

namespace {

QMatrix4x4 qMatrix(const MATRIX4X4& m)
{
    QMatrix4x4 out;
    for (int i = 0; i < 16; ++i) out.data()[i] = m.GetEntry(i);
    return out;
}

// The pixel a scene point lands on, from the frame's top left.
QPoint pixelOf(const Camera& camera, const QImage& frame, const QVector3D& point)
{
    const float aspect = float(frame.width()) / float(frame.height());
    const QVector4D clip = qMatrix(camera.projectionMatrix(aspect))
                           * qMatrix(camera.viewMatrix()) * QVector4D(point, 1.0f);
    return QPoint(int((clip.x() / clip.w() + 1.0f) * 0.5f * frame.width()),
                  int((1.0f - clip.y() / clip.w()) * 0.5f * frame.height()));
}

} // namespace

// The light is fixed in the scene, from above as SceneManager sets it, not to
// the camera. The shaders light a face in view space, so the light they are
// given must be the scene's turned by the view: a face then shows one shade
// from every camera, set by its angle to the light. One Solid die, thick so
// its sides show, over the base plane, seen as the view opens and from low on
// two sides. At each camera the die's top, the side facing the eye and the
// plane are read where their centres land.
TEST(BoxMesh, EachFaceIsLitByItsAngleToTheSceneNotToTheCamera)
{
    Assembly assembly;
    auto die = std::make_unique<Component>("U1", ComponentType::Die);
    die->set_dimensions({2000.0, 2000.0, 1000.0});
    die->set_position({0.0, 0.0, 0.0});
    die->set_render_mode(RenderMode::Solid);
    assembly.add_component(std::move(die));

    AssemblyView view;
    view.setAssembly(&assembly);
    view.resize(400, 300);
    view.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&view));
    ASSERT_TRUE(view.basePlaneVisible());

    const AA_BOUNDING_BOX bounds = view.sceneBounds();
    const QVector3D lo(bounds.mins.x, bounds.mins.y, bounds.mins.z);
    const QVector3D hi(bounds.maxes.x, bounds.maxes.y, bounds.maxes.z);
    const QVector3D mid = (lo + hi) / 2.0f;
    // The shaders' lighting: 0.3 ambient, and 0.6 of the cosine to the light.
    const VECTOR3D light = SceneManager().lightDirection();
    const QVector3D toLight = -QVector3D(light.x, light.y, light.z);
    const auto lit = [&](const QVector3D& normal) {
        return 0.3f + 0.6f * std::max(QVector3D::dotProduct(normal, toLight), 0.0f);
    };
    const QVector3D up(0.0f, 1.0f, 0.0f);
    const int blue = MeshBuilder::colorForComponentType(ComponentType::Die).blue();
    const float grey = 0.95f * 255.0f;  // renderGrid's plane

    struct Look {
        const char* name;
        float yaw;
        float pitch;
        QVector3D side;  // the side facing the eye, if one shows
    };
    const Look looks[] = {{"as the view opens, from above", 0.0f, 89.0f, {}},
                          {"low, from +X", 90.0f, 30.0f, {1.0f, 0.0f, 0.0f}},
                          {"low, from +Z", 0.0f, 30.0f, {0.0f, 0.0f, 1.0f}}};
    for (const Look& look : looks) {
        SCOPED_TRACE(look.name);
        Camera camera = view.camera();
        camera.setTarget(VECTOR3D(mid.x(), mid.y(), mid.z()));
        camera.setDistance(10.0f);
        camera.setOrientation(look.yaw, look.pitch);
        view.setCamera(camera);
        const QImage frame = view.grabFramebuffer();

        const QColor top = frame.pixelColor(pixelOf(camera, frame, {mid.x(), hi.y(), mid.z()}));
        EXPECT_NEAR(top.blue(), blue * lit(up), 2) << "the die's top";
        if (!look.side.isNull()) {
            const QVector3D centre = mid + look.side * (hi - lo) / 2.0f;
            const QColor side = frame.pixelColor(pixelOf(camera, frame, centre));
            EXPECT_NEAR(side.blue(), blue * lit(look.side), 2) << "the side facing the eye";
        }
        // The plane lies 0.1 below the lowest component (updateBasePlane).
        const QVector3D onPlane(mid.x() - 3.0f, lo.y() - 0.1f, mid.z() + 3.0f);
        const QColor plane = frame.pixelColor(pixelOf(camera, frame, onPlane));
        EXPECT_NEAR(plane.red(), grey * lit(up), 2) << "the base plane";
    }
}
