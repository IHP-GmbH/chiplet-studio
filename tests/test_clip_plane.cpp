/**
 * test_clip_plane.cpp - Unit tests for ClipPlane
 */

#include <gtest/gtest.h>
#include "view3d/ClipPlane.h"
#include <cmath>

namespace chiplet {
namespace {

// =============================================================================
// Basic Construction Tests
// =============================================================================

TEST(ClipPlane, DefaultConstruction)
{
    ClipPlane plane;

    EXPECT_FALSE(plane.isEnabled());
    EXPECT_FALSE(plane.isFlipped());
    EXPECT_EQ(plane.axis(), ClipAxis::Z);
}

TEST(ClipPlane, DefaultNormal)
{
    ClipPlane plane;

    QVector3D normal = plane.normal();
    EXPECT_FLOAT_EQ(normal.x(), 0.0f);
    EXPECT_FLOAT_EQ(normal.y(), 0.0f);
    EXPECT_FLOAT_EQ(normal.z(), 1.0f);
}

// =============================================================================
// Enable/Disable Tests
// =============================================================================

TEST(ClipPlane, SetEnabled)
{
    ClipPlane plane;

    plane.setEnabled(true);
    EXPECT_TRUE(plane.isEnabled());

    plane.setEnabled(false);
    EXPECT_FALSE(plane.isEnabled());
}

// =============================================================================
// Axis Tests
// =============================================================================

TEST(ClipPlane, SetAxisX)
{
    ClipPlane plane;
    plane.setAxis(ClipAxis::X);

    EXPECT_EQ(plane.axis(), ClipAxis::X);

    QVector3D normal = plane.normal();
    EXPECT_FLOAT_EQ(normal.x(), 1.0f);
    EXPECT_FLOAT_EQ(normal.y(), 0.0f);
    EXPECT_FLOAT_EQ(normal.z(), 0.0f);
}

TEST(ClipPlane, SetAxisY)
{
    ClipPlane plane;
    plane.setAxis(ClipAxis::Y);

    EXPECT_EQ(plane.axis(), ClipAxis::Y);

    QVector3D normal = plane.normal();
    EXPECT_FLOAT_EQ(normal.x(), 0.0f);
    EXPECT_FLOAT_EQ(normal.y(), 1.0f);
    EXPECT_FLOAT_EQ(normal.z(), 0.0f);
}

TEST(ClipPlane, SetAxisZ)
{
    ClipPlane plane;
    plane.setAxis(ClipAxis::Z);

    EXPECT_EQ(plane.axis(), ClipAxis::Z);

    QVector3D normal = plane.normal();
    EXPECT_FLOAT_EQ(normal.x(), 0.0f);
    EXPECT_FLOAT_EQ(normal.y(), 0.0f);
    EXPECT_FLOAT_EQ(normal.z(), 1.0f);
}

// =============================================================================
// Position Tests
// =============================================================================

TEST(ClipPlane, SetPosition)
{
    ClipPlane plane;
    plane.setRange(-100.0f, 100.0f);
    plane.setPosition(50.0f);

    EXPECT_FLOAT_EQ(plane.position(), 50.0f);
}

TEST(ClipPlane, PositionClamped)
{
    ClipPlane plane;
    plane.setRange(-10.0f, 10.0f);

    plane.setPosition(100.0f);
    EXPECT_FLOAT_EQ(plane.position(), 10.0f);

    plane.setPosition(-100.0f);
    EXPECT_FLOAT_EQ(plane.position(), -10.0f);
}

TEST(ClipPlane, NormalizedPosition)
{
    ClipPlane plane;
    plane.setRange(0.0f, 100.0f);

    plane.setPosition(50.0f);
    EXPECT_FLOAT_EQ(plane.normalizedPosition(), 0.5f);

    plane.setPosition(0.0f);
    EXPECT_FLOAT_EQ(plane.normalizedPosition(), 0.0f);

    plane.setPosition(100.0f);
    EXPECT_FLOAT_EQ(plane.normalizedPosition(), 1.0f);
}

TEST(ClipPlane, SetNormalizedPosition)
{
    ClipPlane plane;
    plane.setRange(0.0f, 100.0f);

    plane.setNormalizedPosition(0.5f);
    EXPECT_FLOAT_EQ(plane.position(), 50.0f);

    plane.setNormalizedPosition(0.0f);
    EXPECT_FLOAT_EQ(plane.position(), 0.0f);

    plane.setNormalizedPosition(1.0f);
    EXPECT_FLOAT_EQ(plane.position(), 100.0f);
}

// =============================================================================
// Plane Equation Tests
// =============================================================================

TEST(ClipPlane, PlaneEquationZ)
{
    ClipPlane plane;
    plane.setAxis(ClipAxis::Z);
    plane.setRange(-10.0f, 10.0f);
    plane.setPosition(5.0f);

    QVector4D eq = plane.planeEquation();

    // Normal should be (0, 0, 1)
    EXPECT_FLOAT_EQ(eq.x(), 0.0f);
    EXPECT_FLOAT_EQ(eq.y(), 0.0f);
    EXPECT_FLOAT_EQ(eq.z(), 1.0f);
    // Distance should be -position = -5
    EXPECT_FLOAT_EQ(eq.w(), -5.0f);
}

TEST(ClipPlane, PlaneEquationFlipped)
{
    ClipPlane plane;
    plane.setAxis(ClipAxis::Z);
    plane.setRange(-10.0f, 10.0f);
    plane.setPosition(5.0f);

    QVector4D eq1 = plane.planeEquation();

    plane.flip();

    QVector4D eq2 = plane.planeEquation();

    // Flipped normal should be negated
    EXPECT_FLOAT_EQ(eq2.x(), -eq1.x());
    EXPECT_FLOAT_EQ(eq2.y(), -eq1.y());
    EXPECT_FLOAT_EQ(eq2.z(), -eq1.z());
    EXPECT_FLOAT_EQ(eq2.w(), -eq1.w());
}

// =============================================================================
// Flip Tests
// =============================================================================

TEST(ClipPlane, FlipToggle)
{
    ClipPlane plane;

    EXPECT_FALSE(plane.isFlipped());

    plane.flip();
    EXPECT_TRUE(plane.isFlipped());

    plane.flip();
    EXPECT_FALSE(plane.isFlipped());
}

// =============================================================================
// Range Tests
// =============================================================================

TEST(ClipPlane, SetRange)
{
    ClipPlane plane;
    plane.setRange(-50.0f, 150.0f);

    EXPECT_FLOAT_EQ(plane.minPosition(), -50.0f);
    EXPECT_FLOAT_EQ(plane.maxPosition(), 150.0f);
}

TEST(ClipPlane, RangeUpdatesClampsPosition)
{
    ClipPlane plane;
    plane.setRange(-100.0f, 100.0f);
    plane.setPosition(80.0f);

    // Shrink range
    plane.setRange(-50.0f, 50.0f);

    // Position should be clamped
    EXPECT_FLOAT_EQ(plane.position(), 50.0f);
}

// =============================================================================
// Custom Plane Tests
// =============================================================================

TEST(ClipPlane, SetCustomPlane)
{
    ClipPlane plane;
    QVector3D normal(1.0f, 1.0f, 0.0f);
    normal.normalize();

    plane.setPlane(normal, 10.0f);

    EXPECT_EQ(plane.axis(), ClipAxis::Custom);
    EXPECT_FLOAT_EQ(plane.distance(), 10.0f);

    QVector3D n = plane.normal();
    EXPECT_NEAR(n.x(), normal.x(), 0.0001f);
    EXPECT_NEAR(n.y(), normal.y(), 0.0001f);
    EXPECT_NEAR(n.z(), normal.z(), 0.0001f);
}

} // namespace
} // namespace chiplet
