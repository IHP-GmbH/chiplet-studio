// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Interface.h - Physical connections between components
 */

#ifndef CHIPLET_CORE_INTERFACE_H
#define CHIPLET_CORE_INTERFACE_H

#include <optional>
#include <string>

namespace chiplet {

/**
 * The interface types this studio can ACT on.
 *
 * This is the studio's MAPPING of the format's interfaces[].type vocabulary,
 * not the vocabulary itself and not storage. A member here means the studio
 * knows what to do with that value; the absence of one means the studio does
 * not, which is a fact about this program and not a defect in the document.
 *
 * The five members mirror the closed vocabulary the vendored reference reader
 * exports as kKnownInterfaceTypes. InterfaceTypeValue keeps the two lists tied
 * together, because a mapping that silently falls behind the vocabulary it
 * claims to cover is the failure this whole type exists to prevent.
 */
enum class InterfaceType {
    MicroBump,      // Die to interposer (fine pitch)
    CopperPillar,   // Interposer to substrate
    TSV,            // Through-silicon via
    WireBond,       // Wire bonding
    SolderBump      // C4-class reflowed solder ball (interconnect method sbump_sac305)
};

/**
 * An interfaces[].type as the document declared it, together with this studio's
 * mapping of it.
 *
 * The STRING is the storage and the enum is a derived view, which is the policy
 * the format's own reference header declares: enum-like fields are carried as
 * the canonical YAML string, and consumers map them into their own richer enums
 * and own the warning. Storing the string is what makes a save byte-faithful:
 * a document that says `solder_bump` says `solder_bump` after a round trip,
 * without the writer having to remember anything.
 *
 * What this type deliberately does NOT offer is an operator== against
 * InterfaceType and an implicit conversion to it. That absence is the whole
 * point, and it was chosen by measurement rather than taste. The obvious
 * alternative, exposing std::optional<InterfaceType>, was compiled against the
 * studio's own toolchain: `switch (opt)` and passing `opt` where the enum is
 * expected both fail, but `opt == InterfaceType::MicroBump` COMPILES and
 * evaluates to false for an unmapped value, with no diagnostic even under
 * -Wall -Wextra -Wswitch. Sorted by that behaviour, the sites that would break
 * are the ones that only display something, and the sites that would keep
 * compiling are the ones that decide something. An unmapped value silently
 * comparing false is exactly the class of bug being removed here, so the type
 * refuses the comparison and makes every caller write its fallback where a
 * reader can see it.
 *
 * There is deliberately no is(InterfaceType) predicate either, and that was an
 * objection worth taking: a predicate answering "not a micro bump" when the
 * truth is "I do not know what this is" is the same hole with a friendlier
 * name. The argument that a caller had to type it is real but weak, because a
 * decision site is exactly where somebody rewrites `x == T` as `x.is(T)`
 * mechanically, the diff reads as reviewed, and the silence comes back intact.
 * It has no caller today, and adding it before one exists is inviting that
 * rewrite. A site that needs to decide branches on mapped(), where the
 * unmapped case cannot be reached without writing down what to do about it.
 */
class InterfaceTypeValue {
public:
    InterfaceTypeValue() = default;

    // From a document. `declared` is kept verbatim, mapped or not.
    explicit InterfaceTypeValue(std::string declared);

    // From code. The canonical string for the member is stored, so a
    // programmatically built interface round-trips like a parsed one.
    explicit InterfaceTypeValue(InterfaceType mapped);

    // Exactly what the document wrote. This is what a save emits.
    const std::string& declared() const;

    // This studio's mapping, or nullopt when there is none.
    std::optional<InterfaceType> mapped() const;

private:
    std::string m_declared;
};

// The canonical YAML spelling of a mapped member. Total: every enumerator has
// one, which is what lets the constructor above store a string for any of them.
const char* interface_type_name(InterfaceType t);

/**
 * Physical parameters for an interface (all in micrometers)
 */
struct InterfacePhysical {
    double pitch = 0.0;
    double diameter = 0.0;
    double height = 0.0;
};

/**
 * Connection endpoint
 */
struct InterfaceEndpoint {
    std::string component;
    std::string surface;    // "top" or "bottom"
    std::string portLayer;  // Layer in GDS defining pads
};

/**
 * Interface represents a physical connection between two components.
 */
class Interface {
public:
    // Type definitions
    typedef std::string string_type;
    typedef InterfaceEndpoint endpoint_type;
    typedef InterfacePhysical physical_type;

    // Constructors and destructor
    Interface(const string_type& id, const InterfaceTypeValue& type);
    Interface(const string_type& id, InterfaceType type);
    ~Interface();

    // Getters - identity
    const string_type& id() const;
    const InterfaceTypeValue& type() const;

    // Getters/Setters - endpoints
    void set_from(const endpoint_type& from);
    const endpoint_type& from() const;

    void set_to(const endpoint_type& to);
    const endpoint_type& to() const;

    // Getters/Setters - physical parameters
    void set_physical(const physical_type& physical);
    const physical_type& physical() const;

private:
    string_type m_id;
    InterfaceTypeValue m_type;
    endpoint_type m_from;
    endpoint_type m_to;
    physical_type m_physical;
};

} // namespace chiplet

#endif // CHIPLET_CORE_INTERFACE_H
