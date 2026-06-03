// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Interface.h - Physical connections between components
 */

#ifndef CHIPLET_CORE_INTERFACE_H
#define CHIPLET_CORE_INTERFACE_H

#include <string>

namespace chiplet {

/**
 * Interface types
 */
enum class InterfaceType {
    MicroBump,      // Die to interposer (fine pitch)
    CopperPillar,   // Interposer to substrate
    TSV,            // Through-silicon via
    WireBond        // Wire bonding
};

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
    Interface(const string_type& id, InterfaceType type);
    ~Interface();

    // Getters - identity
    const string_type& id() const;
    InterfaceType type() const;

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
    InterfaceType m_type;
    endpoint_type m_from;
    endpoint_type m_to;
    physical_type m_physical;
};

} // namespace chiplet

#endif // CHIPLET_CORE_INTERFACE_H
