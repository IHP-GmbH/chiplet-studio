// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * test_box_placement.cpp - Where the 3D view and its top view draw a component
 * whose file names no layout, or one that does not read.
 *
 * Such a component is drawn as its `dimensions` box. `position` places the
 * box by the component's anchor (coord_frame_contract.md section 2):
 * bbox_center puts the box's centre there, and gds_origin puts the layout's
 * GDS (0, 0) there. With no layout to read, where (0, 0) sits inside the box
 * is not known; the view takes the lower-left corner of the box, the
 * convention gds_to_kicad writes and the exported-draft placement test
 * (test_gds_placement.cpp, ExportedDraftFlipChipU1) holds, mirrored in X for a
 * face-down die as the layout path mirrors it, and says so in a note.
 *
 * Every box below is wider than it is high or the other way round, and no
 * two heights are equal, so a box placed by an edge on one axis and by its
 * centre on the other cannot pass, and neither can a shift that is the same
 * for every component.
 *
 * Expected values are written as the floor rectangle the top view draws:
 * x and y in mm of the chiplet frame (floorRectFromSceneXZ, the conversion
 * the top view itself uses).
 */

#include <gtest/gtest.h>

#include "core/Assembly.h"
#include "core/Component.h"
#include "view3d/AssemblyView.h"
#include "view3d/GDSLayerExtractor.h"
#include "view3d/SceneLoad.h"
#include "view3d/SceneOverview.h"

#include <QImage>
#include <QString>
#include <QStringList>
#include <QTest>

#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace chiplet;

namespace {

constexpr double kTolMm = 1e-4;  // 0.1 um

struct Placed {
    std::string id;
    ComponentType type;
    Anchor anchor;
    Orientation orientation;
    double x, y, z;        // position, um
    double w, h, t;        // dimensions, um
    std::string layout;    // empty: the file names none
};

void add(Assembly& assembly, const Placed& p)
{
    auto comp = std::make_unique<Component>(p.id, p.type);
    comp->set_anchor(p.anchor);
    comp->set_orientation(p.orientation);
    comp->set_position({p.x, p.y, p.z});
    comp->set_dimensions({p.w, p.h, p.t});
    if (!p.layout.empty()) {
        comp->set_layout_path(p.layout);
        comp->set_top_cell("TOP");
    }
    if (p.type == ComponentType::Interposer) {
        comp->set_attachment_surface_z(p.t);
    }
    assembly.add_component(std::move(comp));
}

struct Drawn {
    std::map<std::string, ViewBox> floor;  // by component id
    std::vector<std::string> notes;
};

// The boxes the scene load builds, as the top view reads them.
Drawn drawn(const Assembly& assembly)
{
    Drawn out;
    const BoxMeshes boxes = buildBoxMeshes(assembly, {});
    for (const auto& [id, mesh] : boxes.meshes) {
        const AA_BOUNDING_BOX& b = mesh.boundingBox();
        out.floor[id.toStdString()] =
            floorRectFromSceneXZ(b.mins.x, b.mins.z, b.maxes.x, b.maxes.z);
    }
    out.notes = boxes.notes;
    return out;
}

void expectRect(const std::map<std::string, ViewBox>& floor, const std::string& id,
                double xmin, double xmax, double ymin, double ymax)
{
    SCOPED_TRACE(id);
    const auto it = floor.find(id);
    ASSERT_NE(it, floor.end()) << "no box drawn";
    const ViewBox& r = it->second;
    EXPECT_NEAR(r.xmin, xmin, kTolMm) << "left edge, x";
    EXPECT_NEAR(r.xmax, xmax, kTolMm) << "right edge, x";
    EXPECT_NEAR(r.ymin, ymin, kTolMm) << "bottom edge, y";
    EXPECT_NEAR(r.ymax, ymax, kTolMm) << "top edge, y";
}

// The design of the report this test comes from, with every die made
// non-square: an interposer taller than wide, a face-down die taller than
// wide and a face-up die wider than tall, all anchored at their bbox centre.
const Placed kInterposer{"interposer", ComponentType::Interposer, Anchor::BboxCenter,
                         Orientation::FaceUp, 1000, 1750, 0, 2000, 3500, 10.8103, ""};
const Placed kU1Centre{"U1", ComponentType::Die, Anchor::BboxCenter, Orientation::FaceDown,
                       1000, 2500, 54.8103, 600, 1000, 200, ""};
const Placed kU2Centre{"U2", ComponentType::Die, Anchor::BboxCenter, Orientation::FaceUp,
                       500, 1000, 54.8103, 800, 400, 200, ""};

const char* const kNoLayoutOne =
    "3D: U2 is anchored at its GDS origin, but the file names no layout for it, so "
    "where that origin sits is assumed: the lower-left corner of its box (the "
    "lower-right corner, seen from above, for a face-down die). Name a layout, or use "
    "anchor: bbox_center, to place it exactly.";

} // namespace

// bbox_center: each box's centre is its position, in x and in y. On the old
// path every such box sat half its own height too high in y while x was
// right, so the dies sat 1250 um low against a 3500 um interposer.
TEST(BoxPlacement, ABboxCentreBoxIsCentredOnItsPositionInBothAxes)
{
    Assembly assembly;
    add(assembly, kInterposer);
    add(assembly, kU1Centre);
    add(assembly, kU2Centre);

    const Drawn d = drawn(assembly);
    expectRect(d.floor, "interposer", 0.0, 2.0, 0.0, 3.5);
    expectRect(d.floor, "U1", 0.7, 1.3, 2.0, 3.0);
    expectRect(d.floor, "U2", 0.1, 0.9, 0.8, 1.2);
    EXPECT_TRUE(d.notes.empty()) << "a bbox_center box is placed exactly: " << d.notes.front();
}

// gds_origin with no layout: the origin is the box's lower-left corner, the
// lower-right seen from above for a face-down die. One note names both.
TEST(BoxPlacement, AGdsOriginBoxWithNoLayoutHasItsOriginAtItsLowerLeftAndSaysSo)
{
    Assembly assembly;
    add(assembly, {"U1", ComponentType::Die, Anchor::GdsOrigin, Orientation::FaceDown,
                   1500, 2000, 0, 600, 1000, 200, ""});
    add(assembly, {"U2", ComponentType::Die, Anchor::GdsOrigin, Orientation::FaceUp,
                   200, 500, 0, 800, 400, 200, ""});

    const Drawn d = drawn(assembly);
    expectRect(d.floor, "U1", 0.9, 1.5, 2.0, 3.0);
    expectRect(d.floor, "U2", 0.2, 1.0, 0.5, 0.9);
    const std::vector<std::string> expected{
        "3D: U1, U2 are anchored at their GDS origin, but the file names no layout for "
        "them, so where that origin sits is assumed: the lower-left corner of each box "
        "(the lower-right corner, seen from above, for a face-down die). Name a layout, "
        "or use anchor: bbox_center, to place them exactly."};
    EXPECT_EQ(d.notes, expected);
}

// A layout that does not read leaves the same box at the same assumed corner,
// and the note says the layout could not be read, not that there is none.
// One of each case, so each note is in its singular form.
TEST(BoxPlacement, AGdsOriginBoxWhoseLayoutDoesNotReadSaysItCouldNotBeRead)
{
    Assembly assembly;
    add(assembly, {"U3", ComponentType::Die, Anchor::GdsOrigin, Orientation::FaceUp,
                   0, 0, 0, 500, 300, 200, ""});
    add(assembly, {"U4", ComponentType::Die, Anchor::GdsOrigin, Orientation::FaceDown,
                   1000, 0, 0, 400, 200, 200, "/nonexistent/box_placement/U4.gds"});

    const Drawn d = drawn(assembly);
    expectRect(d.floor, "U3", 0.0, 0.5, 0.0, 0.3);
    expectRect(d.floor, "U4", 0.6, 1.0, 0.0, 0.2);
    const std::vector<std::string> expected{
        "3D: U3 is anchored at its GDS origin, but the file names no layout for it, so "
        "where that origin sits is assumed: the lower-left corner of its box (the "
        "lower-right corner, seen from above, for a face-down die). Name a layout, or use "
        "anchor: bbox_center, to place it exactly.",
        "3D: U4 is anchored at its GDS origin, but its layout could not be read, so where "
        "that origin sits is assumed: the lower-left corner of its box (the lower-right "
        "corner, seen from above, for a face-down die). Fix the layout path or the top "
        "cell, or use anchor: bbox_center, to place it exactly."};
    EXPECT_EQ(d.notes, expected);
}

// Two unreadable layouts share one note in its plural form; a bbox_center box
// whose layout does not read is still exact, so it is neither named nor moved.
TEST(BoxPlacement, UnreadableLayoutsShareOneNoteAndABboxCentreOneIsNotNamed)
{
    Assembly assembly;
    add(assembly, {"U5", ComponentType::Die, Anchor::GdsOrigin, Orientation::FaceUp,
                   0, 0, 0, 500, 300, 200, "/nonexistent/box_placement/U5.gds"});
    add(assembly, {"U6", ComponentType::Die, Anchor::GdsOrigin, Orientation::FaceDown,
                   1000, 0, 0, 400, 200, 200, "/nonexistent/box_placement/U6.gds"});
    add(assembly, {"U7", ComponentType::Die, Anchor::BboxCenter, Orientation::FaceUp,
                   2000, 1000, 0, 300, 700, 200, "/nonexistent/box_placement/U7.gds"});

    const Drawn d = drawn(assembly);
    expectRect(d.floor, "U5", 0.0, 0.5, 0.0, 0.3);
    expectRect(d.floor, "U6", 0.6, 1.0, 0.0, 0.2);
    expectRect(d.floor, "U7", 1.85, 2.15, 0.65, 1.35);
    const std::vector<std::string> expected{
        "3D: U5, U6 are anchored at their GDS origin, but their layouts could not be read, "
        "so where that origin sits is assumed: the lower-left corner of each box (the "
        "lower-right corner, seen from above, for a face-down die). Fix the layout paths "
        "or the top cells, or use anchor: bbox_center, to place them exactly."};
    EXPECT_EQ(d.notes, expected);
}

// A layout that reads gives the footprint, placed the same way: gds_origin
// puts the layout's (0, 0) at the position (mirrored in X face down), and
// bbox_center its bbox centre. Nothing is assumed, so nothing is noted. The
// expected rectangles come from the fixture's own bbox; what is checked is
// where that bbox is drawn, not how it is read.
TEST(BoxPlacement, AReadableLayoutIsPlacedByItsOwnBboxWithNoNote)
{
    const std::string gds = std::string(FIXTURES_DIR) + "/sample_minimal.gds";
    const GDSBoundingBox bbox = GDSLayerExtractor::extractBoundingBox(gds, "");
    ASSERT_TRUE(bbox.is_valid()) << "the in-repo fixture did not read: " << gds;
    // A bbox whose origin is its centre would not tell the anchors apart.
    ASSERT_FALSE(bbox.x_min == -bbox.x_max && bbox.y_min == -bbox.y_max);

    Assembly assembly;
    auto addLaid = [&](const char* id, Anchor anchor, Orientation orientation,
                       double x, double y) {
        auto comp = std::make_unique<Component>(id, ComponentType::Die);
        comp->set_anchor(anchor);
        comp->set_orientation(orientation);
        comp->set_position({x, y, 0.0});
        comp->set_dimensions({1.0, 1.0, 200.0});  // the layout's bbox wins
        comp->set_layout_path(gds);
        assembly.add_component(std::move(comp));
    };
    addLaid("L1", Anchor::GdsOrigin, Orientation::FaceDown, 5000, 2000);
    addLaid("L2", Anchor::BboxCenter, Orientation::FaceUp, 9000, 500);

    const Drawn d = drawn(assembly);
    expectRect(d.floor, "L1", (5000 - bbox.x_max) / 1000, (5000 - bbox.x_min) / 1000,
               (2000 + bbox.y_min) / 1000, (2000 + bbox.y_max) / 1000);
    expectRect(d.floor, "L2", (9000 - bbox.width() / 2) / 1000, (9000 + bbox.width() / 2) / 1000,
               (500 - bbox.height() / 2) / 1000, (500 + bbox.height() / 2) / 1000);
    EXPECT_TRUE(d.notes.empty()) << d.notes.front();
}

// The view itself: its top view draws each component where the file places
// it, and its notes strip carries the assumed-origin note.
TEST(BoxPlacement, TheTopViewDrawsEachBoxWhereTheFilePlacesItAndTheStripSaysWhatWasAssumed)
{
    Assembly assembly;
    add(assembly, kInterposer);
    add(assembly, kU1Centre);
    add(assembly, {"U2", ComponentType::Die, Anchor::GdsOrigin, Orientation::FaceUp,
                   200, 500, 54.8103, 800, 400, 200, ""});

    AssemblyView view;
    view.setAssembly(&assembly);
    view.resize(400, 300);
    view.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&view));
    (void)view.grabFramebuffer();  // a frame, so the meshes are built

    std::map<std::string, ViewBox> floor;
    for (const OverviewFootprint& fp : view.buildSceneOverview().footprints) {
        floor[fp.id] = fp.rect;
    }
    ASSERT_EQ(floor.size(), 3u);
    expectRect(floor, "interposer", 0.0, 2.0, 0.0, 3.5);
    expectRect(floor, "U1", 0.7, 1.3, 2.0, 3.0);
    expectRect(floor, "U2", 0.2, 1.0, 0.5, 0.9);

    const QStringList notes = view.notes();
    EXPECT_EQ(notes.count(QString::fromUtf8(kNoLayoutOne)), 1)
        << notes.join('\n').toStdString();
}
