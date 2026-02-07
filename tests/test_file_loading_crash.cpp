/**
 * test_file_loading_crash.cpp - Tests for file loading crash detection
 *
 * These tests specifically target the crash that occurs when loading
 * .chiplet files with components. They verify that the parser and
 * assembly creation work correctly without crashing.
 */

#include <gtest/gtest.h>
#include <cmath>
#include "formats/ChipletFormat.h"
#include "core/Assembly.h"
#include "core/Component.h"

namespace chiplet {
namespace {

// Helper to get fixture path
std::string fixturePath(const std::string& filename) {
    return std::string(FIXTURES_DIR) + "/" + filename;
}

class FileLoadingCrashTest : public ::testing::Test {
protected:
    ChipletFormat format;
};

// Test that loading a file with components doesn't crash
TEST_F(FileLoadingCrashTest, LoadWithComponentsDoesNotCrash) {
    std::string path = fixturePath("with_components.chiplet");

    // This should not crash
    std::unique_ptr<Assembly> assembly;
    ASSERT_NO_THROW({
        assembly = format.load(path);
    });

    ASSERT_NE(assembly, nullptr);
    EXPECT_FALSE(assembly->name().empty());
    EXPECT_GT(assembly->components().size(), 0u);
}

// Test that loading test_2dview.chiplet doesn't crash
TEST_F(FileLoadingCrashTest, LoadTest2DViewDoesNotCrash) {
    std::string path = fixturePath("test_2dview.chiplet");

    std::unique_ptr<Assembly> assembly;
    ASSERT_NO_THROW({
        assembly = format.load(path);
    });

    ASSERT_NE(assembly, nullptr);
    EXPECT_EQ(assembly->name(), "2D View Test Assembly");
    EXPECT_EQ(assembly->components().size(), 2u);
}

// Test that loading minimal.chiplet (no components) works
TEST_F(FileLoadingCrashTest, LoadMinimalDoesNotCrash) {
    std::string path = fixturePath("minimal.chiplet");

    std::unique_ptr<Assembly> assembly;
    ASSERT_NO_THROW({
        assembly = format.load(path);
    });

    ASSERT_NE(assembly, nullptr);
    EXPECT_FALSE(assembly->name().empty());
}

// Test component properties after loading
TEST_F(FileLoadingCrashTest, ComponentPropertiesAreValid) {
    std::string path = fixturePath("with_components.chiplet");
    auto assembly = format.load(path);

    ASSERT_NE(assembly, nullptr);

    for (const auto& comp : assembly->components()) {
        ASSERT_NE(comp, nullptr);

        // ID should not be empty
        EXPECT_FALSE(comp->id().empty());

        // Dimensions should be non-negative
        const auto& dims = comp->dimensions();
        EXPECT_GE(dims.width, 0.0);
        EXPECT_GE(dims.height, 0.0);
        EXPECT_GE(dims.thickness, 0.0);

        // Position is valid (any value is okay)
        const auto& pos = comp->position();
        EXPECT_TRUE(std::isfinite(pos.x));
        EXPECT_TRUE(std::isfinite(pos.y));
        EXPECT_TRUE(std::isfinite(pos.z));
    }
}

// Test that layout paths are resolved correctly
TEST_F(FileLoadingCrashTest, LayoutPathsAreResolved) {
    std::string path = fixturePath("test_2dview.chiplet");
    auto assembly = format.load(path);

    ASSERT_NE(assembly, nullptr);
    ASSERT_GE(assembly->components().size(), 1u);

    // Components with layout should have resolved paths
    for (const auto& comp : assembly->components()) {
        const auto& layoutPath = comp->layout_path();
        if (!layoutPath.empty()) {
            // Path should be absolute or resolvable
            EXPECT_TRUE(layoutPath.find("sample_") != std::string::npos ||
                        layoutPath[0] == '/');
        }
    }
}

// Test that components with zero dimensions don't cause issues
TEST_F(FileLoadingCrashTest, ZeroDimensionsHandled) {
    // Create an assembly programmatically with zero dimensions
    auto assembly = std::make_unique<Assembly>();
    assembly->set_name("Zero Dimensions Test");

    auto comp = std::make_unique<Component>("zero_comp", ComponentType::Die);
    comp->set_dimensions({0.0, 0.0, 0.0});
    comp->set_position({0.0, 0.0, 0.0});

    // This should not crash
    ASSERT_NO_THROW({
        assembly->add_component(std::move(comp));
    });

    EXPECT_EQ(assembly->components().size(), 1u);
}

// Test validation doesn't crash with missing files
TEST_F(FileLoadingCrashTest, ValidationWithMissingLayoutFiles) {
    std::string path = fixturePath("with_components.chiplet");
    auto assembly = format.load(path);

    ASSERT_NE(assembly, nullptr);

    // Validate should return errors for missing files, not crash
    ASSERT_NO_THROW({
        auto result = assembly->validate();
        // We expect errors because layout files don't exist
        // But it should not crash
    });
}

} // anonymous namespace
} // namespace chiplet
