// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Netlist.h - Net, NetClass, and Netlist data structures for assembly connectivity
 */

#ifndef CHIPLET_CORE_NETLIST_H
#define CHIPLET_CORE_NETLIST_H

#include <string>
#include <vector>
#include <map>
#include <regex>

namespace chiplet {

/**
 * Classification of nets by electrical function
 */
enum class NetClass {
    Signal,     // General-purpose signal
    Power,      // Power supply (VDD, VDDQ, etc.)
    Ground,     // Ground reference (VSS, GND, etc.)
    DiffPair,   // Differential pair (P/N)
    NC,         // No connect
    Interface   // Inter-chiplet interface signal (e.g., UCIe, BoW)
};

/**
 * A single connection endpoint within a net
 */
struct NetConnection {
    std::string component;  // Component ID (must match a component in the assembly)
    std::string pin;        // Pin or pad name on the component
    std::string layer;      // Optional: GDS layer specification for the connection
};

/**
 * A net represents a named electrical connection between pins on multiple components.
 */
class Net {
public:
    // Type definitions
    typedef std::string string_type;
    typedef std::vector<NetConnection> connection_list_type;

    // Constructors
    Net();
    Net(const string_type& name, NetClass net_class = NetClass::Signal);
    ~Net();

    // Getters - identity
    const string_type& name() const;
    NetClass net_class() const;

    // Setters
    // NOTE: if this Net is owned by a Netlist, renaming it invalidates the
    // Netlist's name index; call Netlist::rebuild_index() afterwards.
    void set_name(const string_type& name);
    void set_net_class(NetClass nc);

    // Connections
    void add_connection(const NetConnection& conn);
    void add_connection(const std::string& component, const std::string& pin,
                        const std::string& layer = "");
    const connection_list_type& connections() const;
    size_t connection_count() const;
    void clear_connections();

    // External flag: set when this net leaves the package boundary
    // (e.g. terminates at an IOPad on the interposer for wire-bonding).
    bool external() const;
    void set_external(bool e);

private:
    string_type m_name;
    NetClass m_net_class = NetClass::Signal;
    connection_list_type m_connections;
    bool m_external = false;
};

/**
 * Netlist holds all nets for an assembly and provides query/import/export utilities.
 */
class Netlist {
public:
    // Type definitions
    typedef std::string string_type;
    typedef std::vector<Net> net_list_type;

    Netlist();
    ~Netlist();

    // Net management
    void add_net(const Net& net);
    void add_net(Net&& net);
    const Net* net(const string_type& name) const;
    Net* net(const string_type& name);
    const net_list_type& nets() const;
    size_t net_count() const;
    void clear();

    // Query by class
    std::vector<const Net*> nets_by_class(NetClass nc) const;
    size_t count_by_class(NetClass nc) const;

    // Query by component
    std::vector<const Net*> nets_for_component(const string_type& component_id) const;

    // Query: nets exposed externally (Net::external() == true)
    std::vector<const Net*> nets_external() const;

    // External netlist file reference (CSV)
    void set_external_netlist_path(const string_type& path);
    const string_type& external_netlist_path() const;

    // CSV import/export
    // CSV columns: net_name, component, pin, layer, net_class
    bool import_csv(const string_type& path);
    bool export_csv(const string_type& path) const;

    // Pattern-based net mapping
    // Maps nets matching a pattern from one component to another
    // Example: map_pattern("HBM_DQ[*]", "hbm_0", "interposer", "HBM0_DQ{*}")
    int map_pattern(const string_type& source_pattern,
                    const string_type& source_component,
                    const string_type& target_component,
                    const string_type& target_pattern);

    // Check if netlist has any content
    bool empty() const;

private:
    net_list_type m_nets;
    std::map<string_type, size_t> m_net_index;  // Name -> index in m_nets
    string_type m_external_netlist_path;

    // Rebuild the name-to-index mapping
    void rebuild_index();
};

// String conversion helpers
std::string net_class_to_string(NetClass nc);
NetClass string_to_net_class(const std::string& s);

} // namespace chiplet

#endif // CHIPLET_CORE_NETLIST_H
