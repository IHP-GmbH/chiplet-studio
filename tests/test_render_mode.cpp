/**
 * test_render_mode.cpp - Tests for per-component render modes
 */

#include <gtest/gtest.h>
#include "core/Component.h"
#include "core/Assembly.h"
#include "core/CommandProcessor.h"
#include "core/CommandFactory.h"
#include "core/commands/CmdSetRenderMode.h"
#include "formats/ChipletFormat.h"
#include <QApplication>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <optional>

using namespace chiplet;

// Helper to create a test assembly with various component types
static std::unique_ptr<Assembly> createTestAssembly()
{
    auto assembly = std::make_unique<Assembly>();
    assembly->set_name("RenderModeTest");

    auto substrate = std::make_unique<Component>("substrate", ComponentType::Substrate);
    substrate->set_dimensions({10000, 10000, 500});
    substrate->set_position({0, 0, 0});
    assembly->add_component(std::move(substrate));

    auto interposer = std::make_unique<Component>("interposer", ComponentType::Interposer);
    interposer->set_dimensions({8000, 8000, 100});
    interposer->set_position({1000, 1000, 500});
    assembly->add_component(std::move(interposer));

    auto die_a = std::make_unique<Component>("die_a", ComponentType::Die);
    die_a->set_dimensions({3000, 3000, 200});
    die_a->set_position({1500, 1500, 600});
    assembly->add_component(std::move(die_a));

    auto die_b = std::make_unique<Component>("die_b", ComponentType::Die);
    die_b->set_dimensions({3000, 3000, 200});
    die_b->set_position({5000, 1500, 600});
    assembly->add_component(std::move(die_b));

    auto die_array = std::make_unique<Component>("die_array", ComponentType::DieArray);
    die_array->set_dimensions({2000, 2000, 150});
    die_array->set_position({2000, 5000, 600});
    assembly->add_component(std::move(die_array));

    return assembly;
}

// --- Default mode tests ---

TEST(RenderModeTest, DefaultModeForDie)
{
    Component die("test_die", ComponentType::Die);
    EXPECT_EQ(die.render_mode(), RenderMode::Transparent);
}

TEST(RenderModeTest, DefaultModeForDieArray)
{
    Component dieArray("test_die_array", ComponentType::DieArray);
    EXPECT_EQ(dieArray.render_mode(), RenderMode::Transparent);
}

TEST(RenderModeTest, DefaultModeForInterposer)
{
    Component interposer("test_interposer", ComponentType::Interposer);
    EXPECT_EQ(interposer.render_mode(), RenderMode::Transparent);
}

TEST(RenderModeTest, DefaultModeForSubstrate)
{
    Component substrate("test_substrate", ComponentType::Substrate);
    EXPECT_EQ(substrate.render_mode(), RenderMode::Solid);
}

TEST(RenderModeTest, SetAndGetMode)
{
    Component comp("test", ComponentType::Die);

    RenderMode modes[] = {
        RenderMode::Hidden,
        RenderMode::Wireframe,
        RenderMode::Transparent,
        RenderMode::Solid,
        RenderMode::Detailed
    };

    for (RenderMode mode : modes) {
        comp.set_render_mode(mode);
        EXPECT_EQ(comp.render_mode(), mode);
    }
}

// --- CmdSetRenderMode tests ---

class CmdSetRenderModeTest : public ::testing::Test {
protected:
    static void SetUpTestSuite()
    {
        if (!QApplication::instance()) {
            static int argc = 1;
            static char* argv[] = {const_cast<char*>("test"), nullptr};
            static QApplication app(argc, argv);
        }
        CommandFactory::register_builtin_commands();
    }
};

TEST_F(CmdSetRenderModeTest, ExecuteChangesMode)
{
    auto assembly = createTestAssembly();
    Component* die = assembly->component("die_a");
    ASSERT_NE(die, nullptr);
    EXPECT_EQ(die->render_mode(), RenderMode::Transparent);

    CmdSetRenderMode cmd("die_a", RenderMode::Transparent, RenderMode::Solid);
    EXPECT_TRUE(cmd.execute(*assembly));
    EXPECT_EQ(die->render_mode(), RenderMode::Solid);
}

TEST_F(CmdSetRenderModeTest, UndoRestoresMode)
{
    auto assembly = createTestAssembly();
    Component* die = assembly->component("die_a");
    ASSERT_NE(die, nullptr);

    CmdSetRenderMode cmd("die_a", RenderMode::Transparent, RenderMode::Hidden);
    cmd.execute(*assembly);
    EXPECT_EQ(die->render_mode(), RenderMode::Hidden);

    cmd.undo(*assembly);
    EXPECT_EQ(die->render_mode(), RenderMode::Transparent);
}

TEST_F(CmdSetRenderModeTest, RedoReappliesMode)
{
    auto assembly = createTestAssembly();
    CommandProcessor processor(assembly.get());

    Component* die = assembly->component("die_a");
    ASSERT_NE(die, nullptr);
    EXPECT_EQ(die->render_mode(), RenderMode::Transparent);

    auto cmd = std::make_unique<CmdSetRenderMode>("die_a", RenderMode::Transparent, RenderMode::Wireframe);
    processor.execute(std::move(cmd));
    EXPECT_EQ(die->render_mode(), RenderMode::Wireframe);

    processor.undo();
    EXPECT_EQ(die->render_mode(), RenderMode::Transparent);

    processor.redo();
    EXPECT_EQ(die->render_mode(), RenderMode::Wireframe);
}

TEST_F(CmdSetRenderModeTest, Serialize)
{
    CmdSetRenderMode cmd("die_a", RenderMode::Transparent, RenderMode::Detailed);
    nlohmann::json j = cmd.serialize();

    EXPECT_EQ(j["type"], "SetRenderMode");
    EXPECT_EQ(j["data"]["id"], "die_a");
    EXPECT_EQ(j["data"]["old"], static_cast<int>(RenderMode::Transparent));
    EXPECT_EQ(j["data"]["new"], static_cast<int>(RenderMode::Detailed));
}

TEST_F(CmdSetRenderModeTest, Deserialize)
{
    nlohmann::json j = {
        {"type", "SetRenderMode"},
        {"data", {
            {"id", "interposer"},
            {"old", static_cast<int>(RenderMode::Transparent)},
            {"new", static_cast<int>(RenderMode::Solid)}
        }}
    };

    auto cmd = CmdSetRenderMode::deserialize(j);
    ASSERT_NE(cmd, nullptr);

    auto* setCmd = dynamic_cast<CmdSetRenderMode*>(cmd.get());
    ASSERT_NE(setCmd, nullptr);
    EXPECT_EQ(setCmd->component_id(), "interposer");
    EXPECT_EQ(setCmd->old_mode(), RenderMode::Transparent);
    EXPECT_EQ(setCmd->new_mode(), RenderMode::Solid);
}

TEST_F(CmdSetRenderModeTest, FactoryRegistered)
{
    EXPECT_TRUE(CommandFactory::is_registered("SetRenderMode"));
}

TEST_F(CmdSetRenderModeTest, ExecuteFailsForMissingComponent)
{
    auto assembly = createTestAssembly();
    CmdSetRenderMode cmd("nonexistent", RenderMode::Transparent, RenderMode::Solid);
    EXPECT_FALSE(cmd.execute(*assembly));
}

// --- Sorting tests ---

TEST(RenderSortingTest, BackToFrontOrder)
{
    // Create components at known positions and verify sorting
    auto assembly = std::make_unique<Assembly>();

    // Component closest to origin (should be last after sorting)
    auto near = std::make_unique<Component>("near", ComponentType::Die);
    near->set_position({100, 100, 0});
    near->set_dimensions({100, 100, 10});
    assembly->add_component(std::move(near));

    // Component in the middle
    auto mid = std::make_unique<Component>("mid", ComponentType::Die);
    mid->set_position({5000, 5000, 0});
    mid->set_dimensions({100, 100, 10});
    assembly->add_component(std::move(mid));

    // Component farthest from origin (should be first after sorting)
    auto far = std::make_unique<Component>("far", ComponentType::Die);
    far->set_position({10000, 10000, 0});
    far->set_dimensions({100, 100, 10});
    assembly->add_component(std::move(far));

    // Simulate camera at origin (0,0,0) and sort by distance
    // Component centers (in um): near=(150,150,5), mid=(5050,5050,5), far=(10050,10050,5)
    // Distances^2: near ~ 45000, mid ~ 51005000, far ~ 202005000

    std::vector<std::pair<std::string, double>> distances;
    for (const auto& comp : assembly->components()) {
        const auto& pos = comp->position();
        const auto& dims = comp->dimensions();
        double cx = pos.x + dims.width / 2.0;
        double cy = pos.y + dims.height / 2.0;
        double cz = pos.z + dims.thickness / 2.0;
        double dist2 = cx * cx + cy * cy + cz * cz;
        distances.push_back({comp->id(), dist2});
    }

    // Sort back-to-front (farthest first)
    std::sort(distances.begin(), distances.end(),
        [](const auto& a, const auto& b) { return a.second > b.second; });

    ASSERT_EQ(distances.size(), 3u);
    EXPECT_EQ(distances[0].first, "far");
    EXPECT_EQ(distances[1].first, "mid");
    EXPECT_EQ(distances[2].first, "near");
}

// --- Auto-Detailed promotion for flip-chip dies (task #8) ---
//
// Loading a .chiplet whose component declares `orientation: flip_chip`
// (or `face_down`) promotes its render_mode from the constructor
// default (Transparent) to Detailed. The promotion only fires when
// render_mode is still the default, so future render_mode persistence
// can override the policy.
//
// Implementation lives in ChipletFormat::parse_component (loader-side
// policy, not a Component invariant — keeps the data class pure).

namespace {

std::string renderModeFixturePath(const std::string& filename)
{
    return std::string(FIXTURES_DIR) + "/" + filename;
}

// Locate the canonical wire-bond demo .chiplet so the integration
// test below can pin the post-Gate-7 invariant "U1 (flip_chip) loads
// with render_mode == Detailed" on the real demo, not just synthetics.
// Resolution order: env var (set by CI/Docker), workspace-relative
// fallback, then GTEST_SKIP so the test stays portable.
std::optional<std::string> locateWirebondDemoChiplet()
{
    if (const char* env = std::getenv("WIREBOND_DEMO_CHIPLET")) {
        std::filesystem::path p(env);
        if (std::filesystem::exists(p))
            return p.string();
    }
    std::filesystem::path fixtures(FIXTURES_DIR);
    std::filesystem::path candidate = fixtures.parent_path()
        .parent_path().parent_path()
        / "kicad_designs" / "interposer_wire_bonding_demo"
        / "interposer_wire_bonding_demo.chiplet";
    if (std::filesystem::exists(candidate))
        return candidate.string();
    return std::nullopt;
}

}  // namespace

TEST(RenderModeTest, FlipChipDieAutoPromotedToDetailed)
{
    ChipletFormat format;
    auto assembly = format.load(renderModeFixturePath("flip_chip_render_mode.chiplet"));
    ASSERT_NE(assembly, nullptr);

    Component* flip = assembly->component("U_flip");
    ASSERT_NE(flip, nullptr);
    EXPECT_EQ(flip->orientation(), Orientation::FaceDown);
    EXPECT_EQ(flip->render_mode(), RenderMode::Detailed)
        << "flip-chip die must be auto-promoted to Detailed on parse";
}

TEST(RenderModeTest, FaceUpDieKeepsTransparentDefault)
{
    ChipletFormat format;
    auto assembly = format.load(renderModeFixturePath("flip_chip_render_mode.chiplet"));
    ASSERT_NE(assembly, nullptr);

    Component* faceup = assembly->component("U_faceup");
    ASSERT_NE(faceup, nullptr);
    EXPECT_EQ(faceup->orientation(), Orientation::FaceUp);
    EXPECT_EQ(faceup->render_mode(), RenderMode::Transparent)
        << "face-up die must keep the constructor default";
}

TEST(RenderModeTest, ProgrammaticOrientationDoesNotChangeRenderMode)
{
    // The auto-promote policy is parser-side (Option B). Calling
    // set_orientation programmatically (no loader involved) must NOT
    // mutate render_mode — the Component class is a pure data carrier.
    Component die("test_die", ComponentType::Die);
    ASSERT_EQ(die.render_mode(), RenderMode::Transparent);

    die.set_orientation(Orientation::FaceDown);
    EXPECT_EQ(die.orientation(), Orientation::FaceDown);
    EXPECT_EQ(die.render_mode(), RenderMode::Transparent)
        << "set_orientation alone must not promote render_mode "
           "(policy lives in the loader, not the data class)";
}

TEST(RenderModeTest, WirebondDemoU1AutoPromotedToDetailed)
{
    // Integration check on the canonical wire-bond demo .chiplet: U1
    // is declared flip_chip there, so post-Gate-7 it must load with
    // render_mode == Detailed without manual user intervention. This
    // matches the visual state used during Gate 4 verification.
    auto path = locateWirebondDemoChiplet();
    if (!path) {
        GTEST_SKIP() << "wire-bond demo .chiplet not located (set "
                        "WIREBOND_DEMO_CHIPLET to enable this test)";
    }

    ChipletFormat format;
    auto assembly = format.load(*path);
    ASSERT_NE(assembly, nullptr);

    Component* u1 = assembly->component("U1");
    ASSERT_NE(u1, nullptr) << "U1 not found in " << *path;
    EXPECT_EQ(u1->orientation(), Orientation::FaceDown);
    EXPECT_EQ(u1->render_mode(), RenderMode::Detailed)
        << "wire-bond demo U1 (flip_chip) must auto-promote to Detailed";
}
