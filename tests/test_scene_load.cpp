// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * test_scene_load.cpp - The 3D view's CPU loading, without GL
 *
 * The standard view is the default and must not move: a generated layout
 * that runs into each of its limits gives the counts, rows and notes below,
 * written out by hand from the limits, not read back from a run. The fast
 * view reads every shape of the layers the standard view places, at the
 * standard view's height, and its tiles land where the standard meshes do.
 * A datatype with no stackup entry is meshed with every shape and all the
 * area its GDS layer holds, read back from the file by KLayout.
 * studio-viewcount's checks pass on that layout with each setting and
 * camera, and fail when a gap loses its note. On the interposer's
 * technology the bumps are drawn on the file's plane, where the dies seat.
 * One test draws a fast frame through GL and counts what GL received. One
 * shows AssemblyView itself: its Filter rebuild keeps the bumps on that plane.
 */

#include <gtest/gtest.h>

#include "core/Assembly.h"
#include "core/Component.h"
#include "core/LayerStackup.h"
#include "core/TechResolve.h"
#include "formats/ChipletFormat.h"
#include "view3d/AssemblyView.h"
#include "view3d/FastShapesGL.h"
#include "view3d/GDSLayerExtractor.h"
#include "view3d/GdsPlacement.h"
#include "view3d/LayerMeshBuilder.h"
#include "view3d/SceneLoad.h"
#include "view3d/ShapeTiles.h"
#include "view3d/TileChoice.h"
#include "view3d/ViewCount.h"

#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>
#include <QVector3D>
#include <QVector4D>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#ifdef HAVE_KLAYOUT
#include "dbBox.h"
#include "dbCell.h"
#include "dbCellInst.h"
#include "dbLayerProperties.h"
#include "dbLayout.h"
#include "dbPath.h"
#include "dbPolygon.h"
#include "dbReader.h"
#include "dbRecursiveShapeIterator.h"
#include "dbText.h"
#include "dbWriter.h"
#include "tlStream.h"
#endif

using namespace chiplet;

TEST(StandardPin, TheLimitsAreTheOnesTheDefaultViewHad)
{
    LayerStackup stackup;
    stackup.addLayer(8, 0, 0.0, 1.0, "Metal1");
    stackup.addLayer(19, 0, 1.0, 1.0, "Via1");
    stackup.addLayer(6, 0, -1.0, 1.0, "Cont");
    const ExtractionConfig config = standardExtractionConfig(stackup);
    EXPECT_EQ(config.max_polygons_per_layer, 10000u);
    EXPECT_DOUBLE_EQ(config.min_polygon_area, 0.1);
    EXPECT_EQ(config.via_max_cuts, 50000u);
    EXPECT_EQ(config.via_layers, (std::set<LayerKey>{LayerKey(6, 0), LayerKey(19, 0)}));
    EXPECT_EQ(config.modeled_layers,
              (std::set<LayerKey>{LayerKey(6, 0), LayerKey(8, 0), LayerKey(19, 0)}));
    EXPECT_EQ(LayerMeshBuilder().maxPolygonsPerLayer(), 50000u);
}

TEST(FastPlacement, TheModelPutsAGdsPointWhereTheStandardMeshHasIt)
{
    // Standard's vertex for GDS (x, y) at height z: ((x - cx) mm, z mm,
    // -(y - cy) mm), then the component transform.
    QMatrix4x4 transform;
    transform.translate(1.5f, 0.2f, -2.0f);
    transform.scale(-1.0f, 1.0f, 1.0f);
    const QVector2D centre(0.25f, -0.5f);  // mm
    const float spread = 0.03f;
    const auto m = fastLayerModel(transform, centre, spread);
    const float x = 400.0f, y = 120.0f, z = 7.0f;  // um
    const QVector3D want = transform.map(
        QVector3D((x / 1000.0f - centre.x()), z / 1000.0f + spread, -(y / 1000.0f - centre.y())));
    const float got[3] = {
        m[0] * x + m[4] * y + m[8] * z + m[12],
        m[1] * x + m[5] * y + m[9] * z + m[13],
        m[2] * x + m[6] * y + m[10] * z + m[14],
    };
    EXPECT_NEAR(got[0], want.x(), 1e-6);
    EXPECT_NEAR(got[1], want.y(), 1e-6);
    EXPECT_NEAR(got[2], want.z(), 1e-6);
    EXPECT_FLOAT_EQ(m[3], 0.0f);
    EXPECT_FLOAT_EQ(m[15], 1.0f);
}

namespace {

FastLayer fastLayerAt(int layer, double zBottom, double thickness, const std::string& name,
                      std::size_t inGds = 0)
{
    FastLayer l;
    l.key = LayerKey(layer, 0);
    l.place.name = name;
    l.place.zBottom = zBottom;
    l.place.thickness = thickness;
    l.tiles.inGds = inGds;
    return l;
}

// World Y of the GDS point (0, 0, z um) under a column-major model.
float worldY(const std::array<float, 16>& m, float z)
{
    return m[9] * z + m[13];
}

}  // namespace

TEST(FastFrame, TheSpreadMovesAFastLayerAsTheStandardViewMovesItsMesh)
{
    // The standard view spreads a mesh up by z_bottom (mm) * (factor - 1).
    // A layer at 5 um, spread by 3: 0.005 mm * 2 = 0.010 mm up, so its
    // bottom is at 0.015 mm instead of 0.005 mm.
    FastComponent fast;
    fast.componentId = "U1";
    fast.layers.push_back(fastLayerAt(8, 5.0, 1.0, "Metal1"));
    Component3DGeometry geometry;
    const auto shown = [](const std::string&, const FastLayer&) { return true; };

    const FastFrame flat = fastFrame({{&fast, &geometry, true, false}}, shown, 1.0f);
    const FastFrame spread = fastFrame({{&fast, &geometry, true, false}}, shown, 3.0f);
    ASSERT_EQ(flat.inputs.size(), 1u);
    ASSERT_EQ(spread.inputs.size(), 1u);
    EXPECT_NEAR(worldY(flat.inputs[0].model, 5.0f), 0.005f, 1e-7);
    EXPECT_NEAR(worldY(spread.inputs[0].model, 5.0f), 0.015f, 1e-7);
}

TEST(FastFrame, LayersSharingATopTakeTurnsAndAHiddenLayerStaysInTheFrame)
{
    // A drawing layer and its fill share a top: ranks 0 and 1. The via on
    // top starts again at 0. A hidden layer stays in the frame so its shapes
    // land in the hidden bucket; a part not shown, and a hidden substrate,
    // show nothing.
    FastComponent fast;
    fast.componentId = "U1";
    fast.layers.push_back(fastLayerAt(8, 0.0, 1.0, "Metal1"));
    fast.layers.push_back(fastLayerAt(9, 0.0, 1.0, "Metal1.filler"));
    fast.layers.push_back(fastLayerAt(19, 1.0, 1.0, "Via1"));
    fast.layers.push_back(fastLayerAt(41, -2.0, 2.0, "Substrate"));
    Component3DGeometry geometry;
    const auto notFill = [](const std::string& id, const FastLayer& l) {
        return id == "U1" && l.key != LayerKey(9, 0);
    };

    const FastFrame f = fastFrame({{&fast, &geometry, true, true}}, notFill, 1.0f);
    ASSERT_EQ(f.inputs.size(), 4u);
    EXPECT_EQ(f.coverRanks, (std::vector<int>{0, 1, 0, 0}));
    EXPECT_EQ(f.refs[1].componentId, "U1");
    EXPECT_EQ(f.refs[1].layer, &fast.layers[1]);
    EXPECT_TRUE(f.inputs[0].visible);
    EXPECT_FALSE(f.inputs[1].visible);
    EXPECT_TRUE(f.inputs[2].visible);
    EXPECT_FALSE(f.inputs[3].visible);
    EXPECT_EQ(f.inputs[2].tiles, &fast.layers[2].tiles);
    EXPECT_FLOAT_EQ(f.inputs[2].zBottom, 1.0f);
    EXPECT_FLOAT_EQ(f.inputs[2].zTop, 2.0f);

    const FastFrame off = fastFrame({{&fast, &geometry, false, false}}, notFill, 1.0f);
    ASSERT_EQ(off.inputs.size(), 4u);
    for (const TileLayerInput& in : off.inputs) EXPECT_FALSE(in.visible);
}

TEST(FastRows, APreparedLayerIsCountedAndCanBeSwitched)
{
    // Standard drew part of Metal1 and none of Via1 (no checkbox); the body
    // is the standard view's. With Fast both prepared layers say their
    // buckets and get a checkbox; a layer the camera has not settled on yet
    // says no count rather than Standard's.
    std::vector<LayerRow> standard(3);
    standard[0].key = LayerKey(8, 0);
    standard[0].name = "Metal1";
    standard[0].count = "3D shows 10,000 of 12,000 shapes";
    standard[0].countNote = "standard note";
    standard[1].key = LayerKey(40, 0);
    standard[1].name = "Substrate";
    standard[2].key = LayerKey(19, 0);
    standard[2].name = "Via1";
    standard[2].meshed = false;
    standard[2].count = "3D shows 0 of 50,001 shapes";

    FastComponent fast;
    fast.componentId = "U1";
    fast.layers.push_back(fastLayerAt(8, 0.0, 1.0, "Metal1", 12000));
    fast.layers.push_back(fastLayerAt(19, 1.0, 1.0, "Via1", 50001));
    fast.layers.push_back(fastLayerAt(20, 3.0, 1.0, "Via2", 100));
    LayerBuckets metal;
    metal.shape = 100;
    metal.coverage = 11900;
    LayerBuckets via2;
    via2.shape = 100;

    const std::vector<LayerRow> rows = fastLayerRows(
        standard, fast, {{LayerKey(8, 0), metal}, {LayerKey(20, 0), via2}});
    ASSERT_EQ(rows.size(), 4u);
    EXPECT_EQ(rows[0].count,
              "12,000 shapes; at this zoom 100 drawn as shapes, 11,900 as coverage, 0 outside "
              "the view");
    EXPECT_EQ(rows[0].countNote,
              "Coverage shows where the layer's shapes are, not their outlines. Zoom in, or "
              "choose Every shape.");
    EXPECT_TRUE(rows[0].meshed);
    EXPECT_EQ(rows[1].name, "Substrate");
    EXPECT_TRUE(rows[1].count.empty());
    EXPECT_TRUE(rows[1].meshed);
    EXPECT_EQ(rows[2].name, "Via1");
    EXPECT_TRUE(rows[2].meshed);
    EXPECT_TRUE(rows[2].count.empty());
    EXPECT_TRUE(rows[2].countNote.empty());
    // A prepared layer with no standard row is listed after them.
    EXPECT_EQ(rows[3].key, LayerKey(20, 0));
    EXPECT_EQ(rows[3].name, "Via2");
    EXPECT_EQ(rows[3].count, "100 shapes, all drawn");
    EXPECT_TRUE(rows[3].countNote.empty());
    EXPECT_TRUE(rows[3].meshed);
}

TEST(ViewNotes, EachSettingSaysWhatIsDrawn)
{
    Assembly assembly;
    assembly.add_component(std::make_unique<Component>("U1", ComponentType::Die));
    const std::vector<std::string> sheets{"sheet note"};
    const std::map<QString, std::string> unplaced{{QStringLiteral("U1"), "unplaced note"}};
    ShapeCount count;
    count.key = LayerKey(8, 0);
    count.name = "Metal1";
    count.inGds = 12000;
    count.drawn = 10000;
    count.pastRead = 2000;
    count.readLimit = 10000;
    const std::map<QString, std::vector<ShapeCount>> counts{{QStringLiteral("U1"), {count}}};
    std::map<QString, FastComponent> fast;
    fast[QStringLiteral("U1")].notes = {"prepared note"};
    const std::map<std::string, std::string> coverage{{"U1", "coverage note"}};

    const std::vector<std::string> standard = standardNotes(assembly, sheets, unplaced, counts);
    ASSERT_EQ(standard.size(), 3u);  // the sheet, the unplaced layers, Metal1's count
    auto notes = [&](ShapeMode mode, bool prepared, double seconds) {
        return viewNotes({mode, prepared, seconds}, assembly, sheets, unplaced, counts, fast,
                         coverage);
    };

    EXPECT_EQ(notes(ShapeMode::Standard, false, -1.0), standard);
    EXPECT_EQ(notes(ShapeMode::Standard, true, 2.0), standard);

    std::vector<std::string> reading{
        "3D: Fast", "3D: reading every shape for Fast; the standard view is shown until then."};
    reading.insert(reading.end(), standard.begin(), standard.end());
    EXPECT_EQ(notes(ShapeMode::Fast, false, -1.0), reading);

    const std::vector<std::string> fastLines{"3D: Fast", "sheet note", "unplaced note",
                                             "prepared note", "coverage note"};
    EXPECT_EQ(notes(ShapeMode::Fast, true, -1.0), fastLines);
    EXPECT_EQ(notes(ShapeMode::Fast, true, 4.12), fastLines);

    const std::vector<std::string> every = notes(ShapeMode::Every, true, 4.12);
    ASSERT_EQ(every.size(), 5u);
    EXPECT_EQ(every[0], "3D: Every shape; the last frame took 4.1 s.");
    EXPECT_EQ(notes(ShapeMode::Every, true, -1.0)[0], "3D: Every shape");
    EXPECT_EQ(notes(ShapeMode::Every, false, -1.0)[1],
              "3D: reading every shape for Every shape; the standard view is shown until "
              "then.");
}

namespace {

// One plain layer, and a fill layer when asked for.
Component3DGeometry layersWith(bool fill)
{
    Component3DGeometry g;
    g.layers.emplace_back();
    g.layers.back().key = LayerKey(8, 0);
    if (fill) {
        g.layers.emplace_back();
        g.layers.back().key = LayerKey(8, 22);
        g.layers.back().fill = true;
    }
    return g;
}

UndetailedPart part(const std::string& id, bool fill, bool faceDown = false)
{
    UndetailedPart p;
    p.componentId = id;
    p.fill = fill;
    p.faceDown = faceDown;
    return p;
}

const std::string kU1Note = "3D: U1 is not drawn Detailed, so its layers, fill and 3D Shapes do "
                            "not show. Right-click it in Hierarchy, Render Mode, Detailed.";

}  // namespace

// Fill on and no die Detailed: each die with fill is named, and a face-down
// one is told that Detailed (no Si bulk) shows its layers from above.
TEST(DetailNote, FillOnNamesEachDieWithFill)
{
    EXPECT_EQ(detailNote(ShapeMode::Standard, true, {part("U1", true, true), part("U2", true, true)}),
              "3D: U1, U2 are not drawn Detailed, so their layers, fill and 3D Shapes do not "
              "show. Right-click them in Hierarchy, Render Mode, Detailed; a face-down die "
              "needs Detailed (no Si bulk) to show them from above.");
    EXPECT_EQ(detailNote(ShapeMode::Standard, true, {part("U1", true)}), kU1Note);
}

// As the view opens, fill off and Standard, there is nothing hidden to name.
TEST(DetailNote, FillOffAndStandardSaysNothing)
{
    EXPECT_EQ(detailNote(ShapeMode::Standard, false, {part("U1", true), part("U2", true)}), "");
}

// Fast and Every shape draw shapes only for a Detailed part, so every part is
// named, with fill or without, whatever the fill switch says.
TEST(DetailNote, FastAndEveryNameEveryPart)
{
    for (ShapeMode mode : {ShapeMode::Fast, ShapeMode::Every}) {
        for (bool fill : {false, true}) {
            EXPECT_EQ(detailNote(mode, fill, {part("interposer", false), part("U1", true)}),
                      "3D: interposer, U1 are not drawn Detailed, so their layers, fill and 3D "
                      "Shapes do not show. Right-click them in Hierarchy, Render Mode, Detailed.");
        }
    }
}

// Fill on under Standard hides nothing on a part without fill.
TEST(DetailNote, FillOnLeavesOutAPartWithoutFill)
{
    EXPECT_EQ(detailNote(ShapeMode::Standard, true, {part("interposer", false)}), "");
    EXPECT_EQ(detailNote(ShapeMode::Standard, true, {part("interposer", false), part("U1", true)}),
              kU1Note);
}

// The parts are the components with layers that are neither Detailed nor
// Hidden, in assembly order.
TEST(DetailNote, ADetailedComponentIsNotAPart)
{
    Assembly assembly;
    assembly.add_component(std::make_unique<Component>("interposer", ComponentType::Interposer));
    for (const char* id : {"U1", "U2", "U3", "U4", "U5"}) {
        assembly.add_component(std::make_unique<Component>(id, ComponentType::Die));
    }
    assembly.component("U1")->set_render_mode(RenderMode::Detailed);
    assembly.component("U2")->set_orientation(Orientation::FaceDown);
    assembly.component("U3")->set_render_mode(RenderMode::DetailedNoSubstrate);
    assembly.component("U4")->set_render_mode(RenderMode::Hidden);
    std::map<QString, Component3DGeometry> geometry;  // U5 has no layers
    geometry[QStringLiteral("interposer")] = layersWith(false);
    for (const char* id : {"U1", "U2", "U3", "U4"}) {
        geometry[QString::fromLatin1(id)] = layersWith(true);
    }

    const std::vector<UndetailedPart> parts = undetailedParts(assembly, geometry);
    ASSERT_EQ(parts.size(), 2u);
    EXPECT_EQ(parts[0].componentId, "interposer");
    EXPECT_FALSE(parts[0].fill);
    EXPECT_FALSE(parts[0].faceDown);
    EXPECT_EQ(parts[1].componentId, "U2");
    EXPECT_TRUE(parts[1].fill);
    EXPECT_TRUE(parts[1].faceDown);
}

// The detail note leads the strip whichever way it is made, and Every shape's
// timed first line does not overwrite it.
TEST(ViewNotes, TheDetailNoteLeadsEachSetting)
{
    Assembly assembly;
    assembly.add_component(std::make_unique<Component>("U1", ComponentType::Die));
    const std::vector<std::string> sheets{"sheet note"};
    auto notes = [&](ShapeMode mode, bool prepared, double seconds, bool fill) {
        ViewNotesState state{mode, prepared, seconds};
        state.showFill = fill;
        state.undetailed = {part("U1", true)};
        return viewNotes(state, assembly, sheets, {}, {}, {}, {});
    };
    using Lines = std::vector<std::string>;

    EXPECT_EQ(notes(ShapeMode::Standard, false, -1.0, false), Lines{"sheet note"});
    EXPECT_EQ(notes(ShapeMode::Standard, false, -1.0, true), (Lines{kU1Note, "sheet note"}));
    EXPECT_EQ(notes(ShapeMode::Fast, false, -1.0, false),
              (Lines{kU1Note, "3D: Fast",
                     "3D: reading every shape for Fast; the standard view is shown until then.",
                     "sheet note"}));
    EXPECT_EQ(notes(ShapeMode::Every, true, 4.12, false),
              (Lines{kU1Note, "3D: Every shape; the last frame took 4.1 s.", "sheet note"}));
}

// Standard never draws a fill layer's mesh, which holds the first shapes read,
// so the strip drops that layer's count note. With fill on it says the fill
// is being read, then what is drawn: tiles, and what the memory limit left
// out of them.
TEST(ViewNotes, StandardSaysFillIsDrawnAsTilesNotAsTheFirstShapesRead)
{
    Assembly assembly;
    assembly.add_component(std::make_unique<Component>("U1", ComponentType::Die));
    ShapeCount metal;
    metal.key = LayerKey(8, 0);
    metal.name = "Metal1";
    metal.inGds = 12000;
    metal.drawn = 10000;
    metal.pastRead = 2000;
    metal.readLimit = 10000;
    ShapeCount fill = metal;
    fill.key = LayerKey(8, 22);
    fill.name = "Metal1Filler";
    fill.fill = true;
    const std::map<QString, std::vector<ShapeCount>> counts{{QStringLiteral("U1"), {metal, fill}}};
    const std::string metalNote = shapeCountNote("U1", metal);
    ASSERT_FALSE(metalNote.empty());
    ASSERT_FALSE(shapeCountNote("U1", fill).empty());  // what Standard said of it before

    FastLayer read = fastLayerAt(8, 0.0, 1.0, "Metal1Filler", 12000);
    read.key = LayerKey(8, 22);
    read.place.fill = true;
    read.tiles.shapes = 12000;
    std::map<QString, FastComponent> fast;
    fast[QStringLiteral("U1")].layers.push_back(read);
    const std::string tiles = fillCoverageNote("U1", 1, 12000);
    const std::map<std::string, std::string> coverage{{"U1", tiles}};
    auto notes = [&](bool showFill, bool fillPrepared) {
        ViewNotesState state{ShapeMode::Standard, false, -1.0};
        state.showFill = showFill;
        state.fillPrepared = fillPrepared;
        return viewNotes(state, assembly, {}, {}, counts, fast, coverage);
    };
    using Lines = std::vector<std::string>;

    EXPECT_EQ(standardNotes(assembly, {}, {}, counts), Lines{metalNote});
    EXPECT_EQ(notes(false, false), Lines{metalNote});
    EXPECT_EQ(notes(false, true), Lines{metalNote});
    EXPECT_EQ(notes(true, false), (Lines{metalNote, fillReadingNote()}));
    EXPECT_EQ(notes(true, true), (Lines{metalNote, tiles}));

    // A fill layer the memory limit prepared in part says so before the tiles.
    fast[QStringLiteral("U1")].layers[0].tiles.shapes = 11000;
    const std::string part = preparedNote("U1", 8, 22, "Metal1Filler", 11000, 12000);
    ASSERT_FALSE(part.empty());
    EXPECT_EQ(notes(true, true), (Lines{metalNote, part, tiles}));

    EXPECT_EQ(fillReadingNote(), "3D: reading every fill shape; fill is drawn once it is read.");
    EXPECT_EQ(tiles, "3D: U1 draws 1 fill layer as hatched coverage tiles (12,000 shapes in "
                     "view). Choose Every shape to draw each shape.");
    EXPECT_EQ(fillCoverageNote("U2", 4, 1605895),
              "3D: U2 draws 4 fill layers as hatched coverage tiles (1,605,895 shapes in "
              "view). Choose Every shape to draw each shape.");
    EXPECT_TRUE(fillCoverageNote("U1", 0, 0).empty());
}

namespace {

// The resolver's built-in rung reads the configs dir; pin it and put the
// default back.
struct ConfigsDir {
    explicit ConfigsDir(const std::string& dir) { BlenderGDSConfigs::setConfigsDir(dir); }
    ~ConfigsDir() { BlenderGDSConfigs::setConfigsDir(""); }
    ConfigsDir(const ConfigsDir&) = delete;
    ConfigsDir& operator=(const ConfigsDir&) = delete;
};

// Snapshot and restore one environment variable.
class EnvGuard {
public:
    explicit EnvGuard(const char* name) : m_name(name)
    {
        if (const char* v = std::getenv(name)) {
            m_had = true;
            m_value = v;
        }
    }
    ~EnvGuard()
    {
        if (m_had) setenv(m_name.c_str(), m_value.c_str(), 1);
        else unsetenv(m_name.c_str());
    }
    EnvGuard(const EnvGuard&) = delete;
    EnvGuard& operator=(const EnvGuard&) = delete;

private:
    std::string m_name, m_value;
    bool m_had = false;
};

}  // namespace

// The stackup the standard view draws the interposer's technology at puts the
// bumps on the file's plane when the interposer declares one, so they meet the
// die where it seats.
TEST(SceneLoad, TheViewDrawsTheBumpsOnTheFilesPlane)
{
    EnvGuard env("INTERCONNECT_PDK_ROOT");
    const std::string base = std::string(FIXTURES_DIR) + "/interconnect_merge";
    ConfigsDir configs(base + "/configs");
    setenv("INTERCONNECT_PDK_ROOT", (base + "/pdk").c_str(), 1);

    ChipletFormat format;
    auto assembly = format.load(base + "/merge_z.chiplet");
    ASSERT_NE(assembly, nullptr);
    Component* interposer = assembly->component("interposer");
    ASSERT_NE(interposer, nullptr);

    // No plane in the file: the stackup's.
    RenderStackup drawn = resolveRenderStackup(*assembly, "intm4tm2", nullptr);
    ASSERT_TRUE(drawn.modeled);
    ASSERT_NE(drawn.stackup.find(500, 35), nullptr);
    EXPECT_EQ(drawn.stackup.find(500, 35)->z_bottom, 13.83);

    interposer->set_attachment_surface_z(10.8103);
    drawn = resolveRenderStackup(*assembly, "intm4tm2", nullptr);
    const LayerElevation* pillar = drawn.stackup.find(500, 35);
    const LayerElevation* cap = drawn.stackup.find(501, 35);
    ASSERT_NE(pillar, nullptr);
    ASSERT_NE(cap, nullptr);
    EXPECT_EQ(pillar->z_bottom, 10.8103);
    EXPECT_EQ(assembly->calculate_component_z("die_a"), 58.8103);
    EXPECT_DOUBLE_EQ(cap->z_top(), assembly->calculate_component_z("die_a"));
}

#ifdef HAVE_KLAYOUT

namespace {

db::Coord dbu(double um)
{
    return static_cast<db::Coord>(std::llround(um / 0.001));
}

// `n` squares of `size` um, `perRow` to a row at `pitch` um, from y0.
void addGrid(db::Cell& cell, unsigned int li, int n, int perRow, double size, double pitch,
             double y0)
{
    for (int i = 0; i < n; ++i) {
        const double x = (i % perRow) * pitch;
        const double y = y0 + (i / perRow) * pitch;
        cell.shapes(li).insert(db::Box(dbu(x), dbu(y), dbu(x + size), dbu(y + size)));
    }
}

void writeLayout(const db::Layout& layout, const std::string& path)
{
    tl::OutputStream stream(path);
    db::SaveLayoutOptions options;
    db::Writer writer(options);
    writer.write(const_cast<db::Layout&>(layout), stream);
}

// A die that runs into every limit the standard view can reach:
//   8/0  Metal1  12,000 squares: 2,000 past the read limit of 10,000
//   8/2          3 squares, placed at Metal1's height by its layer number
//   10/0 Metal2  30 squares, and 50 under the 0.1 um² floor
//   19/0 Via1    50,001 cuts: a via layer over 50,000 is not drawn
//   20/0 Via2    100 cuts, all drawn
//   30/0 Pad     20 squares all under the floor: drawn as cuts (backstop)
//   40/0         one shape; the stackup's substrate is a full-extent body
//   99/0         5 squares, not in the stackup
// The mesh limit (50,000 per layer) cannot be reached: the read limit and
// the via limit both stop a layer before it.
void writeLimitsDie(const std::string& path)
{
    db::Layout ly;
    ly.dbu(0.001);
    db::Cell& top = ly.cell(ly.add_cell("TOP"));
    auto layer = [&](int l, int d) { return ly.insert_layer(db::LayerProperties(l, d)); };
    const unsigned int m1 = layer(8, 0);
    addGrid(top, m1, 12000, 100, 1.0, 2.0, 0.0);
    top.shapes(m1).insert(db::Text("label", db::Trans()));
    addGrid(top, layer(8, 2), 3, 3, 1.0, 2.0, 250.0);
    const unsigned int m2 = layer(10, 0);
    addGrid(top, m2, 30, 30, 1.0, 2.0, 260.0);
    addGrid(top, m2, 50, 50, 0.2, 2.0, 262.0);
    addGrid(top, layer(19, 0), 50001, 400, 0.2, 0.5, 300.0);
    addGrid(top, layer(20, 0), 100, 100, 0.2, 0.5, 370.0);
    addGrid(top, layer(30, 0), 20, 20, 0.2, 2.0, 372.0);
    addGrid(top, layer(40, 0), 1, 1, 5.0, 5.0, 380.0);
    addGrid(top, layer(99, 0), 5, 5, 1.0, 2.0, 390.0);
    writeLayout(ly, path);
}

RenderStackup limitsStackup()
{
    RenderStackup render;
    render.modeled = true;
    render.label = "pin";
    LayerStackup& s = render.stackup;
    s.addLayer(40, 0, -10.0, 10.0, "Substrate");
    s.addLayer(8, 0, 0.0, 1.0, "Metal1");
    s.addLayer(19, 0, 1.0, 1.0, "Via1");
    s.addLayer(10, 0, 2.0, 1.0, "Metal2");
    s.addLayer(20, 0, 3.0, 1.0, "Via2");
    s.addLayer(30, 0, 4.0, 1.0, "Pad");
    return render;
}

Component limitsComponent(const std::string& path, Orientation orientation)
{
    Component comp("U1", ComponentType::Die);
    comp.set_layout_path(path);
    comp.set_top_cell("TOP");
    comp.set_orientation(orientation);
    comp.set_anchor(Anchor::BboxCenter);
    comp.set_position(Position3D{1000.0, 2000.0, 0.0});
    return comp;
}

const ShapeCount* countOf(const std::vector<ShapeCount>& counts, int layer, int datatype)
{
    for (const auto& c : counts) {
        if (c.key == LayerKey(layer, datatype)) return &c;
    }
    return nullptr;
}

const LayerMesh* meshOf(const Component3DGeometry& g, int layer, int datatype)
{
    for (const auto& m : g.layers) {
        if (m.key == LayerKey(layer, datatype)) return &m;
    }
    return nullptr;
}

// The area of a mesh's upward faces in um² (build() draws in mm).
double topFaceArea(const LayerMesh& m)
{
    const std::vector<Vertex>& v = m.mesh.vertices();
    const std::vector<GLuint>& idx = m.mesh.indices();
    double a = 0;
    for (std::size_t i = 0; i + 2 < idx.size(); i += 3) {
        const Vertex& p = v[idx[i]];
        const Vertex& q = v[idx[i + 1]];
        const Vertex& r = v[idx[i + 2]];
        if (p.normal[1] != 1.0f || q.normal[1] != 1.0f || r.normal[1] != 1.0f) continue;
        const double ux = q.position[0] - p.position[0], uz = q.position[2] - p.position[2];
        const double wx = r.position[0] - p.position[0], wz = r.position[2] - p.position[2];
        a += std::abs(ux * wz - uz * wx) / 2.0;
    }
    return a * 1e6;
}

struct GdsLayer {
    std::size_t shapes = 0;
    double area = 0;  // um²
};

// What cell `top` of the file at `path` holds on l/d, read by KLayout alone.
GdsLayer readGdsLayer(const std::string& path, const char* top, int l, int d)
{
    db::Layout ly;
    tl::InputStream stream(path);
    db::Reader reader(stream);
    reader.read(ly);
    GdsLayer out;
    const auto cell = ly.cell_by_name(top);
    const int li = ly.get_layer_maybe(db::LayerProperties(l, d));
    if (!cell.first || li < 0) return out;
    for (db::RecursiveShapeIterator it(ly, ly.cell(cell.second), unsigned(li)); !it.at_end();
         ++it) {
        db::Polygon p;
        it.shape().polygon(p);
        ++out.shapes;
        out.area += std::abs(double(p.area())) * ly.dbu() * ly.dbu();
    }
    return out;
}

double recordArea(const LayerRecords& r)
{
    double a = 0;
    for (const TileBox& b : r.boxes) a += double(b.x1 - b.x0) * (b.y1 - b.y0);
    for (const TileTrap& t : r.traps) {
        a += double(t.yt - t.yb) * 0.5 * ((t.xbr - t.xbl) + (t.xtr - t.xtl));
    }
    return a;
}

}  // namespace

TEST(StandardPin, ALayoutAtEveryLimitGivesTheSameCountsRowsAndNotes)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::string path = dir.filePath("limits.gds").toStdString();
    writeLimitsDie(path);

    const Component comp = limitsComponent(path, Orientation::FaceUp);
    auto loaded = loadStandardComponent(comp, nullptr, limitsStackup(), 0.0);
    ASSERT_TRUE(loaded.has_value());
    const auto& counts = loaded->counts;

    // Every layer 3D draws is counted at the height it is drawn at: 8/2 has
    // no entry of its own and is counted at Metal1's. 99/0 is placed nowhere.
    ASSERT_EQ(counts.size(), 7u);
    const ShapeCount* pin = countOf(counts, 8, 2);
    ASSERT_NE(pin, nullptr);
    EXPECT_TRUE(pin->borrowed);
    EXPECT_EQ(pin->name, "Metal1");
    EXPECT_EQ(pin->inGds, 3u);
    EXPECT_EQ(pin->drawn, 3u);
    struct Want {
        int layer;
        std::size_t inGds, drawn, underArea, pastRead, viaSkipped;
    };
    for (const Want& w : {Want{8, 12000, 10000, 0, 2000, 0}, Want{10, 80, 30, 50, 0, 0},
                          Want{19, 50001, 0, 0, 0, 50001}, Want{20, 100, 100, 0, 0, 0},
                          Want{30, 20, 20, 0, 0, 0}, Want{40, 1, 1, 0, 0, 0}}) {
        const ShapeCount* c = countOf(counts, w.layer, 0);
        ASSERT_NE(c, nullptr) << w.layer;
        EXPECT_EQ(c->inGds, w.inGds) << w.layer;
        EXPECT_EQ(c->drawn, w.drawn) << w.layer;
        EXPECT_EQ(c->underArea, w.underArea) << w.layer;
        EXPECT_EQ(c->pastRead, w.pastRead) << w.layer;
        EXPECT_EQ(c->viaSkipped, w.viaSkipped) << w.layer;
        EXPECT_EQ(c->pastMesh, 0u) << w.layer;
    }

    std::vector<std::string> notes;
    for (const ShapeCount& c : counts) {
        if (std::string n = shapeCountNote("U1", c); !n.empty()) notes.push_back(n);
    }
    EXPECT_EQ(notes, (std::vector<std::string>{
        "3D: U1 8/0 Metal1: 10,000 of 12,000 shapes shown; 2,000 past the read limit of "
        "10,000 per layer.",
        "3D: U1 10/0 Metal2: 30 of 80 shapes shown; 50 under 0.1 um² skipped.",
        "3D: U1 19/0 Via1: 0 of 50,001 shapes shown; a via layer over 50,000 cuts is not "
        "drawn.",
    }));
    EXPECT_EQ(loaded->note, "3D: U1 has shapes on 99/0, not in the pin stackup, not drawn.");

    // The meshes: every placed layer with something read, the substrate as
    // a body, and nothing for the dropped via layer.
    const Component3DGeometry& g = loaded->geometry;
    std::vector<LayerKey> meshed;
    for (const LayerMesh& m : g.layers) meshed.push_back(m.key);
    EXPECT_EQ(std::set<LayerKey>(meshed.begin(), meshed.end()),
              (std::set<LayerKey>{LayerKey(8, 0), LayerKey(8, 2), LayerKey(10, 0),
                                  LayerKey(20, 0), LayerKey(30, 0), LayerKey(40, 0)}));
    EXPECT_EQ(meshOf(g, 8, 0)->shapes, 10000u);
    EXPECT_EQ(meshOf(g, 8, 2)->shapes, 3u);
    EXPECT_EQ(meshOf(g, 8, 2)->z_bottom, meshOf(g, 8, 0)->z_bottom);
    EXPECT_EQ(meshOf(g, 40, 0)->shapes, 1u);

    const auto rows = layerRows("U1", g, counts);
    std::map<LayerKey, std::string> rowCount;
    for (const LayerRow& r : rows) rowCount[r.key] = r.count;
    EXPECT_EQ(rowCount.at(LayerKey(8, 0)), "3D shows 10,000 of 12,000 shapes");
    EXPECT_EQ(rowCount.at(LayerKey(10, 0)), "3D shows 30 of 80 shapes");
    EXPECT_EQ(rowCount.at(LayerKey(19, 0)), "3D shows 0 of 50,001 shapes");
    EXPECT_EQ(rowCount.at(LayerKey(20, 0)), "");
    EXPECT_EQ(rowCount.at(LayerKey(8, 2)), "");
}

// The mesh of a datatype with no stackup entry holds every shape of its GDS
// layer and the same area, compared with the file as KLayout reads it, not
// with any count. 126/0 has its own entry and is held to the same.
TEST(StandardPin, ADatatypeWithNoEntryIsMeshedWithEveryShapeOfItsGdsLayer)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::string path = dir.filePath("borrowed.gds").toStdString();
    {
        db::Layout ly;
        ly.dbu(0.001);
        db::Cell& top = ly.cell(ly.add_cell("TOP"));
        addGrid(top, ly.insert_layer(db::LayerProperties(126, 0)), 4, 4, 2.0, 3.0, 0.0);
        const unsigned int pin = ly.insert_layer(db::LayerProperties(126, 2));
        addGrid(top, pin, 7, 7, 1.0, 2.0, 10.0);
        // An L (not convex), an octagon and a triangle.
        auto polygon = [&](std::vector<std::pair<double, double>> pts) {
            std::vector<db::Point> hull;
            for (const auto& [x, y] : pts) hull.emplace_back(dbu(x), dbu(y));
            db::Polygon p;
            p.assign_hull(hull.begin(), hull.end());
            top.shapes(pin).insert(p);
        };
        polygon({{0, 20}, {6, 20}, {6, 22}, {2, 22}, {2, 26}, {0, 26}});
        polygon({{12, 21}, {13, 20}, {17, 20}, {18, 21}, {18, 25}, {17, 26}, {13, 26}, {12, 25}});
        polygon({{20, 20}, {26, 20}, {20, 26}});
        writeLayout(ly, path);
    }

    RenderStackup render;
    render.modeled = true;
    render.label = "pin";
    render.stackup.addLayer(126, 0, 5.0, 3.0, "TopMetal1");
    ASSERT_EQ(render.stackup.find(126, 2), nullptr);

    const Component comp = limitsComponent(path, Orientation::FaceUp);
    auto loaded = loadStandardComponent(comp, nullptr, render, 0.0);
    ASSERT_TRUE(loaded.has_value());
    const Component3DGeometry& g = loaded->geometry;
    const LayerMesh* own = meshOf(g, 126, 0);
    const LayerMesh* borrowed = meshOf(g, 126, 2);
    ASSERT_NE(own, nullptr);
    ASSERT_NE(borrowed, nullptr);
    EXPECT_EQ(borrowed->name, "TopMetal1");
    EXPECT_EQ(borrowed->z_bottom, own->z_bottom);

    // 7 + 20 + 34 + 18 um² on 126/2, 4 x 4 um² on 126/0.
    const GdsLayer gdsBorrowed = readGdsLayer(path, "TOP", 126, 2);
    ASSERT_EQ(gdsBorrowed.shapes, 10u);
    ASSERT_NEAR(gdsBorrowed.area, 79.0, 1e-9);
    const GdsLayer gdsOwn = readGdsLayer(path, "TOP", 126, 0);
    ASSERT_EQ(gdsOwn.shapes, 4u);
    ASSERT_NEAR(gdsOwn.area, 16.0, 1e-9);

    EXPECT_EQ(borrowed->shapes, gdsBorrowed.shapes);
    EXPECT_NEAR(topFaceArea(*borrowed), gdsBorrowed.area, 1e-3);
    EXPECT_EQ(own->shapes, gdsOwn.shapes);
    EXPECT_NEAR(topFaceArea(*own), gdsOwn.area, 1e-3);
}

TEST(FastSource, ReadsWhatStandardPlacesAndLeavesTheBodyToIt)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::string path = dir.filePath("limits.gds").toStdString();
    writeLimitsDie(path);

    for (Orientation o : {Orientation::FaceUp, Orientation::FaceDown}) {
        const Component comp = limitsComponent(path, o);
        auto loaded = loadStandardComponent(comp, nullptr, limitsStackup(), 0.0);
        ASSERT_TRUE(loaded.has_value());

        std::set<LayerKey> keys;
        for (const auto& [key, place] : loaded->fast.layers) keys.insert(key);
        // The dropped via layer is read; 99/0 (placed nowhere) and the
        // substrate (a body in both views) are not.
        EXPECT_EQ(keys, (std::set<LayerKey>{LayerKey(8, 0), LayerKey(8, 2), LayerKey(10, 0),
                                            LayerKey(19, 0), LayerKey(20, 0),
                                            LayerKey(30, 0)}));

        std::size_t budget = kMaxPreparedRecords;
        const FastComponent fast = loadFastComponent(loaded->fast, budget);
        EXPECT_TRUE(fast.notes.empty());
        std::map<LayerKey, const FastLayer*> byKey;
        for (const FastLayer& l : fast.layers) byKey[l.key] = &l;
        ASSERT_EQ(byKey.size(), 6u);
        EXPECT_EQ(byKey.at(LayerKey(8, 0))->tiles.shapes, 12000u);
        EXPECT_EQ(byKey.at(LayerKey(10, 0))->tiles.shapes, 80u);
        EXPECT_EQ(byKey.at(LayerKey(19, 0))->tiles.shapes, 50001u);
        EXPECT_EQ(byKey.at(LayerKey(30, 0))->tiles.shapes, 20u);
        for (const FastLayer& l : fast.layers) {
            EXPECT_EQ(l.tiles.shapes, l.tiles.inGds) << l.key.layer << "/" << l.key.datatype;
        }

        // Same height and colour as the standard mesh, and the same place in
        // the world: Via2 is drawn whole by both.
        const Component3DGeometry& g = loaded->geometry;
        const FastLayer& via2 = *byKey.at(LayerKey(20, 0));
        const LayerMesh& mesh = *meshOf(g, 20, 0);
        EXPECT_NEAR(via2.place.zBottom / 1000.0, mesh.z_bottom, 1e-9);
        EXPECT_NEAR((via2.place.zBottom + via2.place.thickness) / 1000.0, mesh.z_top, 1e-9);
        EXPECT_EQ(via2.place.color, mesh.color);

        const auto model = fastLayerModel(g.transform, g.centeringOffset, 0.0f);
        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
        for (const ShapeTile& t : via2.tiles.tiles) {
            for (int c = 0; c < 8; ++c) {
                const float p[3] = {(c & 1) ? t.x1 : t.x0, (c & 2) ? t.y1 : t.y0,
                                    float((c & 4) ? via2.place.zBottom + via2.place.thickness
                                                  : via2.place.zBottom)};
                for (int a = 0; a < 3; ++a) {
                    const float w = model[a] * p[0] + model[4 + a] * p[1] + model[8 + a] * p[2]
                                  + model[12 + a];
                    lo[a] = std::min(lo[a], w);
                    hi[a] = std::max(hi[a], w);
                }
            }
        }
        const AA_BOUNDING_BOX world = transformedMeshBounds(mesh.mesh.boundingBox(), g.transform);
        EXPECT_NEAR(lo[0], world.mins.x, 1e-5);
        EXPECT_NEAR(lo[1], world.mins.y, 1e-5);
        EXPECT_NEAR(lo[2], world.mins.z, 1e-5);
        EXPECT_NEAR(hi[0], world.maxes.x, 1e-5);
        EXPECT_NEAR(hi[1], world.maxes.y, 1e-5);
        EXPECT_NEAR(hi[2], world.maxes.z, 1e-5);
    }
}

TEST(FastRecords, EveryShapeIsCountedAndTheAreaIsKept)
{
    db::Layout ly;
    ly.dbu(0.001);
    db::Cell& top = ly.cell(ly.add_cell("TOP"));
    const unsigned int a = ly.insert_layer(db::LayerProperties(1, 0));
    const unsigned int b = ly.insert_layer(db::LayerProperties(2, 0));
    const unsigned int skipped = ly.insert_layer(db::LayerProperties(3, 0));

    // Layer 1: two boxes, an L, a diagonal path, a square with a hole, and a
    // text that is not a shape.
    top.shapes(a).insert(db::Box(dbu(0), dbu(0), dbu(2), dbu(1)));
    top.shapes(a).insert(db::Box(dbu(5), dbu(5), dbu(6), dbu(8)));
    std::vector<db::Point> l = {{dbu(20), dbu(0)},  {dbu(30), dbu(0)},  {dbu(30), dbu(2)},
                                {dbu(22), dbu(2)},  {dbu(22), dbu(10)}, {dbu(20), dbu(10)}};
    db::Polygon lpoly;
    lpoly.assign_hull(l.begin(), l.end());
    top.shapes(a).insert(lpoly);
    std::vector<db::Point> pathPts = {{dbu(40), dbu(0)}, {dbu(50), dbu(10)}};
    top.shapes(a).insert(db::Path(pathPts.begin(), pathPts.end(), dbu(1)));
    std::vector<db::Point> outer = {{dbu(60), dbu(0)}, {dbu(70), dbu(0)}, {dbu(70), dbu(10)},
                                    {dbu(60), dbu(10)}};
    std::vector<db::Point> hole = {{dbu(63), dbu(3)}, {dbu(67), dbu(3)}, {dbu(67), dbu(7)},
                                   {dbu(63), dbu(7)}};
    db::Polygon holed;
    holed.assign_hull(outer.begin(), outer.end());
    holed.insert_hole(hole.begin(), hole.end());
    top.shapes(a).insert(holed);
    top.shapes(a).insert(db::Text("pin", db::Trans()));

    // Layer 2: a child cell's box, placed rotated by 90 degrees at (100, 0).
    db::Cell& child = ly.cell(ly.add_cell("CHILD"));
    child.shapes(b).insert(db::Box(dbu(0), dbu(0), dbu(4), dbu(2)));
    top.insert(db::CellInstArray(db::CellInst(child.cell_index()),
                                 db::Trans(db::Trans::r90, db::Vector(dbu(100), 0))));

    top.shapes(skipped).insert(db::Box(0, 0, dbu(1), dbu(1)));

    std::size_t budget = 1000;
    const auto records = extractRecords(&ly, "TOP", {LayerKey(1, 0), LayerKey(2, 0)}, budget);
    ASSERT_EQ(records.size(), 2u);
    EXPECT_EQ(records.count(LayerKey(3, 0)), 0u);

    const LayerRecords& r1 = records.at(LayerKey(1, 0));
    EXPECT_EQ(r1.inGds, 5u);
    EXPECT_EQ(r1.shapes(), 5u);
    EXPECT_EQ(r1.boxes.size(), 2u);
    EXPECT_EQ(r1.trapEnds.size(), 3u);
    // 2 + 3 for the boxes, 36 for the L, 10 sqrt 2 for the path, 100 - 16
    // for the square with its hole.
    EXPECT_NEAR(recordArea(r1), 5.0 + 36.0 + 10.0 * std::sqrt(2.0) + 84.0, 0.05);
    EXPECT_EQ(budget, 1000u - r1.records() - 1u);

    const LayerRecords& r2 = records.at(LayerKey(2, 0));
    ASSERT_EQ(r2.boxes.size(), 1u);
    EXPECT_FLOAT_EQ(r2.boxes[0].x0, 98.0f);
    EXPECT_FLOAT_EQ(r2.boxes[0].y0, 0.0f);
    EXPECT_FLOAT_EQ(r2.boxes[0].x1, 100.0f);
    EXPECT_FLOAT_EQ(r2.boxes[0].y1, 4.0f);
}

TEST(FastRecords, TheMemoryLimitPreparesAPrefixAndNeverSplitsAShape)
{
    db::Layout ly;
    ly.dbu(0.001);
    db::Cell& top = ly.cell(ly.add_cell("TOP"));
    const unsigned int a = ly.insert_layer(db::LayerProperties(1, 0));
    const unsigned int b = ly.insert_layer(db::LayerProperties(2, 0));
    const unsigned int c = ly.insert_layer(db::LayerProperties(3, 0));
    for (int i = 0; i < 5; ++i) top.shapes(a).insert(db::Box(dbu(i * 2), 0, dbu(i * 2 + 1), dbu(1)));
    for (int i = 0; i < 3; ++i) top.shapes(b).insert(db::Box(dbu(i * 2), dbu(5), dbu(i * 2 + 1), dbu(6)));
    std::vector<db::Point> l = {{0, dbu(20)},      {dbu(10), dbu(20)}, {dbu(10), dbu(22)},
                                {dbu(2), dbu(22)}, {dbu(2), dbu(30)},  {0, dbu(30)}};
    db::Polygon lpoly;
    lpoly.assign_hull(l.begin(), l.end());
    top.shapes(c).insert(lpoly);

    const std::set<LayerKey> all = {LayerKey(1, 0), LayerKey(2, 0), LayerKey(3, 0)};
    std::size_t budget = 7;
    auto records = extractRecords(&ly, "TOP", all, budget);
    EXPECT_EQ(budget, 0u);
    EXPECT_EQ(records.at(LayerKey(1, 0)).shapes(), 5u);
    EXPECT_EQ(records.at(LayerKey(2, 0)).shapes(), 2u);
    EXPECT_EQ(records.at(LayerKey(2, 0)).inGds, 3u);
    EXPECT_EQ(records.at(LayerKey(3, 0)).shapes(), 0u);
    EXPECT_EQ(records.at(LayerKey(3, 0)).inGds, 1u);

    // The L is more than one record: with one left it is not prepared at all.
    budget = 1;
    records = extractRecords(&ly, "TOP", {LayerKey(3, 0)}, budget);
    EXPECT_EQ(budget, 1u);
    EXPECT_EQ(records.at(LayerKey(3, 0)).shapes(), 0u);
    EXPECT_EQ(records.at(LayerKey(3, 0)).inGds, 1u);

    // The note names what was left out.
    EXPECT_EQ(preparedNote("U1", 2, 0, "Metal2", 2, 3),
              "3D: U1 2/0 Metal2: 2 of 3 shapes prepared (memory limit).");
}


namespace {

// The pin die as a one-component assembly, and the scene the view loads for
// it with the pin stackup.
struct PinScene {
    QTemporaryDir dir;
    Assembly assembly;
    StandardScene scene;
};

std::unique_ptr<PinScene> pinScene()
{
    auto p = std::make_unique<PinScene>();
    const std::string path = p->dir.filePath("limits.gds").toStdString();
    writeLimitsDie(path);
    auto comp = std::make_unique<Component>(limitsComponent(path, Orientation::FaceUp));
    comp->set_render_mode(RenderMode::Detailed);
    auto loaded = loadStandardComponent(*comp, nullptr, limitsStackup(), 0.0);
    if (loaded) p->scene.components.emplace(QStringLiteral("U1"), std::move(*loaded));
    p->assembly.add_component(std::move(comp));
    return p;
}

ViewCountReport countPin(const ViewCountOptions& options)
{
    auto p = pinScene();
    return viewCountScene(p->assembly, std::move(p->scene), {}, options);
}

bool hasLine(const ViewCountReport& r, const std::string& line)
{
    return std::find(r.lines.begin(), r.lines.end(), line) != r.lines.end();
}

std::vector<std::string> linesStarting(const ViewCountReport& r, const std::string& prefix)
{
    std::vector<std::string> out;
    for (const std::string& l : r.lines) {
        if (l.rfind(prefix, 0) == 0) out.push_back(l);
    }
    return out;
}

std::string joined(const std::vector<std::string>& lines)
{
    std::string out;
    for (const std::string& l : lines) out += l + "\n";
    return out;
}

}  // namespace

TEST(ViewCount, StandardAccountsForEveryShapeOfThePinLayout)
{
    const ViewCountReport r = countPin(ViewCountOptions{});
    EXPECT_TRUE(r.failures.empty()) << joined(r.lines);
    EXPECT_TRUE(hasLine(r, "layer U1 8/0 Metal1 gds=12000 drawn=10000 under_area=0 "
                           "past_read=2000 via_skipped=0 past_mesh=0"));
    EXPECT_TRUE(hasLine(r, "layer U1 10/0 Metal2 gds=80 drawn=30 under_area=50 past_read=0 "
                           "via_skipped=0 past_mesh=0"));
    EXPECT_TRUE(hasLine(r, "layer U1 19/0 Via1 gds=50001 drawn=0 under_area=0 past_read=0 "
                           "via_skipped=50001 past_mesh=0"));
    EXPECT_TRUE(hasLine(r, "layer U1 8/2 Metal1 by-layer-number gds=3 drawn=3 under_area=0 "
                           "past_read=0 via_skipped=0 past_mesh=0"));
    EXPECT_TRUE(hasLine(r, "unplaced U1 99/0 gds=5"));
    EXPECT_TRUE(hasLine(r, "note 3D: U1 has shapes on 99/0, not in the pin stackup, not drawn."));
    EXPECT_TRUE(hasLine(r, "check ok"));
    // Standard has no setting line: its notes are what they were.
    EXPECT_TRUE(linesStarting(r, "note 3D: Fast").empty());
}

TEST(ViewCount, StandardFailsWhenAGapLosesItsNote)
{
    {
        auto p = pinScene();
        p->scene.components.at(QStringLiteral("U1")).note.clear();
        const auto r = viewCountScene(p->assembly, std::move(p->scene), {}, {});
        EXPECT_EQ(r.failures,
                  (std::vector<std::string>{
                      "U1 99/0: 5 shapes placed nowhere and no note names them"}));
        EXPECT_TRUE(hasLine(r, "check FAILED (1)"));
    }
    {
        // A count whose parts no longer add up: its note loses the reason too.
        auto p = pinScene();
        for (ShapeCount& c : p->scene.components.at(QStringLiteral("U1")).counts) {
            if (c.key == LayerKey(8, 0)) c.pastRead = 0;
        }
        const auto r = viewCountScene(p->assembly, std::move(p->scene), {}, {});
        ASSERT_FALSE(r.failures.empty());
        EXPECT_EQ(r.failures[0],
                  "U1 8/0: drawn and left out add up to 10000, the GDS holds 12000");
    }
    {
        // A layer drawn at another key's height is held to the same count.
        auto p = pinScene();
        for (ShapeCount& c : p->scene.components.at(QStringLiteral("U1")).counts) {
            if (c.key == LayerKey(8, 2)) c.drawn = 2;
        }
        const auto r = viewCountScene(p->assembly, std::move(p->scene), {}, {});
        EXPECT_EQ(r.failures,
                  (std::vector<std::string>{
                      "U1 8/2: drawn and left out add up to 2, the GDS holds 3"}));
    }
}

TEST(ViewCount, StandardListsTheCountNotesOfTheLayersInView)
{
    ViewCountOptions options;
    options.camera = ViewCountCamera::Fit;
    const ViewCountReport r = countPin(options);
    EXPECT_TRUE(r.failures.empty()) << joined(r.lines);
    EXPECT_TRUE(hasLine(r, "in-view U1 8/0 Metal1: 3D: U1 8/0 Metal1: 10,000 of 12,000 shapes "
                           "shown; 2,000 past the read limit of 10,000 per layer."))
        << joined(r.lines);
    EXPECT_TRUE(hasLine(r, "in-view U1 20/0 Via2: all drawn")) << joined(r.lines);
}

TEST(ViewCount, FastPreparesEveryShapeAndLeavesTheBodyToTheStackup)
{
    ViewCountOptions options;
    options.mode = ShapeMode::Fast;
    const ViewCountReport r = countPin(options);
    EXPECT_TRUE(r.failures.empty()) << joined(r.lines);
    const auto m1 = linesStarting(r, "layer U1 8/0 Metal1 gds=12000 prepared=12000 boxes=12000 "
                                     "others=0 trapezoids=0 tiles=");
    EXPECT_EQ(m1.size(), 1u) << joined(r.lines);
    EXPECT_EQ(linesStarting(r, "layer U1 19/0 Via1 gds=50001 prepared=50001 ").size(), 1u);
    EXPECT_EQ(linesStarting(r, "layer U1 8/2 Metal1 gds=3 prepared=3 ").size(), 1u);
    EXPECT_EQ(linesStarting(r, "body U1 40/0 gds=1 ").size(), 1u);
    EXPECT_TRUE(hasLine(r, "unplaced U1 99/0 gds=5"));
    EXPECT_TRUE(hasLine(r, "note " + shapeModeNote(ShapeMode::Fast)));
    // The standard view's limits do not apply, so neither do its notes.
    EXPECT_TRUE(linesStarting(r, "note 3D: U1 8/0").empty()) << joined(r.lines);
}

TEST(ViewCount, FastNamesWhatTheMemoryLimitLeftOut)
{
    ViewCountOptions options;
    options.mode = ShapeMode::Fast;
    options.recordBudget = 100;
    const ViewCountReport r = countPin(options);
    EXPECT_TRUE(r.failures.empty()) << joined(r.lines);
    EXPECT_EQ(linesStarting(r, "layer U1 8/0 Metal1 gds=12000 prepared=100 ").size(), 1u)
        << joined(r.lines);
    EXPECT_TRUE(
        hasLine(r, "note 3D: U1 8/0 Metal1: 100 of 12,000 shapes prepared (memory limit)."));
}

TEST(ViewCount, FastBucketsAddUpAtEachCamera)
{
    for (ViewCountCamera camera :
         {ViewCountCamera::Fit, ViewCountCamera::Mid, ViewCountCamera::Close}) {
        ViewCountOptions options;
        options.mode = ShapeMode::Fast;
        options.camera = camera;
        const ViewCountReport r = countPin(options);
        EXPECT_TRUE(r.failures.empty()) << joined(r.lines);
        // Six placed layers, each with its buckets and its row.
        EXPECT_EQ(linesStarting(r, "buckets U1 ").size(), 6u) << joined(r.lines);
        EXPECT_EQ(linesStarting(r, "row U1 ").size(), 6u);
        EXPECT_EQ(linesStarting(r, "frame layers_shown=6 of 6 ").size(), 1u)
            << joined(r.lines);
        if (camera == ViewCountCamera::Fit) {
            // At fit no tile is 2 px on screen: the frame is coverage.
            EXPECT_EQ(linesStarting(r, "note 3D at this zoom: U1 draws ").size(), 1u)
                << joined(r.lines);
            EXPECT_EQ(linesStarting(r, "coverage U1 layers=").size(), 1u);
        }
    }
}

TEST(ViewCount, AComponentNotDetailedShowsNoLayers)
{
    // Components open solid or transparent; their buckets all add up with
    // nothing drawn, and only the frame line says so.
    auto p = pinScene();
    p->assembly.components().front()->set_render_mode(RenderMode::Transparent);
    ViewCountOptions options;
    options.mode = ShapeMode::Fast;
    options.camera = ViewCountCamera::Fit;
    const ViewCountReport r = viewCountScene(p->assembly, std::move(p->scene), {}, options);
    EXPECT_TRUE(r.failures.empty()) << joined(r.lines);
    EXPECT_TRUE(hasLine(r, "frame layers_shown=0 of 6 shape_records=0 coverage_tiles=0"))
        << joined(r.lines);
}

TEST(ViewCount, TheFillSwitchDecidesAFillLayer)
{
    // As setFillVisible does for the standard meshes: a fill layer follows
    // the switch, whatever the stackup says of its visibility.
    for (bool fill : {false, true}) {
        auto p = pinScene();
        for (auto& [key, place] : p->scene.components.at(QStringLiteral("U1")).fast.layers) {
            if (key == LayerKey(10, 0)) {
                place.fill = true;
                place.visible = false;
            }
        }
        ViewCountOptions options;
        options.mode = ShapeMode::Fast;
        options.camera = ViewCountCamera::Fit;
        options.fill = fill;
        const ViewCountReport r = viewCountScene(p->assembly, std::move(p->scene), {}, options);
        EXPECT_TRUE(r.failures.empty()) << joined(r.lines);
        const std::string shown =
            fill ? "frame layers_shown=6 of 6 " : "frame layers_shown=5 of 6 ";
        EXPECT_EQ(linesStarting(r, shown).size(), 1u) << "fill " << fill << "\n"
                                                      << joined(r.lines);
    }
}

TEST(ViewCount, EveryShapeDrawsNoCoverage)
{
    ViewCountOptions options;
    options.mode = ShapeMode::Every;
    options.camera = ViewCountCamera::Fit;
    const ViewCountReport r = countPin(options);
    EXPECT_TRUE(r.failures.empty()) << joined(r.lines);
    const auto buckets = linesStarting(r, "buckets U1 ");
    ASSERT_EQ(buckets.size(), 6u);
    for (const std::string& b : buckets) {
        EXPECT_NE(b.find(" coverage=0 "), std::string::npos) << b;
    }
    EXPECT_TRUE(linesStarting(r, "note 3D at this zoom:").empty());
}

TEST(ViewCount, ACameraThatCannotBePlacedIsAFailure)
{
    ViewCountOptions options;
    options.camera = ViewCountCamera::Explicit;
    options.eye = {1.0f, 2.0f, 3.0f};
    options.target = options.eye;
    const ViewCountReport r = countPin(options);
    EXPECT_EQ(r.failures, (std::vector<std::string>{"camera: the eye is on the target"}));
    EXPECT_TRUE(hasLine(r, "check FAILED (1)"));
}

TEST(ViewCount, ModeNamesSetTheirModeAndABadOneSetsNone)
{
    // studio-viewcount --mode ID=MODE: each name sets its own mode, and a
    // command line with one bad id or name changes no component at all.
    const std::vector<std::pair<std::string, RenderMode>> named = {
        {"hidden", RenderMode::Hidden},
        {"wireframe", RenderMode::Wireframe},
        {"transparent", RenderMode::Transparent},
        {"solid", RenderMode::Solid},
        {"detailed", RenderMode::Detailed},
        {"detailed-no-si", RenderMode::DetailedNoSubstrate},
    };
    for (const auto& [name, mode] : named) {
        Assembly assembly;
        assembly.add_component(std::make_unique<Component>("U1", ComponentType::Die));
        assembly.component("U1")->set_render_mode(mode == RenderMode::Hidden
                                                      ? RenderMode::Wireframe
                                                      : RenderMode::Hidden);
        std::string error;
        EXPECT_TRUE(setRenderModes(assembly, {{"U1", name}}, error)) << name << ": " << error;
        EXPECT_TRUE(assembly.component("U1")->render_mode() == mode) << name;
    }
    for (const std::string bad : {"Solid", "detailed_no_si", ""}) {
        EXPECT_FALSE(renderModeNamed(bad).has_value()) << bad;
    }

    Assembly assembly;
    assembly.add_component(std::make_unique<Component>("U1", ComponentType::Die));
    assembly.add_component(std::make_unique<Component>("U2", ComponentType::Die));
    std::string error;
    ASSERT_TRUE(setRenderModes(assembly, {{"U1", "solid"}, {"U2", "wireframe"}}, error))
        << error;
    // U1 comes first and is good; the bad entry after it must not leave U1 set.
    EXPECT_FALSE(setRenderModes(assembly, {{"U1", "hidden"}, {"U2", "bogus"}}, error));
    EXPECT_EQ(error, "no render mode bogus");
    EXPECT_FALSE(setRenderModes(assembly, {{"U1", "hidden"}, {"U9", "hidden"}}, error));
    EXPECT_EQ(error, "no component U9");
    EXPECT_TRUE(assembly.component("U1")->render_mode() == RenderMode::Solid);
    EXPECT_TRUE(assembly.component("U2")->render_mode() == RenderMode::Wireframe);
}

namespace {

// 40,000 fill squares on 8/22, 1 um at 2 um pitch, in four 100 x 100 grids,
// one per quadrant, 10 um apart at the centre; and four metal squares on
// 8/0. KLayout reads a layer in its box tree's order, so the first 10,000
// fill shapes read are one whole quadrant, and the standard read limit keeps
// exactly that quadrant. 8/0 and 8/22 are Metal1 and Metal1Filler in the
// bundled sg13g2 stackup too.
void writeQuadrantFill(const std::string& path)
{
    db::Layout ly;
    ly.dbu(0.001);
    db::Cell& top = ly.cell(ly.add_cell("TOP"));
    const unsigned int fill = ly.insert_layer(db::LayerProperties(8, 22));
    for (int q = 0; q < 4; ++q) {
        for (int i = 0; i < 10000; ++i) {
            const double x = (q % 2) * 210.0 + (i % 100) * 2.0;
            const double y = (q / 2) * 210.0 + (i / 100) * 2.0;
            top.shapes(fill).insert(db::Box(dbu(x), dbu(y), dbu(x + 1.0), dbu(y + 1.0)));
        }
    }
    addGrid(top, ly.insert_layer(db::LayerProperties(8, 0)), 4, 2, 10.0, 399.0, 0.0);
    writeLayout(ly, path);
}

constexpr double kQuadrantCentre = 204.5;  // um, between the grids in x and in y

int quadrantOf(double x, double y)
{
    return (x >= kQuadrantCentre ? 1 : 0) + (y >= kQuadrantCentre ? 2 : 0);
}

// The quadrant die as a one-component assembly, and the scene the view loads
// for it with a two-layer stackup, Metal1 and its fill.
std::unique_ptr<PinScene> fillScene()
{
    auto p = std::make_unique<PinScene>();
    const std::string path = p->dir.filePath("fill.gds").toStdString();
    writeQuadrantFill(path);
    auto comp = std::make_unique<Component>("U1", ComponentType::Die);
    comp->set_layout_path(path);
    comp->set_top_cell("TOP");
    comp->set_anchor(Anchor::BboxCenter);
    comp->set_render_mode(RenderMode::Detailed);
    RenderStackup render;
    render.modeled = true;
    render.label = "fill";
    render.stackup.addLayer(8, 0, 0.0, 1.0, "Metal1");
    render.stackup.addLayer(8, 22, 0.0, 1.0, "Metal1Filler", "filler");
    auto loaded = loadStandardComponent(*comp, nullptr, render, 0.0);
    if (loaded) p->scene.components.emplace(QStringLiteral("U1"), std::move(*loaded));
    p->assembly.add_component(std::move(comp));
    return p;
}

}  // namespace

// Standard with fill on never draws the first shapes in read order: those are
// one region, and the view would say something false about where the fill
// is. It draws the fill layer's coverage tiles over the whole extent, from a
// read of every fill shape, and never the capped mesh. The camera's pixel
// scale is raised so far that Fast draws every tile as shapes, so a frame
// that is all coverage says Standard chose it.
TEST(StandardFill, FillReadInOneQuadrantFirstDrawsTilesOverTheWholeExtent)
{
    auto p = fillScene();
    ASSERT_EQ(p->scene.components.count(QStringLiteral("U1")), 1u);
    StandardComponent& standard = p->scene.components.at(QStringLiteral("U1"));

    // What the fixture is for: the standard read kept one quadrant whole and
    // none of the rest.
    std::array<std::size_t, 4> read{};
    for (const SimplePolygon& poly : standard.polygons.at(LayerKey(8, 22)).polygons) {
        double x0, y0, x1, y1;
        poly.boundingBox(x0, y0, x1, y1);
        ++read[quadrantOf((x0 + x1) / 2, (y0 + y1) / 2)];
    }
    ASSERT_EQ(*std::max_element(read.begin(), read.end()), 10000u);
    ASSERT_EQ(read[0] + read[1] + read[2] + read[3], 10000u);

    // The capped mesh is never drawn, with fill on; the metal beside it is.
    setFillVisible(standard.geometry, true);
    ASSERT_NE(meshOf(standard.geometry, 8, 22), nullptr);
    ASSERT_NE(meshOf(standard.geometry, 8, 0), nullptr);
    ASSERT_TRUE(meshOf(standard.geometry, 8, 22)->visible);
    EXPECT_FALSE(standardMeshDrawn(*meshOf(standard.geometry, 8, 22)));
    EXPECT_TRUE(standardMeshDrawn(*meshOf(standard.geometry, 8, 0)));

    std::size_t budget = kMaxPreparedRecords;
    const FastComponent fill = loadFillTiles(standard.fast, budget);
    ASSERT_EQ(fill.layers.size(), 1u);  // the fill layer, not the metal
    ASSERT_EQ(fill.layers[0].key, LayerKey(8, 22));
    const TiledLayer& tiles = fill.layers[0].tiles;
    EXPECT_EQ(tiles.records(), 40000u);  // every fill shape, not the standard read's 10,000

    ViewCountOptions options;
    options.camera = ViewCountCamera::Fit;
    Camera camera;
    std::string error;
    ASSERT_TRUE(viewCountCamera(p->assembly, p->scene, options, camera, error)) << error;
    TileCamera view = tileCamera(camera, 1280, 800);
    view.viewportHeight = 800 * 1000;
    const FastFrame frame = fastFrame(
        {{&fill, &standard.geometry, true, false}},
        [](const std::string&, const FastLayer&) { return true; }, 1.0f);
    ASSERT_EQ(frame.inputs.size(), 1u);

    // Fast at this scale draws every tile as shapes.
    TileChoiceMemory fastMemory;
    const TileChoice fast =
        chooseTiles(frame.inputs, view, tileChoiceParams(ShapeMode::Fast), fastMemory);
    ASSERT_EQ(fast.shapeRecords, tiles.records());

    TileChoiceMemory memory;
    const TileChoice choice =
        chooseTiles(frame.inputs, view, tileChoiceParams(ShapeMode::Standard), memory);
    EXPECT_EQ(choice.shapeRecords, 0u);
    std::array<std::size_t, 4> covered{};
    for (std::size_t t = 0; t < tiles.tiles.size(); ++t) {
        if (choice.buckets[0][t] != TileBucket::Coverage) continue;
        for (std::size_t b = tiles.tiles[t].b0; b < tiles.tiles[t].b1; ++b) {
            const TileBox& box = tiles.boxes[b];
            ++covered[quadrantOf((box.x0 + box.x1) / 2, (box.y0 + box.y1) / 2)];
        }
    }
    EXPECT_EQ(covered, (std::array<std::size_t, 4>{10000, 10000, 10000, 10000}));
}

// studio-viewcount says the same with Standard: a fill layer is hidden with
// fill off; with fill on every fill shape is read and drawn as coverage
// tiles, none as shapes, its capped mesh is not in view, and the strip says
// tiles and keeps no count note for it. The viewport is ten times the usual,
// a scale at which Fast would draw every fill tile as shapes.
TEST(ViewCount, StandardDrawsFillAsCoverageTilesOverTheWholeLayer)
{
    for (bool fill : {false, true}) {
        SCOPED_TRACE(fill ? "fill on" : "fill off");
        auto p = fillScene();
        ViewCountOptions options;
        options.camera = ViewCountCamera::Fit;
        options.width = 12800;
        options.height = 8000;
        options.fill = fill;
        const ViewCountReport r = viewCountScene(p->assembly, std::move(p->scene), {}, options);
        EXPECT_TRUE(r.failures.empty()) << joined(r.lines);
        EXPECT_TRUE(linesStarting(r, "in-view U1 8/22 ").empty()) << joined(r.lines);
        for (const std::string& n : linesStarting(r, "note ")) {
            EXPECT_EQ(n.find("8/22"), std::string::npos) << n;
        }
        if (!fill) {
            EXPECT_TRUE(hasLine(r, "layer U1 8/22 Metal1Filler fill gds=40000 hidden (fill off)"))
                << joined(r.lines);
            EXPECT_TRUE(linesStarting(r, "buckets U1 8/22 ").empty());
            EXPECT_TRUE(linesStarting(r, "coverage U1 ").empty());
            EXPECT_TRUE(linesStarting(r, "frame fill ").empty());
            continue;
        }
        const std::string layer = "layer U1 8/22 Metal1Filler fill gds=40000 prepared=40000 "
                                  "boxes=40000 others=0 trapezoids=0 tiles=";
        const std::string buckets = "buckets U1 8/22 shape=0 coverage=40000 outside=0 "
                                    "hidden=0 shape_records=0 coverage_tiles=";
        const std::string frame =
            "frame fill layers_shown=1 of 1 shape_records=0 coverage_tiles=";
        EXPECT_EQ(linesStarting(r, layer).size(), 1u) << joined(r.lines);
        EXPECT_EQ(linesStarting(r, buckets).size(), 1u) << joined(r.lines);
        EXPECT_TRUE(hasLine(r, "coverage U1 fill_layers=1 shapes=40000")) << joined(r.lines);
        EXPECT_TRUE(hasLine(r, "note " + fillCoverageNote("U1", 1, 40000))) << joined(r.lines);
        EXPECT_EQ(linesStarting(r, frame).size(), 1u) << joined(r.lines);
    }
}

namespace {

AA_BOUNDING_BOX boxMm(float x0, float y0, float z0, float x1, float y1, float z1)
{
    AA_BOUNDING_BOX b;
    b.SetFromMinsMaxes(VECTOR3D(x0, y0, z0), VECTOR3D(x1, y1, z1));
    return b;
}

// A scene put together from boxes, with no layout: a component's layers are
// one mesh of one extent, its body a mesh of another.
struct BoxScene {
    Assembly assembly;
    StandardScene scene;

    void add(const std::string& id, ComponentType type, const AA_BOUNDING_BOX* layers,
             const AA_BOUNDING_BOX* body)
    {
        assembly.add_component(std::make_unique<Component>(id, type));
        const QString key = QString::fromStdString(id);
        if (layers) {
            StandardComponent c;
            LayerMesh m;
            m.mesh.setBoundingBox(*layers);
            c.geometry.layers.push_back(std::move(m));
            scene.components.emplace(key, std::move(c));
        }
        if (body) {
            ComponentMesh m;
            m.setBoundingBox(*body);
            scene.boxes.emplace(key, std::move(m));
        }
    }
};

const AA_BOUNDING_BOX kInterposerLayers = boxMm(0, -0.25f, -3.2f, 2, 0.0127103f, 0);
const AA_BOUNDING_BOX kInterposerBody = boxMm(0, -1.58617f, -3.2f, 2, 0.01383f, 0);

// The boxes of the ex5 copy as studio-viewcount printed them: an interposer
// whose body's top is above its layers, and two dies on it with a 0.2 mm gap
// between them along z.
std::unique_ptr<BoxScene> ex5Boxes()
{
    auto s = std::make_unique<BoxScene>();
    const AA_BOUNDING_BOX u1Layers = boxMm(0.5f, 0.05783f, -2.7f, 1.5f, 0.12166f, -1.7f);
    const AA_BOUNDING_BOX u1Body = boxMm(0.5f, 0.05783f, -2.7f, 1.5f, 0.80783f, -1.7f);
    const AA_BOUNDING_BOX u2Layers = boxMm(0.5f, 0.05783f, -1.5f, 1.5f, 0.12166f, -0.5f);
    const AA_BOUNDING_BOX u2Body = boxMm(0.5f, 0.05783f, -1.5f, 1.5f, 0.80783f, -0.5f);
    s->add("interposer", ComponentType::Interposer, &kInterposerLayers, &kInterposerBody);
    s->add("U1", ComponentType::Die, &u1Layers, &u1Body);
    s->add("U2", ComponentType::Die, &u2Layers, &u2Body);
    return s;
}

bool insideClosed(const VECTOR3D& p, const AA_BOUNDING_BOX& b)
{
    return p.x >= b.mins.x && p.x <= b.maxes.x && p.y >= b.mins.y && p.y <= b.maxes.y
        && p.z >= b.mins.z && p.z <= b.maxes.z;
}

// Whether a point of the segment from a to b, ends left out, is strictly
// inside the box. By sampling, apart from the camera's own clipping, so it
// finds no crossing shorter than a 4096th of the segment.
bool sampledCrossing(const VECTOR3D& a, const VECTOR3D& b, const AA_BOUNDING_BOX& box)
{
    constexpr int kSteps = 4096;
    for (int k = 1; k < kSteps; ++k) {
        const double t = double(k) / kSteps;
        const double x = a.x + t * (b.x - a.x);
        const double y = a.y + t * (b.y - a.y);
        const double z = a.z + t * (b.z - a.z);
        if (x > box.mins.x && x < box.maxes.x && y > box.mins.y && y < box.maxes.y
            && z > box.mins.z && z < box.maxes.z) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST(CloseCamera, TheEyeIsInNoBoxAndSeesItsTargetPastThemAll)
{
    auto s = ex5Boxes();
    ViewCountOptions options;
    options.camera = ViewCountCamera::Close;
    Camera camera;
    std::string error;
    ASSERT_TRUE(viewCountCamera(s->assembly, s->scene, options, camera, error)) << error;
    const VECTOR3D eye = camera.position();
    const VECTOR3D target = camera.target();
    const std::vector<ComponentBox> boxes = componentBoxes(s->scene);
    ASSERT_EQ(boxes.size(), 6u);
    for (const ComponentBox& b : boxes) {
        EXPECT_FALSE(insideClosed(eye, b.box)) << b.id << " " << b.kind;
        EXPECT_FALSE(sampledCrossing(eye, target, b.box)) << b.id << " " << b.kind;
    }
}

TEST(CloseCamera, TheSearchTakesTheNearestClearCellAndBreaksTiesByXThenZ)
{
    ViewCountOptions options;
    options.camera = ViewCountCamera::Close;
    {
        // The cells nearest the centre are under a die or have one in the
        // way. Worked out apart from the code, the first clear one is 41,29,
        // the 203rd tried.
        auto s = ex5Boxes();
        Camera camera;
        std::string error;
        CloseCell cell;
        ASSERT_TRUE(viewCountCamera(s->assembly, s->scene, options, camera, error, &cell))
            << error;
        EXPECT_EQ(cell.face, "interposer");
        EXPECT_EQ(cell.i, 41);
        EXPECT_EQ(cell.j, 29);
        EXPECT_EQ(cell.tried, 203);
        const VECTOR3D t = camera.target();
        // The cell's centre, at the body's top (above the layers').
        EXPECT_EQ(t.x, 1.296875f);
        EXPECT_EQ(t.y, 0.01383f);
        EXPECT_EQ(t.z, -3.2f + 3.2f * 59.0f / 128.0f);
    }
    {
        // Four cells tie nearest the centre: 31,31, 31,32, 32,31 and 32,32. A
        // post stands on 31,31, so the next by x then z is 31,32; by z then
        // x it would be 32,31.
        auto s = std::make_unique<BoxScene>();
        const AA_BOUNDING_BOX post = boxMm(0.97f, 0.01383f, -1.64f, 0.995f, 0.2f, -1.615f);
        s->add("interposer", ComponentType::Interposer, &kInterposerLayers, &kInterposerBody);
        s->add("U1", ComponentType::Die, nullptr, &post);
        Camera camera;
        std::string error;
        CloseCell cell;
        ASSERT_TRUE(viewCountCamera(s->assembly, s->scene, options, camera, error, &cell))
            << error;
        EXPECT_EQ(cell.i, 31);
        EXPECT_EQ(cell.j, 32);
        EXPECT_EQ(cell.tried, 2);
    }
}

TEST(CloseCamera, NoClearCellIsAFailure)
{
    ViewCountOptions options;
    options.camera = ViewCountCamera::Close;
    {
        // A die body over the whole face and past it on the eye's side: from
        // every cell, the eye is in it or the line to the eye runs through it.
        auto s = std::make_unique<BoxScene>();
        const AA_BOUNDING_BOX lid = boxMm(-0.5f, 0.05783f, -3.7f, 3, 0.80783f, 1);
        s->add("interposer", ComponentType::Interposer, &kInterposerLayers, &kInterposerBody);
        s->add("U1", ComponentType::Die, nullptr, &lid);
        Camera camera;
        std::string error;
        EXPECT_FALSE(viewCountCamera(s->assembly, s->scene, options, camera, error));
        EXPECT_EQ(error, "no point on interposer's top face gives the close camera a clear view");
    }
    {
        auto s = std::make_unique<BoxScene>();
        s->add("U1", ComponentType::Die, nullptr, nullptr);
        Camera camera;
        std::string error;
        EXPECT_FALSE(viewCountCamera(s->assembly, s->scene, options, camera, error));
        EXPECT_EQ(error, "no component with a box for the close camera");
    }
}

// What GL receives from the fast view, counted by GL: the drawer renders a
// frame of the pin layout at the fit camera, and a primitive query must
// count 12 triangles per record the choice drew as a shape (a box or a
// trapezoid, one prism each) and 2 per coverage quad. That ties what GL was
// given to the numbers studio-viewcount prints.
class FastGL : public ::testing::Test {
protected:
    QOpenGLContext context;
    QOffscreenSurface surface;
    QOpenGLExtraFunctions* gl = nullptr;
    GLuint fbo = 0, colour = 0, depth = 0;

    void SetUp() override
    {
        QSurfaceFormat format;
        format.setVersion(3, 3);
        format.setProfile(QSurfaceFormat::CoreProfile);
        context.setFormat(format);
        ASSERT_TRUE(context.create()) << "Native OpenGL context is required, not skipped";
        surface.setFormat(context.format());
        surface.create();
        ASSERT_TRUE(surface.isValid());
        ASSERT_TRUE(context.makeCurrent(&surface));
        const auto actual = context.format();
        ASSERT_TRUE(actual.majorVersion() > 3
                    || (actual.majorVersion() == 3 && actual.minorVersion() >= 3));
        gl = context.extraFunctions();
        ASSERT_NE(gl, nullptr);
        gl->initializeOpenGLFunctions();
        gl->glGenRenderbuffers(1, &colour);
        gl->glBindRenderbuffer(GL_RENDERBUFFER, colour);
        gl->glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 1280, 800);
        gl->glGenRenderbuffers(1, &depth);
        gl->glBindRenderbuffer(GL_RENDERBUFFER, depth);
        gl->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, 1280, 800);
        gl->glGenFramebuffers(1, &fbo);
        gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        gl->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
                                      colour);
        gl->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER,
                                      depth);
        ASSERT_EQ(gl->glCheckFramebufferStatus(GL_FRAMEBUFFER), GLenum(GL_FRAMEBUFFER_COMPLETE));
        gl->glViewport(0, 0, 1280, 800);
        gl->glEnable(GL_DEPTH_TEST);
        gl->glEnable(GL_CULL_FACE);
    }

    void TearDown() override
    {
        if (gl) {
            gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
            gl->glDeleteFramebuffers(1, &fbo);
            const GLuint buffers[2] = {colour, depth};
            gl->glDeleteRenderbuffers(2, buffers);
        }
        context.doneCurrent();
    }
};

namespace {

QMatrix4x4 qMatrix(const MATRIX4X4& m)
{
    QMatrix4x4 out;
    for (int i = 0; i < 16; ++i) out.data()[i] = m.GetEntry(i);
    return out;
}

}  // namespace

TEST_F(FastGL, ThePrimitiveQueryCountsWhatTheChoiceDrew)
{
    auto p = pinScene();
    const StandardComponent& standard = p->scene.components.at(QStringLiteral("U1"));
    std::size_t budget = kMaxPreparedRecords;
    const FastComponent fast = loadFastComponent(standard.fast, budget);

    ViewCountOptions options;
    options.mode = ShapeMode::Fast;
    options.camera = ViewCountCamera::Fit;
    Camera camera;
    std::string error;
    ASSERT_TRUE(viewCountCamera(p->assembly, p->scene, options, camera, error)) << error;

    const FastFrame frame = fastFrame(
        {{&fast, &standard.geometry, true, false}},
        [](const std::string&, const FastLayer&) { return true; }, 1.0f);
    // At fit no tile reaches the size threshold, so every tile in view is made
    // a candidate, and the budget is half of all records: the frame then holds
    // both kinds, and neither term of the sum is vacuous.
    std::size_t records = 0;
    for (const TileLayerInput& in : frame.inputs) {
        for (const ShapeTile& t : in.tiles->tiles) records += t.records();
    }
    TileChoiceParams params;
    params.upPx = 0.0f;
    params.downPx = 0.0f;
    params.budget = records / 2;
    TileChoiceMemory memory;
    const TileChoice choice =
        chooseTiles(frame.inputs, tileCamera(camera, 1280, 800), params, memory);
    ASSERT_GT(choice.shapeRecords, 0u);
    ASSERT_GT(choice.coverageTiles, 0u);

    std::vector<const FastLayer*> uploads;
    std::vector<FastDrawLayer> draws;
    for (std::size_t i = 0; i < frame.refs.size(); ++i) {
        uploads.push_back(frame.refs[i].layer);
        FastDrawLayer d;
        d.layer = frame.refs[i].layer;
        d.choiceIndex = i;
        d.model = QMatrix4x4(frame.inputs[i].model.data()).transposed();
        d.color = QVector4D(0.5f, 0.5f, 0.5f, 1.0f);
        d.coverRank = frame.coverRanks[i];
        draws.push_back(d);
    }

    FastShapesGL drawer;
    ASSERT_TRUE(drawer.init(gl));
    drawer.upload(gl, uploads);
    FastFrameMatrices m;
    m.view = qMatrix(camera.viewMatrix());
    m.projection = qMatrix(camera.projectionMatrix(1280.0f / 800.0f));
    m.lightDirection = QVector3D(0.0f, -1.0f, 0.0f);
    m.fcoefHalf = camera.fcoef() * 0.5f;

    gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    drawer.beginCount(gl);
    drawer.draw(gl, draws, choice, m, false);
    drawer.draw(gl, draws, choice, m, true);
    const std::size_t triangles = drawer.endCount(gl);
    EXPECT_EQ(gl->glGetError(), GLenum(GL_NO_ERROR));
    EXPECT_EQ(triangles, 12 * choice.shapeRecords + 2 * choice.coverageTiles)
        << choice.shapeRecords << " shape records, " << choice.coverageTiles << " coverage tiles";
    drawer.release(gl);
}

// Fill's coverage tiles are hatched: diagonal bands on screen, every other
// one darker, so fill reads apart from metal drawn in the same colour. A
// metal layer's tiles are flat. One tile, one box filling it, drawn over the
// whole viewport.
TEST_F(FastGL, FillCoverageIsHatchedAndOtherCoverageIsNot)
{
    for (bool fill : {false, true}) {
        SCOPED_TRACE(fill ? "fill" : "metal");
        LayerRecords records;
        records.boxes.push_back({0.0f, 0.0f, 100.0f, 100.0f});
        records.inGds = 1;
        FastLayer layer;
        layer.key = LayerKey(8, fill ? 22 : 0);
        layer.place.fill = fill;
        layer.place.zBottom = 0.0;
        layer.place.thickness = 1.0;
        layer.tiles = tileLayer(std::move(records));
        ASSERT_EQ(layer.tiles.tiles.size(), 1u);

        TileChoice choice;
        choice.buckets = {{TileBucket::Coverage}};
        choice.coverageTiles = 1;
        FastDrawLayer d;
        d.layer = &layer;
        d.model.translate(-1.0f, -1.0f, 0.0f);  // um to clip; view and projection are identity
        d.model.scale(0.02f, 0.02f, 0.001f);
        d.color = QVector4D(0.5f, 0.5f, 0.5f, 1.0f);
        FastFrameMatrices m;
        m.lightDirection = QVector3D(0.0f, 0.0f, -1.0f);
        m.fcoefHalf = 0.5f;

        FastShapesGL drawer;
        ASSERT_TRUE(drawer.init(gl));
        drawer.upload(gl, {&layer});
        gl->glDisable(GL_CULL_FACE);
        gl->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawer.draw(gl, {d}, choice, m, false);
        std::vector<uint8_t> row(1280 * 4), below(1280 * 4);
        gl->glReadPixels(0, 400, 1280, 1, GL_RGBA, GL_UNSIGNED_BYTE, row.data());
        gl->glReadPixels(0, 399, 1280, 1, GL_RGBA, GL_UNSIGNED_BYTE, below.data());
        EXPECT_EQ(gl->glGetError(), GLenum(GL_NO_ERROR));
        drawer.release(gl);

        std::set<int> levels;
        for (int x = 0; x < 1280; ++x) levels.insert(row[4 * x]);
        ASSERT_GT(*levels.begin(), 0) << "the tile did not cover the row";
        if (!fill) {
            EXPECT_EQ(levels.size(), 1u);
            continue;
        }
        ASSERT_EQ(levels.size(), 2u);
        EXPECT_NEAR(2 * *levels.begin(), *levels.rbegin(), 2);  // the dark band is half as bright
        // Diagonal: the pixel down and to the right is the same band.
        int off = 0;
        for (int x = 0; x + 1 < 1280; ++x) off += below[4 * (x + 1)] != row[4 * x];
        EXPECT_EQ(off, 0);
        // Bands 4 px wide along the row, the two at its ends cut short.
        std::vector<int> runs{1};
        for (int x = 1; x < 1280; ++x) {
            if (row[4 * x] == row[4 * (x - 1)]) ++runs.back();
            else runs.push_back(1);
        }
        ASSERT_GT(runs.size(), 2u);
        EXPECT_EQ(std::count(runs.begin() + 1, runs.end() - 1, 4),
                  std::ptrdiff_t(runs.size()) - 2);
    }
}

// The light is fixed in the scene in the fast view too: the drawer is given
// the scene's light, as SceneManager sets it, and turns it by the view, so a
// layer's top shows one shade from every camera. One box filling its one
// tile, drawn as a shape and as coverage, seen from above and from low on one
// side; the frame's centre is the top's centre.
TEST_F(FastGL, ALayersTopIsLitByItsAngleToTheSceneNotToTheCamera)
{
    const VECTOR3D l = SceneManager().lightDirection();
    const QVector3D light(l.x, l.y, l.z);
    // The shaders' lighting of a face that looks up: 0.3 ambient, and 0.6 of
    // the cosine to the light.
    const float lit = 0.3f + 0.6f * std::max(-light.y(), 0.0f);
    gl->glDisable(GL_CULL_FACE);
    for (TileBucket bucket : {TileBucket::Shape, TileBucket::Coverage}) {
        const bool shape = bucket == TileBucket::Shape;
        SCOPED_TRACE(shape ? "shape" : "coverage");
        LayerRecords records;
        records.boxes.push_back({0.0f, 0.0f, 1000.0f, 1000.0f});
        records.inGds = 1;
        FastLayer layer;
        layer.key = LayerKey(8, 0);
        layer.place.zBottom = 0.0;
        layer.place.thickness = 100.0;
        layer.tiles = tileLayer(std::move(records));
        ASSERT_EQ(layer.tiles.tiles.size(), 1u);

        TileChoice choice;
        choice.buckets = {{bucket}};
        (shape ? choice.shapeRecords : choice.coverageTiles) = 1;
        FastDrawLayer d;
        d.layer = &layer;
        // GDS um to the scene's mm, the GDS's Z up (CoordFrame.h): x, z, -y.
        d.model = QMatrix4x4(0.001f, 0.0f, 0.0f, 0.0f,
                             0.0f, 0.0f, 0.001f, 0.0f,
                             0.0f, -0.001f, 0.0f, 0.0f,
                             0.0f, 0.0f, 0.0f, 1.0f);
        d.color = QVector4D(0.5f, 0.5f, 0.5f, 1.0f);

        FastShapesGL drawer;
        ASSERT_TRUE(drawer.init(gl));
        drawer.upload(gl, {&layer});
        for (const auto& [yaw, pitch] : {std::pair{0.0f, 89.0f}, std::pair{90.0f, 30.0f}}) {
            SCOPED_TRACE(pitch > 80.0f ? "from above" : "low, from +X");
            Camera camera;
            camera.setTarget(VECTOR3D(0.5f, 0.1f, -0.5f));  // the top's centre, mm
            camera.setDistance(3.0f);
            camera.setOrientation(yaw, pitch);
            FastFrameMatrices m;
            m.view = qMatrix(camera.viewMatrix());
            m.projection = qMatrix(camera.projectionMatrix(1280.0f / 800.0f));
            m.lightDirection = light;
            m.fcoefHalf = camera.fcoef() * 0.5f;
            gl->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            drawer.draw(gl, {d}, choice, m, false);
            uint8_t centre[4] = {};
            gl->glReadPixels(640, 400, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, centre);
            EXPECT_EQ(gl->glGetError(), GLenum(GL_NO_ERROR));
            EXPECT_NEAR(centre[0], 0.5f * 255.0f * lit, 2) << "the top";
        }
        drawer.release(gl);
    }
}

// The Filter slider rebuilds a component's layers from the stackup the view
// kept from its load. On the interposer's technology that stackup puts the
// bumps on the file's plane, so after a rebuild they are still there, not at
// the stackup's own 13.83. Through the view's public calls, on a shown view.
TEST(SceneLoad, TheFilterRebuildKeepsTheBumpsOnTheFilesPlane)
{
    EnvGuard env("INTERCONNECT_PDK_ROOT");
    const std::string base = std::string(FIXTURES_DIR) + "/interconnect_merge";
    ConfigsDir configs(base + "/configs");
    setenv("INTERCONNECT_PDK_ROOT", (base + "/pdk").c_str(), 1);

    // 500/35 CuPillar: four large shapes and six small ones, so a filter
    // that drops the small ones shows that the rebuild ran.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::string gds = dir.filePath("interposer.gds").toStdString();
    {
        db::Layout ly;
        ly.dbu(0.001);
        db::Cell& top = ly.cell(ly.add_cell("TOP"));
        const unsigned int pillar = ly.insert_layer(db::LayerProperties(500, 35));
        addGrid(top, pillar, 4, 4, 40.0, 100.0, 0.0);   // 1600 um2 each
        addGrid(top, pillar, 6, 6, 2.0, 10.0, 200.0);   // 4 um2 each
        writeLayout(ly, gds);
    }

    ChipletFormat format;
    auto assembly = format.load(base + "/merge_z.chiplet");
    ASSERT_NE(assembly, nullptr);
    Component* interposer = assembly->component("interposer");
    ASSERT_NE(interposer, nullptr);
    interposer->set_layout_path(gds);
    interposer->set_top_cell("TOP");
    interposer->set_attachment_surface_z(10.8103);
    interposer->set_render_mode(RenderMode::Detailed);

    const QString id = QStringLiteral("interposer");
    const double plane = 10.8103 * 0.001;  // um to the scene's mm
    AssemblyView view;
    view.setAssembly(assembly.get());
    view.resize(400, 300);
    view.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&view));
    view.grabFramebuffer();  // initializeGL builds the assembly set before it

    const auto loaded = view.drawnLayer(id, 500, 35);
    ASSERT_TRUE(loaded.has_value()) << "no 500/35 mesh after the load";
    ASSERT_EQ(loaded->shapes, 10u);
    EXPECT_NEAR(loaded->zBottom, plane, 1e-9);

    // The slider is debounced; the rebuild is in once the small shapes go.
    view.setShapeFilterPercent(id, 50.0);
    std::optional<AssemblyView::DrawnLayer> rebuilt = loaded;
    QElapsedTimer waited;
    waited.start();
    while (rebuilt && rebuilt->shapes == 10u && waited.elapsed() < 5000) {
        QTest::qWait(50);
        rebuilt = view.drawnLayer(id, 500, 35);
    }
    ASSERT_TRUE(rebuilt.has_value()) << "no 500/35 mesh after the Filter rebuild";
    ASSERT_EQ(rebuilt->shapes, 4u) << "the Filter rebuild did not run in 5 s";
    EXPECT_NEAR(rebuilt->zBottom, plane, 1e-9);
}

// Through the view, on the bundled sg13g2 stackup: Show Fill on in Standard
// reads every fill shape in the background and draws it as tiles; the fill
// row counts all of it, none as shapes, and the strip says tiles and keeps no
// count note for the standard read's 10,000. Off again, the row is the
// file's count and the strip drops the tiles.
TEST(SceneLoad, ShowFillInStandardReadsEveryFillShapeAndSaysTiles)
{
    ConfigsDir configs(CONFIGS_DIR);
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::string gds = dir.filePath("fill.gds").toStdString();
    writeQuadrantFill(gds);
    Assembly assembly;
    auto comp = std::make_unique<Component>("U1", ComponentType::Die);
    comp->set_layout_path(gds);
    comp->set_top_cell("TOP");
    comp->set_anchor(Anchor::BboxCenter);
    comp->set_render_mode(RenderMode::Detailed);
    comp->set_technology("sg13g2");
    assembly.add_component(std::move(comp));

    const QString id = QStringLiteral("U1");
    AssemblyView view;
    view.setAssembly(&assembly);
    view.resize(400, 300);
    view.show();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(&view));
    view.grabFramebuffer();  // initializeGL builds the assembly set before it

    auto fillRow = [&]() -> std::optional<AssemblyView::LayerListEntry> {
        for (const auto& e : view.componentLayers(id)) {
            if (e.layer == 8 && e.datatype == 22) return e;
        }
        return std::nullopt;
    };
    auto noteStarting = [&](const QString& prefix) {
        for (const QString& n : view.notes()) {
            if (n.startsWith(prefix)) return true;
        }
        return false;
    };
    auto noFillCountNote = [&]() {
        for (const QString& n : view.notes()) {
            if (n.contains(QStringLiteral("8/22"))) return false;
        }
        return true;
    };
    const QString tiles =
        QStringLiteral("3D: U1 draws 1 fill layer as hatched coverage tiles (");

    const auto off = fillRow();
    ASSERT_TRUE(off.has_value()) << "no 8/22 row: the stackup did not place the fill";
    EXPECT_TRUE(off->fill);
    EXPECT_EQ(off->count, QStringLiteral("40,000 shapes"));
    EXPECT_TRUE(off->countNote.isEmpty());
    EXPECT_TRUE(noFillCountNote());
    EXPECT_FALSE(view.fillPrepared());

    // The read is in the background, and the row and the tiles line follow
    // the camera once it has settled: no repaint for 250 ms, so the loop
    // only waits.
    view.setShowFill(true);
    QElapsedTimer waited;
    waited.start();
    while (!(view.fillPrepared() && noteStarting(tiles) && fillRow()
             && fillRow()->count.contains(QStringLiteral("as coverage")))
           && waited.elapsed() < 30000) {
        QTest::qWait(50);
    }
    ASSERT_TRUE(view.fillPrepared()) << "the fill was not read in 30 s";
    ASSERT_TRUE(noteStarting(tiles)) << view.notes().join('\n').toStdString();
    EXPECT_FALSE(noteStarting(QString::fromStdString(fillReadingNote())));
    EXPECT_TRUE(noFillCountNote()) << view.notes().join('\n').toStdString();
    const auto on = fillRow();
    ASSERT_TRUE(on.has_value());
    const QString noneAsShapes = QStringLiteral("40,000 shapes; at this zoom 0 drawn as shapes, ");
    EXPECT_TRUE(on->count.startsWith(noneAsShapes)) << on->count.toStdString();
    EXPECT_FALSE(on->count.contains(QStringLiteral("not prepared"))) << on->count.toStdString();

    view.setShowFill(false);
    EXPECT_FALSE(noteStarting(tiles));
    EXPECT_EQ(fillRow()->count, QStringLiteral("40,000 shapes"));
}

// The strip says fill is being read only for a die whose fill the view draws:
// Detailed, with or without its substrate, and shown. A die switched out of
// Detailed keeps its fill source, so turning fill on reads it again; the
// strip then names it as not Detailed and does not say fill is coming. A
// hidden die says nothing either, and hiding one refreshes the strip. Each
// case is a fresh view, asked before any event is processed, so the read
// that fill on starts has not finished.
TEST(SceneLoad, TheStripSaysFillIsBeingReadOnlyForADieWhoseFillIsDrawn)
{
    ConfigsDir configs(CONFIGS_DIR);
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::string gds = dir.filePath("fill.gds").toStdString();
    writeQuadrantFill(gds);
    const QString id = QStringLiteral("U1");
    const QString reading = QString::fromStdString(fillReadingNote());
    const QString notDetailed = QStringLiteral("3D: U1 is not drawn Detailed");

    struct Case {
        const char* name;
        RenderMode mode;  // set after the view has built U1 Detailed
        bool shown;
        bool fillDrawn;
    };
    for (const Case& c : {Case{"Detailed", RenderMode::Detailed, true, true},
                          Case{"Detailed (no Si bulk)", RenderMode::DetailedNoSubstrate, true, true},
                          Case{"switched to Transparent", RenderMode::Transparent, true, false},
                          Case{"hidden", RenderMode::Detailed, false, false}}) {
        SCOPED_TRACE(c.name);
        Assembly assembly;
        auto comp = std::make_unique<Component>("U1", ComponentType::Die);
        comp->set_layout_path(gds);
        comp->set_top_cell("TOP");
        comp->set_anchor(Anchor::BboxCenter);
        comp->set_render_mode(RenderMode::Detailed);
        comp->set_technology("sg13g2");
        Component* u1 = comp.get();
        assembly.add_component(std::move(comp));

        AssemblyView view;
        view.setAssembly(&assembly);
        view.resize(400, 300);
        view.show();
        ASSERT_TRUE(QTest::qWaitForWindowExposed(&view));
        view.grabFramebuffer();  // builds U1 Detailed: its layers and its fill source
        if (c.mode != RenderMode::Detailed) {
            u1->set_render_mode(c.mode);
            view.onComponentRenderModeChanged(id, c.mode);
        }
        if (!c.shown) {
            int refreshed = 0;
            QObject::connect(&view, &AssemblyView::notesChanged, [&refreshed] { ++refreshed; });
            view.setComponentVisibility(id, false);
            EXPECT_EQ(refreshed, 1) << "hiding a die does not refresh the strip";
        }

        view.setShowFill(true);
        const QStringList notes = view.notes();
        const std::string strip = notes.join('\n').toStdString();
        const bool detailNote = std::any_of(notes.begin(), notes.end(), [&](const QString& n) {
            return n.startsWith(notDetailed);
        });
        ASSERT_EQ(detailNote, c.mode == RenderMode::Transparent) << strip;
        EXPECT_EQ(notes.contains(reading), c.fillDrawn) << strip;
        EXPECT_EQ(view.fillPrepared(), !c.fillDrawn) << strip;

        // Let the read finish before the view goes, so no case overlaps the next.
        ASSERT_TRUE(QThreadPool::globalInstance()->waitForDone(30000));
    }
}

#endif  // HAVE_KLAYOUT
