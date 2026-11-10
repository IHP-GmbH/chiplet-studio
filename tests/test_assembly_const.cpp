// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later
//
// `const Assembly&` is load-bearing: ChipletFormat::save takes one
// and reads its own output back through the format reader and
// refuses to write when the reader rejects it. That check is worth nothing if
// save can change the model it is checking.
//
// Why these are static_asserts and not a runtime gate. The defect is that
// certain code COMPILES; a passing ctest run structurally cannot testify about
// it. A revert must stop the build, not add one red line to a 600-test run.
//
// Both directions are asserted on purpose. The negative arm alone is satisfied
// by making everything const, which would break every legitimate mutation in
// the program; the positive arm alone is the status quo this commit removed.

#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "core/Assembly.h"
#include "core/Component.h"
#include "core/ComponentID.h"
#include "core/Interface.h"
#include "core/Technology.h"

using namespace chiplet;

namespace {

// Detection idiom. Substitution failure in the immediate context of the
// decltype drops the partial specialisation, so an ill-formed expression
// yields false rather than a hard error.

template <class A, class = void>
struct mutable_via_lookup : std::false_type {};
template <class A>
struct mutable_via_lookup<A, std::void_t<decltype(
    std::declval<A&>().component(std::declval<const ComponentID&>())
        ->set_name(std::string{})
)>> : std::true_type {};

template <class A, class = void>
struct mutable_via_list : std::false_type {};
template <class A>
struct mutable_via_list<A, std::void_t<decltype(
    std::declval<A&>().components().front()->clear_io_pads()
)>> : std::true_type {};

template <class A, class = void>
struct mutable_via_interface : std::false_type {};
template <class A>
struct mutable_via_interface<A, std::void_t<decltype(
    std::declval<A&>().interface(std::declval<const std::string&>())
        ->set_physical(std::declval<const InterfacePhysical&>())
)>> : std::true_type {};

template <class A, class = void>
struct mutable_via_technology : std::false_type {};
template <class A>
struct mutable_via_technology<A, std::void_t<decltype(
    std::declval<A&>().technology(std::declval<const std::string&>())->set_dbu(0.001)
)>> : std::true_type {};

template <class A, class = void>
struct mutable_via_resolve : std::false_type {};
template <class A>
struct mutable_via_resolve<A, std::void_t<decltype(
    std::declval<A&>().resolve_component_technology(std::declval<const ComponentID&>())
        ->set_dbu(0.001)
)>> : std::true_type {};

// THE TRAP THIS EXISTS TO CLOSE: for the detection idiom, "returns const" and
// "the accessor was deleted" both evaluate to false. Without a readability
// probe, someone could delete every const accessor and all five negative
// asserts below would still pass, green and meaningless. This arm makes
// absence distinguishable from constness.
template <class A, class = void>
struct readable_when_const : std::false_type {};
template <class A>
struct readable_when_const<A, std::void_t<
    decltype(std::declval<const A&>().component(std::declval<const ComponentID&>())->id()),
    decltype(std::declval<const A&>().components().front()->id()),
    decltype(std::declval<const A&>().interface(std::declval<const std::string&>())->id()),
    decltype(std::declval<const A&>().technology(std::declval<const std::string&>())->id()),
    decltype(std::declval<const A&>().resolve_component_technology(
                 std::declval<const ComponentID&>())->id())
>> : std::true_type {};

} // namespace

// --- The probe that keeps the negative arm honest.

static_assert(readable_when_const<Assembly>::value,
    "a const Assembly must still be fully READABLE; if this fires, "
    "an accessor was deleted rather than const-qualified and the negative "
    "asserts below have gone vacuous");

// --- Negative arm: a const Assembly hands out nothing mutable.

static_assert(!mutable_via_lookup<const Assembly>::value,
    "Assembly::component() const must return const Component*");
static_assert(!mutable_via_list<const Assembly>::value,
    "components() const must not yield a mutable Component. const "
    "does not propagate through a bare unique_ptr; keep owning_ptr");
static_assert(!mutable_via_interface<const Assembly>::value,
    "interface() const must return const Interface*");
static_assert(!mutable_via_technology<const Assembly>::value,
    "technology() must return const Technology*");
static_assert(!mutable_via_resolve<const Assembly>::value,
    "resolve_component_technology() returns a pointer INTO "
    "m_technologies and must return const Technology*");

// --- Positive arm: legitimate mutation still compiles. If someone "fixes"
// this by making everything const, these fire and name what they broke.

static_assert(mutable_via_lookup<Assembly>::value,
    "a NON-const Assembly must still allow component mutation "
    "(PropertiesPanel and the Python console both rely on it)");
static_assert(mutable_via_list<Assembly>::value,
    "a NON-const Assembly must still allow mutation through "
    "components(); ChipletFormat::auto_calculate_z and "
    "MainWindow::detectAndSelectCells write through this list");
static_assert(mutable_via_interface<Assembly>::value,
    "a NON-const Assembly must still allow interface mutation");

// technology() is deliberately const in BOTH directions: no non-const overload
// exists because no caller mutates a Technology through the assembly. Pin that
// so adding one is a conscious act, not a drive-by.
static_assert(std::is_same_v<
        decltype(std::declval<Assembly&>().technology(std::declval<const std::string&>())),
        const Technology*>,
    "technology() is const-returning even on a non-const Assembly, "
    "on purpose. Every Technology mutation happens in "
    "ChipletFormat::build_technology before add_technology takes ownership. "
    "If you need a non-const overload, add it with its caller in the same diff");

// --- Runtime half. Proves the positive arm is not merely well-formed, and
// gives the invariant a name a reader can grep for in the ctest output.

TEST(AssemblyConstBoundary, NonConstPathStillMutates)
{
    Assembly a;
    a.add_component(std::make_unique<Component>("die0", ComponentType::Die));

    a.component("die0")->set_name("renamed");
    EXPECT_EQ(a.component("die0")->name(), "renamed");

    for (auto& c : a.components()) {
        Position3D p;
        p.z = 42.0;
        c->set_position(p);
    }

    const Assembly& ca = a;
    EXPECT_EQ(ca.component("die0")->name(), "renamed");
    EXPECT_DOUBLE_EQ(ca.component("die0")->position().z, 42.0);
}

TEST(AssemblyConstBoundary, ConstAssemblyIsReadOnly)
{
    EXPECT_TRUE(readable_when_const<Assembly>::value);
    EXPECT_FALSE(mutable_via_lookup<const Assembly>::value);
    EXPECT_FALSE(mutable_via_list<const Assembly>::value);
    EXPECT_FALSE(mutable_via_technology<const Assembly>::value);
    EXPECT_TRUE(mutable_via_lookup<Assembly>::value);
    EXPECT_TRUE(mutable_via_list<Assembly>::value);
}
