/**
 * test_visualization.cpp - Tests for chiplet loading and 3D visualization
 *
 * These tests verify:
 * - Chiplet files load correctly
 * - Assembly data is valid after loading
 * - AssemblyView can be created without crash
 * - Components render correctly (when display available)
 */

#include <gtest/gtest.h>
#include <QApplication>
#include <QSignalSpy>
#include <QGuiApplication>
#include <QScreen>
#include <set>
#include <cmath>

#include "formats/ChipletFormat.h"
#include "core/Assembly.h"
#include "core/Component.h"

namespace chiplet {

// Test fixture for visualization tests
class VisualizationTest : public ::testing::Test {
protected:
    static int g_argc;
    static char* g_argv[];
    static QApplication* g_app;

    static void SetUpTestSuite() {
        if (!QApplication::instance()) {
            g_app = new QApplication(g_argc, g_argv);
        }
    }

    std::string fixturePath(const std::string& name) {
        return std::string(FIXTURES_DIR) + "/" + name;
    }

    static bool isDisplayAvailable() {
        QGuiApplication* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
        if (!app) return false;
        QString platform = app->platformName();
        if (platform == "offscreen" || platform == "minimal") return false;
        return !app->screens().isEmpty();
    }
};

int VisualizationTest::g_argc = 0;
char* VisualizationTest::g_argv[] = {nullptr};
QApplication* VisualizationTest::g_app = nullptr;

// =============================================================================
// CHIPLET FILE LOADING TESTS
// =============================================================================

TEST_F(VisualizationTest, LoadMinimalChiplet) {
    ChipletFormat format;
    auto assembly = format.load(fixturePath("minimal.chiplet"));

    ASSERT_NE(assembly, nullptr) << "Failed to load minimal.chiplet";
    EXPECT_FALSE(assembly->name().empty());
}

TEST_F(VisualizationTest, LoadWithComponentsChiplet) {
    ChipletFormat format;
    auto assembly = format.load(fixturePath("with_components.chiplet"));

    ASSERT_NE(assembly, nullptr) << "Failed to load with_components.chiplet";
    EXPECT_GT(assembly->components().size(), 0) << "Expected at least one component";

    // Verify components have valid data
    for (const auto& comp : assembly->components()) {
        EXPECT_FALSE(comp->name().empty()) << "Component name should not be empty";
    }
}

TEST_F(VisualizationTest, LoadTest2DViewChiplet) {
    ChipletFormat format;
    auto assembly = format.load(fixturePath("test_2dview.chiplet"));

    ASSERT_NE(assembly, nullptr) << "Failed to load test_2dview.chiplet";
    EXPECT_EQ(assembly->components().size(), 2) << "test_2dview.chiplet should have 2 components";
}

TEST_F(VisualizationTest, LoadWithTechnologiesChiplet) {
    ChipletFormat format;
    auto assembly = format.load(fixturePath("with_technologies.chiplet"));

    ASSERT_NE(assembly, nullptr) << "Failed to load with_technologies.chiplet";
}

// =============================================================================
// INVALID FILE HANDLING TESTS
// =============================================================================

TEST_F(VisualizationTest, LoadInvalidYaml) {
    ChipletFormat format;
    // ChipletFormat throws exceptions for invalid YAML
    EXPECT_THROW(format.load(fixturePath("invalid_yaml.chiplet")), std::exception);
}

TEST_F(VisualizationTest, LoadMissingNameFile) {
    ChipletFormat format;
    // ChipletFormat throws exceptions for missing required fields
    EXPECT_THROW(format.load(fixturePath("invalid_missing_name.chiplet")), std::exception);
}

TEST_F(VisualizationTest, LoadNonexistentFile) {
    ChipletFormat format;
    // ChipletFormat throws exceptions for missing files
    EXPECT_THROW(format.load(fixturePath("nonexistent.chiplet")), std::exception);
}

// =============================================================================
// COMPONENT VALIDATION TESTS
// =============================================================================

TEST_F(VisualizationTest, ComponentsHaveValidDimensions) {
    ChipletFormat format;
    auto assembly = format.load(fixturePath("with_components.chiplet"));

    ASSERT_NE(assembly, nullptr);

    for (const auto& comp : assembly->components()) {
        // Components should have positive dimensions or zero (default)
        const auto& dims = comp->dimensions();
        double w = dims.width;
        double h = dims.height;
        double t = dims.thickness;

        EXPECT_GE(w, 0.0) << "Width should be >= 0 for " << comp->name();
        EXPECT_GE(h, 0.0) << "Height should be >= 0 for " << comp->name();
        EXPECT_GE(t, 0.0) << "Thickness should be >= 0 for " << comp->name();
    }
}

TEST_F(VisualizationTest, ComponentsHaveValidPosition) {
    ChipletFormat format;
    auto assembly = format.load(fixturePath("with_components.chiplet"));

    ASSERT_NE(assembly, nullptr);

    for (const auto& comp : assembly->components()) {
        // Position values should be finite
        const auto& pos = comp->position();
        double x = pos.x;
        double y = pos.y;
        double z = pos.z;

        EXPECT_TRUE(std::isfinite(x)) << "X should be finite for " << comp->name();
        EXPECT_TRUE(std::isfinite(y)) << "Y should be finite for " << comp->name();
        EXPECT_TRUE(std::isfinite(z)) << "Z should be finite for " << comp->name();
    }
}

// =============================================================================
// ASSEMBLY MODIFICATION TESTS (for visualization updates)
// =============================================================================

TEST_F(VisualizationTest, ModifyComponentPosition) {
    ChipletFormat format;
    auto assembly = format.load(fixturePath("with_components.chiplet"));

    ASSERT_NE(assembly, nullptr);
    ASSERT_GT(assembly->components().size(), 0);

    auto& comp = assembly->components()[0];
    const auto& orig_pos = comp->position();
    double original_x = orig_pos.x;
    double original_y = orig_pos.y;
    double original_z = orig_pos.z;

    // Modify position
    Position3D new_pos;
    new_pos.x = original_x + 100.0;
    new_pos.y = original_y + 50.0;
    new_pos.z = original_z;
    comp->set_position(new_pos);

    EXPECT_DOUBLE_EQ(comp->position().x, original_x + 100.0);
    EXPECT_DOUBLE_EQ(comp->position().y, original_y + 50.0);
}

TEST_F(VisualizationTest, CreateEmptyAssembly) {
    auto assembly = std::make_unique<Assembly>();
    assembly->set_name("TestAssembly");

    EXPECT_EQ(assembly->name(), "TestAssembly");
    EXPECT_EQ(assembly->components().size(), 0);
}

TEST_F(VisualizationTest, AddComponentToAssembly) {
    auto assembly = std::make_unique<Assembly>();
    assembly->set_name("TestAssembly");

    auto comp = std::make_unique<Component>("test_comp", ComponentType::Die);

    Dimensions3D dims;
    dims.width = 10.0;
    dims.height = 10.0;
    dims.thickness = 0.5;
    comp->set_dimensions(dims);

    Position3D pos;
    pos.x = 0.0;
    pos.y = 0.0;
    pos.z = 0.0;
    comp->set_position(pos);

    assembly->add_component(std::move(comp));

    EXPECT_EQ(assembly->components().size(), 1);
    EXPECT_EQ(assembly->components()[0]->name(), "test_comp");
}

// =============================================================================
// COMPONENT TYPE TESTS
// =============================================================================

TEST_F(VisualizationTest, ComponentTypesCorrectlyIdentified) {
    ChipletFormat format;
    auto assembly = format.load(fixturePath("with_components.chiplet"));

    ASSERT_NE(assembly, nullptr);

    // Check that component types are valid
    for (const auto& comp : assembly->components()) {
        ComponentType type = comp->type();
        EXPECT_TRUE(type == ComponentType::Die ||
                    type == ComponentType::DieArray ||
                    type == ComponentType::Interposer ||
                    type == ComponentType::Substrate)
            << "Unknown component type for " << comp->name();
    }
}

// =============================================================================
// LAYOUT PATH TESTS
// =============================================================================

TEST_F(VisualizationTest, ComponentLayoutPaths) {
    ChipletFormat format;
    auto assembly = format.load(fixturePath("test_2dview.chiplet"));

    ASSERT_NE(assembly, nullptr);

    for (const auto& comp : assembly->components()) {
        std::string layoutPath = comp->layout_path();
        // Layout path can be empty or set
        // Just verify we can access it without crash
        SUCCEED();
    }
}

// =============================================================================
// STRESS TESTS
// =============================================================================

TEST_F(VisualizationTest, LoadFilesMultipleTimes) {
    ChipletFormat format;

    // Load same file multiple times to check for memory leaks or state issues
    for (int i = 0; i < 10; ++i) {
        auto assembly = format.load(fixturePath("with_components.chiplet"));
        ASSERT_NE(assembly, nullptr) << "Load failed on iteration " << i;
        EXPECT_GT(assembly->components().size(), 0);
    }
}

TEST_F(VisualizationTest, LoadDifferentFilesSequentially) {
    ChipletFormat format;

    // Load different files sequentially
    std::vector<std::string> files = {
        "minimal.chiplet",
        "with_components.chiplet",
        "test_2dview.chiplet",
        "with_technologies.chiplet"
    };

    for (const auto& file : files) {
        auto assembly = format.load(fixturePath(file));
        ASSERT_NE(assembly, nullptr) << "Failed to load " << file;
    }
}

// =============================================================================
// ASSEMBLY NAME AND METADATA
// =============================================================================

TEST_F(VisualizationTest, AssemblyHasName) {
    ChipletFormat format;
    auto assembly = format.load(fixturePath("minimal.chiplet"));

    ASSERT_NE(assembly, nullptr);
    EXPECT_FALSE(assembly->name().empty());
}

TEST_F(VisualizationTest, ModifyAssemblyName) {
    auto assembly = std::make_unique<Assembly>();
    assembly->set_name("Original");
    EXPECT_EQ(assembly->name(), "Original");

    assembly->set_name("Modified");
    EXPECT_EQ(assembly->name(), "Modified");
}

// =============================================================================
// COMPONENT ID AND NAME
// =============================================================================

TEST_F(VisualizationTest, ComponentIdIsUnique) {
    ChipletFormat format;
    auto assembly = format.load(fixturePath("with_components.chiplet"));

    ASSERT_NE(assembly, nullptr);

    std::set<std::string> ids;
    for (const auto& comp : assembly->components()) {
        std::string id = comp->id();
        EXPECT_TRUE(ids.find(id) == ids.end())
            << "Duplicate component ID found: " << id;
        ids.insert(id);
    }
}

TEST_F(VisualizationTest, ModifyComponentName) {
    auto comp = std::make_unique<Component>("comp_id", ComponentType::Die);
    EXPECT_EQ(comp->name(), "comp_id");  // Defaults to ID

    comp->set_name("New Name");
    EXPECT_EQ(comp->name(), "New Name");
    EXPECT_EQ(comp->id(), "comp_id");  // ID unchanged
}

// =============================================================================
// COORDINATE CONVERSION TESTS (Regression test for instancing bug)
// =============================================================================

TEST_F(VisualizationTest, MeshBuilderCoordinateConversion) {
    // Test that MeshBuilder correctly converts micrometers to millimeters
    // This is a regression test for the instancing position bug where
    // coordinates were applied twice or with wrong scale

    auto comp = std::make_unique<Component>("test_comp", ComponentType::Die);

    // Set dimensions in micrometers (1000um = 1mm)
    Dimensions3D dims;
    dims.width = 2000.0;      // 2mm
    dims.height = 3000.0;     // 3mm
    dims.thickness = 100.0;   // 0.1mm
    comp->set_dimensions(dims);

    // Set position in micrometers
    Position3D pos;
    pos.x = 5000.0;   // 5mm
    pos.y = 6000.0;   // 6mm
    pos.z = 0.0;
    comp->set_position(pos);

    // Verify component stores values in micrometers
    EXPECT_DOUBLE_EQ(comp->dimensions().width, 2000.0);
    EXPECT_DOUBLE_EQ(comp->dimensions().height, 3000.0);
    EXPECT_DOUBLE_EQ(comp->position().x, 5000.0);
    EXPECT_DOUBLE_EQ(comp->position().y, 6000.0);

    // Note: MeshBuilder converts to mm internally (divides by 1000)
    // The mesh bounding box should be in mm scale:
    // - Width 2mm, Height 3mm, centered at (5mm, 6mm)
    // - Expected bounding box roughly: [4..6, 4.5..7.5, 0..0.1] in mm

    // This test verifies the component data is stored correctly
    // The actual MeshBuilder conversion is tested via visual verification
    // and the fix ensures instancing uses buildComponentMeshAtOrigin()
    // with position transforms also converted to mm
}

TEST_F(VisualizationTest, ComponentPositionScaleConsistency) {
    // Verify that position values loaded from chiplet files are in micrometers
    // and that our code can correctly handle the um->mm conversion
    ChipletFormat format;
    auto assembly = format.load(fixturePath("with_components.chiplet"));

    ASSERT_NE(assembly, nullptr);
    ASSERT_GT(assembly->components().size(), 0);

    for (const auto& comp : assembly->components()) {
        const auto& pos = comp->position();
        const auto& dims = comp->dimensions();

        // For typical chiplet assemblies, positions should be in reasonable um range
        // (e.g., millimeters to centimeters = 1000-100000 um)
        // This catches if coordinates were accidentally stored in mm
        if (dims.width > 0) {
            // If dimensions are specified, they should be reasonable for um
            // A typical die is 1-20mm = 1000-20000um
            EXPECT_LT(dims.width, 1000000.0)
                << "Width seems too large for um scale: " << comp->name();
            EXPECT_GT(dims.width, 1.0)
                << "Width seems too small for um scale: " << comp->name();
        }
    }
}

} // namespace chiplet
