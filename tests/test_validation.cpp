/**
 * test_validation.cpp - Unit tests for Technology and Assembly validation
 */

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "core/Assembly.h"
#include "core/Component.h"
#include "core/Technology.h"

namespace chiplet {
namespace {

// Helper to create a temporary file
class TempFile {
public:
    TempFile(const std::string& content = "")
        : m_path(std::filesystem::temp_directory_path() / ("test_" + std::to_string(counter++) + ".txt"))
    {
        std::ofstream ofs(m_path);
        ofs << content;
    }

    ~TempFile()
    {
        std::filesystem::remove(m_path);
    }

    const std::filesystem::path& path() const { return m_path; }
    std::string path_string() const { return m_path.string(); }

private:
    std::filesystem::path m_path;
    static int counter;
};

int TempFile::counter = 0;

// =============================================================================
// Technology Validation Tests
// =============================================================================

TEST(TechnologyValidation, ValidTechnology)
{
    TempFile lyp("# Layer properties file");

    Technology tech("test_tech");
    tech.set_description("Test technology");
    tech.set_layer_properties_path(lyp.path_string());
    tech.set_dbu(0.001);

    auto result = tech.validate();

    EXPECT_TRUE(result.valid);
    EXPECT_TRUE(result.errors.empty());
}

TEST(TechnologyValidation, EmptyIdIsError)
{
    Technology tech("");

    auto result = tech.validate();

    EXPECT_FALSE(result.valid);
    EXPECT_FALSE(result.errors.empty());

    bool found = false;
    for (const auto& e : result.errors) {
        if (e.find("ID") != std::string::npos && e.find("empty") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Expected error about empty ID";
}

TEST(TechnologyValidation, MissingLayerPropertiesIsError)
{
    Technology tech("test_tech");
    tech.set_layer_properties_path("/nonexistent/path/to/file.lyp");

    auto result = tech.validate();

    EXPECT_FALSE(result.valid);

    bool found = false;
    for (const auto& e : result.errors) {
        if (e.find("Layer properties file not found") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Expected error about missing layer properties file";
}

TEST(TechnologyValidation, NoLayerPropertiesIsWarning)
{
    Technology tech("test_tech");
    // No layer_properties_path set

    auto result = tech.validate();

    EXPECT_TRUE(result.valid);
    EXPECT_FALSE(result.warnings.empty());

    bool found = false;
    for (const auto& w : result.warnings) {
        if (w.find("No layer properties file") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Expected warning about no layer properties file";
}

TEST(TechnologyValidation, NegativeDbuIsError)
{
    Technology tech("test_tech");
    tech.set_dbu(-0.001);

    auto result = tech.validate();

    EXPECT_FALSE(result.valid);

    bool found = false;
    for (const auto& e : result.errors) {
        if (e.find("positive") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Expected error about DBU being positive";
}

TEST(TechnologyValidation, LargeDbuIsWarning)
{
    Technology tech("test_tech");
    tech.set_dbu(10.0);  // 10um per database unit - unusual

    auto result = tech.validate();

    EXPECT_TRUE(result.valid);  // Still valid, just a warning

    bool found = false;
    for (const auto& w : result.warnings) {
        if (w.find("larger than 1um") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Expected warning about large DBU";
}

TEST(TechnologyValidation, LayerPropertiesExist)
{
    TempFile lyp("# Layer properties");

    Technology tech("test_tech");
    tech.set_layer_properties_path(lyp.path_string());

    EXPECT_TRUE(tech.layer_properties_exist());
}

TEST(TechnologyValidation, LayerPropertiesNotExist)
{
    Technology tech("test_tech");
    tech.set_layer_properties_path("/nonexistent/file.lyp");

    EXPECT_FALSE(tech.layer_properties_exist());
}

TEST(TechnologyValidation, EmptyLayerPropertiesPathIsValid)
{
    Technology tech("test_tech");
    // Empty path is OK (optional)

    EXPECT_TRUE(tech.layer_properties_exist());
}

// =============================================================================
// Assembly Validation Tests
// =============================================================================

TEST(AssemblyValidation, EmptyAssemblyIsValid)
{
    Assembly assembly;
    assembly.set_name("Test");

    auto result = assembly.validate();

    EXPECT_TRUE(result.valid);
}

TEST(AssemblyValidation, ValidAssemblyWithComponents)
{
    TempFile lyp("# Layer properties");

    Assembly assembly;
    assembly.set_name("Test Assembly");

    // Add technology
    auto tech = std::make_unique<Technology>("test_tech");
    tech->set_layer_properties_path(lyp.path_string());
    assembly.add_technology(std::move(tech));

    // Add component with valid technology reference
    auto comp = std::make_unique<Component>("die1", ComponentType::Die);
    comp->set_technology("test_tech");
    assembly.add_component(std::move(comp));

    auto result = assembly.validate();

    EXPECT_TRUE(result.valid);
}

TEST(AssemblyValidation, InvalidTechnologyReference)
{
    Assembly assembly;
    assembly.set_name("Test Assembly");

    // Add component with invalid technology reference
    auto comp = std::make_unique<Component>("die1", ComponentType::Die);
    comp->set_technology("nonexistent_tech");
    assembly.add_component(std::move(comp));

    auto result = assembly.validate();

    EXPECT_FALSE(result.valid);

    bool found = false;
    for (const auto& e : result.errors) {
        if (e.find("nonexistent_tech") != std::string::npos &&
            e.find("not found") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Expected error about missing technology";
}

TEST(AssemblyValidation, NoTechnologyIsWarning)
{
    Assembly assembly;
    assembly.set_name("Test Assembly");

    // Add component without technology
    auto comp = std::make_unique<Component>("die1", ComponentType::Die);
    // No technology set
    assembly.add_component(std::move(comp));

    auto result = assembly.validate();

    EXPECT_TRUE(result.valid);  // Still valid, just a warning

    bool found = false;
    for (const auto& w : result.warnings) {
        if (w.find("No technology specified") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Expected warning about no technology";
}

TEST(AssemblyValidation, MissingLayoutFileIsError)
{
    Assembly assembly;
    assembly.set_name("Test Assembly");

    // Add component with nonexistent layout file
    auto comp = std::make_unique<Component>("die1", ComponentType::Die);
    comp->set_layout_path("/nonexistent/layout.gds");
    assembly.add_component(std::move(comp));

    auto result = assembly.validate();

    EXPECT_FALSE(result.valid);

    bool found = false;
    for (const auto& e : result.errors) {
        if (e.find("Layout file not found") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Expected error about missing layout file";
}

TEST(AssemblyValidation, TechnologyValidationPropagates)
{
    Assembly assembly;
    assembly.set_name("Test Assembly");

    // Add technology with invalid layer properties path
    auto tech = std::make_unique<Technology>("bad_tech");
    tech->set_layer_properties_path("/nonexistent/file.lyp");
    assembly.add_technology(std::move(tech));

    auto result = assembly.validate();

    EXPECT_FALSE(result.valid);

    bool found = false;
    for (const auto& e : result.errors) {
        if (e.find("bad_tech") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Expected error to include technology ID";
}

TEST(AssemblyValidation, ResolveTechnology)
{
    Assembly assembly;

    auto tech = std::make_unique<Technology>("test_tech");
    assembly.add_technology(std::move(tech));

    auto comp = std::make_unique<Component>("die1", ComponentType::Die);
    comp->set_technology("test_tech");
    const Component* comp_ptr = comp.get();
    assembly.add_component(std::move(comp));

    Technology* resolved = assembly.resolve_component_technology(comp_ptr);

    ASSERT_NE(resolved, nullptr);
    EXPECT_EQ(resolved->id(), "test_tech");
}

TEST(AssemblyValidation, ResolveTechnologyNotFound)
{
    Assembly assembly;

    auto comp = std::make_unique<Component>("die1", ComponentType::Die);
    comp->set_technology("nonexistent");
    const Component* comp_ptr = comp.get();
    assembly.add_component(std::move(comp));

    Technology* resolved = assembly.resolve_component_technology(comp_ptr);

    EXPECT_EQ(resolved, nullptr);
}

TEST(AssemblyValidation, ResolveTechnologyNullComponent)
{
    Assembly assembly;

    Technology* resolved = assembly.resolve_component_technology(nullptr);

    EXPECT_EQ(resolved, nullptr);
}

TEST(AssemblyValidation, IsValidShortcut)
{
    Assembly assembly;
    assembly.set_name("Test");

    EXPECT_TRUE(assembly.is_valid());

    // Add invalid component
    auto comp = std::make_unique<Component>("die1", ComponentType::Die);
    comp->set_layout_path("/nonexistent/file.gds");
    assembly.add_component(std::move(comp));

    EXPECT_FALSE(assembly.is_valid());
}

} // namespace
} // namespace chiplet
