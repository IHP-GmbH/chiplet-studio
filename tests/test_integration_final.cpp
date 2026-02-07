/**
 * test_integration_final.cpp - Final integration verification
 *
 * Validates all refactored systems work together:
 * - Component creation and management via C++ API
 * - ID-based component management
 * - Position, dimensions, technology assignment
 *
 * Note: Python tests are optional (may crash in Docker due to interpreter issues)
 */

#include <gtest/gtest.h>
#include <QCoreApplication>
#include "core/Assembly.h"
#include "core/Component.h"
#include "core/CommandProcessor.h"

using namespace chiplet;

// =============================================================================
// C++ Integration Tests (always run)
// =============================================================================

class IntegrationFinalTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_assembly = std::make_unique<Assembly>();
        m_processor = std::make_unique<CommandProcessor>(m_assembly.get());
    }

    void TearDown() override {
        m_processor.reset();
        m_assembly.reset();
    }

    std::unique_ptr<Assembly> m_assembly;
    std::unique_ptr<CommandProcessor> m_processor;
};

// =============================================================================
// Stress Test: Create 100 components via C++ API
// =============================================================================

TEST_F(IntegrationFinalTest, StressTest100ComponentsCpp) {
    m_assembly->set_name("Stress Test");

    // Create 10x10 grid of dies
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 10; ++x) {
            std::string compId = "Die_" + std::to_string(x) + "_" + std::to_string(y);

            auto die = std::make_unique<Component>(compId, ComponentType::Die);

            // Set dimensions
            Dimensions3D dims;
            dims.width = 1000.0;
            dims.height = 1000.0;
            dims.thickness = 50.0;
            die->set_dimensions(dims);

            // Set position in grid (2mm spacing)
            Position3D pos;
            pos.x = x * 2000.0;
            pos.y = y * 2000.0;
            pos.z = 0.0;
            die->set_position(pos);

            // Set technology
            die->set_technology("ihp-sg13g2");

            m_assembly->add_component(std::move(die));
        }
    }

    // Verify 100 components created
    EXPECT_EQ(m_assembly->components().size(), 100u)
        << "Expected 100 components in 10x10 grid";

    // Verify assembly name was set
    EXPECT_EQ(m_assembly->name(), "Stress Test");

    // Verify first component (origin)
    EXPECT_TRUE(m_assembly->has_component("Die_0_0"));
    Component* first = m_assembly->component("Die_0_0");
    ASSERT_NE(first, nullptr);
    EXPECT_DOUBLE_EQ(first->position().x, 0.0);
    EXPECT_DOUBLE_EQ(first->position().y, 0.0);
    EXPECT_DOUBLE_EQ(first->position().z, 0.0);

    // Verify last component (corner at 9,9)
    EXPECT_TRUE(m_assembly->has_component("Die_9_9"));
    Component* last = m_assembly->component("Die_9_9");
    ASSERT_NE(last, nullptr);
    EXPECT_DOUBLE_EQ(last->position().x, 18000.0);  // 9 * 2000
    EXPECT_DOUBLE_EQ(last->position().y, 18000.0);  // 9 * 2000
    EXPECT_DOUBLE_EQ(last->position().z, 0.0);

    // Verify dimensions
    EXPECT_DOUBLE_EQ(last->dimensions().width, 1000.0);
    EXPECT_DOUBLE_EQ(last->dimensions().height, 1000.0);
    EXPECT_DOUBLE_EQ(last->dimensions().thickness, 50.0);

    // Verify technology was set
    EXPECT_EQ(last->technology(), "ihp-sg13g2");
}

// =============================================================================
// Corner Components Test
// =============================================================================

TEST_F(IntegrationFinalTest, VerifyCornerComponentsCpp) {
    // Create corner components
    struct CornerData {
        int x, y;
    };
    CornerData corners[] = {{0, 0}, {9, 0}, {0, 9}, {9, 9}};

    for (const auto& corner : corners) {
        std::string compId = "Corner_" + std::to_string(corner.x) + "_" + std::to_string(corner.y);
        auto die = std::make_unique<Component>(compId, ComponentType::Die);

        Dimensions3D dims{1000.0, 1000.0, 50.0};
        die->set_dimensions(dims);

        Position3D pos{corner.x * 2000.0, corner.y * 2000.0, 0.0};
        die->set_position(pos);

        m_assembly->add_component(std::move(die));
    }

    EXPECT_EQ(m_assembly->components().size(), 4u);

    // Verify positions
    struct CornerCheck {
        const char* id;
        double x, y;
    };
    CornerCheck checks[] = {
        {"Corner_0_0", 0.0, 0.0},
        {"Corner_9_0", 18000.0, 0.0},
        {"Corner_0_9", 0.0, 18000.0},
        {"Corner_9_9", 18000.0, 18000.0}
    };

    for (const auto& check : checks) {
        EXPECT_TRUE(m_assembly->has_component(check.id)) << "Missing: " << check.id;
        Component* c = m_assembly->component(check.id);
        ASSERT_NE(c, nullptr) << "Null component: " << check.id;
        EXPECT_DOUBLE_EQ(c->position().x, check.x) << "Wrong X for " << check.id;
        EXPECT_DOUBLE_EQ(c->position().y, check.y) << "Wrong Y for " << check.id;
    }
}

// =============================================================================
// Mixed Component Types Test
// =============================================================================

TEST_F(IntegrationFinalTest, MixedComponentTypesCpp) {
    // Create substrate at bottom
    auto substrate = std::make_unique<Component>("pkg_substrate", ComponentType::Substrate);
    substrate->set_dimensions({20000.0, 20000.0, 500.0});
    substrate->set_position({0.0, 0.0, 0.0});
    m_assembly->add_component(std::move(substrate));

    // Create interposer on substrate
    auto interposer = std::make_unique<Component>("interposer", ComponentType::Interposer);
    interposer->set_dimensions({15000.0, 15000.0, 100.0});
    interposer->set_position({2500.0, 2500.0, 500.0});
    m_assembly->add_component(std::move(interposer));

    // Create 4 dies on interposer
    for (int i = 0; i < 4; ++i) {
        double x = (i % 2) * 6000.0 + 4000.0;
        double y = (i / 2) * 6000.0 + 4000.0;

        auto die = std::make_unique<Component>("die_" + std::to_string(i), ComponentType::Die);
        die->set_dimensions({5000.0, 5000.0, 100.0});
        die->set_position({x, y, 600.0});
        die->set_technology("ihp-sg13g2");
        m_assembly->add_component(std::move(die));
    }

    EXPECT_EQ(m_assembly->components().size(), 6u);  // 1 substrate + 1 interposer + 4 dies

    // Verify types
    EXPECT_EQ(m_assembly->component("pkg_substrate")->type(), ComponentType::Substrate);
    EXPECT_EQ(m_assembly->component("interposer")->type(), ComponentType::Interposer);
    EXPECT_EQ(m_assembly->component("die_0")->type(), ComponentType::Die);

    // Verify Z-stacking
    EXPECT_DOUBLE_EQ(m_assembly->component("pkg_substrate")->position().z, 0.0);
    EXPECT_DOUBLE_EQ(m_assembly->component("interposer")->position().z, 500.0);
    EXPECT_DOUBLE_EQ(m_assembly->component("die_0")->position().z, 600.0);
}

// =============================================================================
// Component Modification Test
// =============================================================================

TEST_F(IntegrationFinalTest, ComponentModificationCpp) {
    // Create a component
    auto die = std::make_unique<Component>("test_die", ComponentType::Die);
    die->set_dimensions({1000.0, 1000.0, 100.0});
    die->set_position({100.0, 200.0, 50.0});
    m_assembly->add_component(std::move(die));

    Component* c = m_assembly->component("test_die");
    ASSERT_NE(c, nullptr);
    EXPECT_DOUBLE_EQ(c->position().x, 100.0);
    EXPECT_DOUBLE_EQ(c->position().y, 200.0);

    // Move it
    Position3D newPos = c->position();
    newPos.x += 10.0;
    newPos.y += 20.0;
    newPos.z += 5.0;
    c->set_position(newPos);

    // Verify the move was applied
    EXPECT_DOUBLE_EQ(c->position().x, 110.0);
    EXPECT_DOUBLE_EQ(c->position().y, 220.0);
    EXPECT_DOUBLE_EQ(c->position().z, 55.0);
}
