/**
 * test_coord_frame_contract.cpp - Verification tests for the
 * coord-frame contract (chiplet-studio/docs/coord_frame_contract.md).
 *
 * Two test groups:
 *   - Synth*: load coord_contract_synth.chiplet (a small fixture with
 *     exact, hand-computable values). Asserts the schema parses
 *     correctly and the documented 3D world mapping holds for every
 *     component without any tolerance.
 *   - WirebondDemo*: load the full wire-bond demo .chiplet (regenerated
 *     end-to-end via KiCad export + hyp_to_gds.py --update-chiplet-file).
 *     Asserts the canonical-frame post-conditions: per-component
 *     anchors, U1 mounting Z = 57.83 um (cupillar_opt1), all io_pads inside the
 *     interposer bbox (no HYP-absolute leaks). Path is resolved from
 *     the WIREBOND_DEMO_CHIPLET env var or a workspace-relative path;
 *     tests skip with a clear message if neither is reachable.
 */

#include <gtest/gtest.h>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "core/Assembly.h"
#include "core/Component.h"
#include "core/IOPad.h"
#include "formats/ChipletFormat.h"

namespace chiplet {
namespace {

std::string fixturePath(const std::string& filename)
{
    return std::string(FIXTURES_DIR) + "/" + filename;
}

// Locate the canonical wire-bond demo .chiplet. The chiplet lives in a
// sibling repo (kicad_designs), so it is not bundled with chiplet-studio
// test fixtures. Resolution order:
//   1. $WIREBOND_DEMO_CHIPLET (absolute or relative)
//   2. <FIXTURES_DIR>/../../../kicad_designs/interposer_wire_bonding_demo/
//      interposer_wire_bonding_demo.chiplet (default workspace layout)
// Returns an empty optional when nothing exists on disk; callers
// GTEST_SKIP with a message that points to the env var.
std::optional<std::string> locateWirebondDemoChiplet()
{
    if (const char* env = std::getenv("WIREBOND_DEMO_CHIPLET")) {
        std::filesystem::path p(env);
        if (std::filesystem::exists(p)) {
            return p.string();
        }
    }

    std::filesystem::path fixtures(FIXTURES_DIR);
    std::filesystem::path candidate = fixtures.parent_path()  // tests/
                                          .parent_path()      // chiplet-studio/
                                          .parent_path()      // workspace/
                                          / "kicad_designs"
                                          / "interposer_wire_bonding_demo"
                                          / "interposer_wire_bonding_demo.chiplet";
    if (std::filesystem::exists(candidate)) {
        return candidate.string();
    }
    return std::nullopt;
}

// World-space transform applied by AssemblyView::buildLayerGeometry
// (src/view3d/AssemblyView.cpp). Kept in sync by hand so we can assert
// the resolved 3D placement without instantiating an OpenGL context.
// chiplet (x, y, z) in um -> 3D world (x, z, -y) in mm.
struct WorldPosition {
    double x_mm;
    double y_mm;
    double z_mm;
};
WorldPosition componentWorldPosition(const Component& c)
{
    const auto& p = c.position();
    return WorldPosition{p.x / 1000.0, p.z / 1000.0, -p.y / 1000.0};
}

// ------------------------------------------------------------------
// Synthetic fixture — §7.1 of the contract.
// ------------------------------------------------------------------

class CoordFrameContractSynth : public ::testing::Test {
protected:
    void SetUp() override
    {
        ChipletFormat format;
        assembly = format.load(fixturePath("coord_contract_synth.chiplet"));
        ASSERT_NE(assembly, nullptr);
    }

    std::unique_ptr<Assembly> assembly;
};

TEST_F(CoordFrameContractSynth, AnchorsDeclared)
{
    auto* interposer = assembly->component("interposer");
    ASSERT_NE(interposer, nullptr);
    EXPECT_EQ(interposer->anchor(), Anchor::BboxCenter);
    EXPECT_TRUE(interposer->anchor_declared());

    auto* die_a = assembly->component("U_A");
    ASSERT_NE(die_a, nullptr);
    EXPECT_EQ(die_a->anchor(), Anchor::GdsOrigin);
    EXPECT_TRUE(die_a->anchor_declared());

    auto* die_b = assembly->component("U_B");
    ASSERT_NE(die_b, nullptr);
    EXPECT_EQ(die_b->anchor(), Anchor::GdsOrigin);
    EXPECT_TRUE(die_b->anchor_declared());
}

TEST_F(CoordFrameContractSynth, CenterPositions)
{
    auto* interposer = assembly->component("interposer");
    ASSERT_NE(interposer, nullptr);
    EXPECT_DOUBLE_EQ(interposer->position().x, 500.0);
    EXPECT_DOUBLE_EQ(interposer->position().y, 500.0);
    EXPECT_DOUBLE_EQ(interposer->position().z, 0.0);

    auto* die_a = assembly->component("U_A");
    ASSERT_NE(die_a, nullptr);
    EXPECT_DOUBLE_EQ(die_a->position().x, 250.0);
    EXPECT_DOUBLE_EQ(die_a->position().y, 250.0);
    EXPECT_DOUBLE_EQ(die_a->position().z, 50.0);

    auto* die_b = assembly->component("U_B");
    ASSERT_NE(die_b, nullptr);
    EXPECT_DOUBLE_EQ(die_b->position().x, 750.0);
    EXPECT_DOUBLE_EQ(die_b->position().y, 750.0);
    EXPECT_DOUBLE_EQ(die_b->position().z, 50.0);
}

TEST_F(CoordFrameContractSynth, Dimensions)
{
    auto* interposer = assembly->component("interposer");
    ASSERT_NE(interposer, nullptr);
    EXPECT_DOUBLE_EQ(interposer->dimensions().width, 1000.0);
    EXPECT_DOUBLE_EQ(interposer->dimensions().height, 1000.0);
    EXPECT_DOUBLE_EQ(interposer->dimensions().thickness, 13.83);

    for (const char* id : {"U_A", "U_B"}) {
        auto* die = assembly->component(id);
        ASSERT_NE(die, nullptr) << id;
        EXPECT_DOUBLE_EQ(die->dimensions().width, 100.0) << id;
        EXPECT_DOUBLE_EQ(die->dimensions().height, 100.0) << id;
        EXPECT_DOUBLE_EQ(die->dimensions().thickness, 50.0) << id;
    }
}

TEST_F(CoordFrameContractSynth, IOPadsParsedAtCorners)
{
    auto* interposer = assembly->component("interposer");
    ASSERT_NE(interposer, nullptr);

    const auto& pads = interposer->io_pads();
    ASSERT_EQ(pads.size(), 4u);

    auto findPad = [&](const std::string& id) -> const IOPad* {
        for (const auto& p : pads) {
            if (p.id() == id) return &p;
        }
        return nullptr;
    };

    struct Expected { const char* id; double x; double y; };
    const Expected expectations[] = {
        {"P_BL",  50.0,  50.0},
        {"P_BR", 950.0,  50.0},
        {"P_TL",  50.0, 950.0},
        {"P_TR", 950.0, 950.0},
    };
    for (const auto& e : expectations) {
        const IOPad* p = findPad(e.id);
        ASSERT_NE(p, nullptr) << e.id;
        EXPECT_DOUBLE_EQ(p->position().x, e.x) << e.id;
        EXPECT_DOUBLE_EQ(p->position().y, e.y) << e.id;
        EXPECT_DOUBLE_EQ(p->size().x, 50.0) << e.id;
        EXPECT_DOUBLE_EQ(p->size().y, 50.0) << e.id;
        EXPECT_EQ(p->layer(), "TopMetal2") << e.id;
        EXPECT_EQ(p->io_class(), IOClass::WireBond) << e.id;
    }
}

// 3D world mapping reproduced from AssemblyView::buildLayerGeometry
// (line 1500-1502): (x_um/1000, z_um/1000, -y_um/1000) in mm. With the
// synth positions this maps to:
//   interposer (500, 500, 0)  -> (0.5,  0.0,  -0.5)
//   Die A      (250, 250, 50) -> (0.25, 0.05, -0.25)
//   Die B      (750, 750, 50) -> (0.75, 0.05, -0.75)
TEST_F(CoordFrameContractSynth, WorldPositionMatchesContract)
{
    auto* interposer = assembly->component("interposer");
    ASSERT_NE(interposer, nullptr);
    WorldPosition w = componentWorldPosition(*interposer);
    EXPECT_DOUBLE_EQ(w.x_mm, 0.5);
    EXPECT_DOUBLE_EQ(w.y_mm, 0.0);
    EXPECT_DOUBLE_EQ(w.z_mm, -0.5);

    auto* die_a = assembly->component("U_A");
    ASSERT_NE(die_a, nullptr);
    w = componentWorldPosition(*die_a);
    EXPECT_DOUBLE_EQ(w.x_mm, 0.25);
    EXPECT_DOUBLE_EQ(w.y_mm, 0.05);
    EXPECT_DOUBLE_EQ(w.z_mm, -0.25);

    auto* die_b = assembly->component("U_B");
    ASSERT_NE(die_b, nullptr);
    w = componentWorldPosition(*die_b);
    EXPECT_DOUBLE_EQ(w.x_mm, 0.75);
    EXPECT_DOUBLE_EQ(w.y_mm, 0.05);
    EXPECT_DOUBLE_EQ(w.z_mm, -0.75);
}

// ------------------------------------------------------------------
// Wire-bond demo round-trip — §7.2 of the contract.
//
// The expected values are the post-Gate-3 canonical .chiplet produced
// by KiCad export + hyp_to_gds.py --update-chiplet-file. Numbers are
// taken from the regenerated demo (Gate 3 of coord-frame contract
// execution).
// ------------------------------------------------------------------

class CoordFrameContractWirebondDemo : public ::testing::Test {
protected:
    void SetUp() override
    {
        auto path = locateWirebondDemoChiplet();
        if (!path.has_value()) {
            GTEST_SKIP() << "Wire-bond demo .chiplet not found. Set "
                         << "WIREBOND_DEMO_CHIPLET or run from the "
                         << "default workspace layout.";
        }
        ChipletFormat format;
        assembly = format.load(*path);
        ASSERT_NE(assembly, nullptr);
    }

    std::unique_ptr<Assembly> assembly;
};

TEST_F(CoordFrameContractWirebondDemo, Anchors)
{
    auto* interposer = assembly->component("interposer");
    ASSERT_NE(interposer, nullptr);
    EXPECT_EQ(interposer->anchor(), Anchor::BboxCenter);
    EXPECT_TRUE(interposer->anchor_declared());

    auto* u1 = assembly->component("U1");
    ASSERT_NE(u1, nullptr);
    EXPECT_EQ(u1->anchor(), Anchor::GdsOrigin);
    EXPECT_TRUE(u1->anchor_declared());
}

// U1 sits on cupillar_opt1 (28 um CuPillar + 16 um SnAgCap = 44 um
// stack) over the interposer TopMetal2 (z_top = 13.83 um). Final z =
// 13.83 + 44 = 57.83. Option 1 is the die's design rule set: U1's pad
// ring has a 79.93 um-pitch pair (sout/GND), legal at Option 1's 75 um
// minimum but below Option 2's 80 um -- the canonical regen therefore
// selects cupillar_opt1 for U1 (per-die CONNECTION field). XY are the
// GDS-bbox-corner placement values for the two-die wire-bond demo;
// since the converter draws the board outline on prBoundary 189/0 the
// canonical origin is the board outline corner (Edge.Cuts), routing
// independent. Their exact numeric values are the regression witness
// against the 6 previous alignment incidents.
TEST_F(CoordFrameContractWirebondDemo, U1Position)
{
    auto* u1 = assembly->component("U1");
    ASSERT_NE(u1, nullptr);
    EXPECT_NEAR(u1->position().x, 1954.12, 0.1);
    EXPECT_NEAR(u1->position().y, 2332.48, 0.1);
    EXPECT_NEAR(u1->position().z, 57.83,   0.01);
}

// U2 sits on vendorx_microbump (18 um Cu + 6 um cap = 24 um stack):
// z = 13.83 + 24 = 37.83. The per-die method comes from U2's
// CONNECTION footprint field; this locks the mixed-method demo (U1
// IHP cu-pillar + U2 vendor microbump in one export).
TEST_F(CoordFrameContractWirebondDemo, U2Position)
{
    auto* u2 = assembly->component("U2");
    ASSERT_NE(u2, nullptr);
    EXPECT_NEAR(u2->position().x, 5170.23, 0.1);
    EXPECT_NEAR(u2->position().y, 2420.27, 0.1);
    EXPECT_NEAR(u2->position().z, 37.83,   0.01);
}

TEST_F(CoordFrameContractWirebondDemo, InterposerPosition)
{
    auto* interposer = assembly->component("interposer");
    ASSERT_NE(interposer, nullptr);
    // Interposer position is the bbox center in canonical (GDS-bbox-corner)
    // coordinates: width/2, height/2, 0. Dimensions are the board
    // outline (prBoundary 189/0 = KiCad Edge.Cuts), not the drawn-copper
    // extent, per coord_frame_contract.md section 1.5.
    EXPECT_NEAR(interposer->position().x, 3246.16, 0.1);
    EXPECT_NEAR(interposer->position().y, 2801.00, 0.1);
    EXPECT_NEAR(interposer->position().z,    0.00, 0.01);
    EXPECT_NEAR(interposer->dimensions().width,  6492.31, 0.1);
    EXPECT_NEAR(interposer->dimensions().height, 5602.00, 0.1);
    EXPECT_NEAR(interposer->dimensions().thickness, 13.83, 0.01);
}

// Pre-Gate-3 the io_pads pass-through left them in HYP-absolute
// (~1.3e5 um). Now every pad must sit inside the interposer extent,
// which for the wire-bond demo is the 6492.312 x 5602.001 um board
// outline.
TEST_F(CoordFrameContractWirebondDemo, IOPadsInCanonicalFrame)
{
    auto* interposer = assembly->component("interposer");
    ASSERT_NE(interposer, nullptr);

    const double w = interposer->dimensions().width;
    const double h = interposer->dimensions().height;
    ASSERT_GT(w, 0.0);
    ASSERT_GT(h, 0.0);

    const auto& pads = interposer->io_pads();
    ASSERT_FALSE(pads.empty()) << "Demo regenerated without io_pads";

    for (const auto& pad : pads) {
        const double x = pad.position().x;
        const double y = pad.position().y;
        EXPECT_GE(x, 0.0) << "pad " << pad.id() << " x outside frame";
        EXPECT_LE(x, w)   << "pad " << pad.id() << " x outside frame";
        EXPECT_GE(y, 0.0) << "pad " << pad.id() << " y outside frame";
        EXPECT_LE(y, h)   << "pad " << pad.id() << " y outside frame";
    }
}

// Load alone is the assertion: if the file still carried
// _metadata.finalize_required: true (KiCad intermediate output), the
// Gate 1 loader would have thrown ChipletFormatException at SetUp() and
// the fixture would never reach here.
TEST_F(CoordFrameContractWirebondDemo, FinalizedFileLoaded)
{
    ASSERT_NE(assembly, nullptr);
    EXPECT_FALSE(assembly->name().empty());
}

}  // namespace
}  // namespace chiplet
