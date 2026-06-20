// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Assembly.h - Top-level container for chiplet package assembly
 */

#ifndef CHIPLET_CORE_ASSEMBLY_H
#define CHIPLET_CORE_ASSEMBLY_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <unordered_map>
#include "ComponentID.h"
#include "Component.h"
#include "ConnectionStack.h"
#include "Interface.h"
#include "Technology.h"
#include "Netlist.h"
#include "flow/FlowDefinition.h"

namespace chiplet {

/**
 * Validation result for assembly.
 */
struct AssemblyValidation {
    bool valid = true;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;

    void add_error(const std::string& msg) {
        errors.push_back(msg);
        valid = false;
    }

    void add_warning(const std::string& msg) {
        warnings.push_back(msg);
    }

    void merge(const TechnologyValidation& tv, const std::string& context) {
        for (const auto& e : tv.errors) {
            add_error(context + ": " + e);
        }
        for (const auto& w : tv.warnings) {
            add_warning(context + ": " + w);
        }
    }
};

/**
 * Assembly represents a complete chiplet package containing
 * multiple components (dies, interposer, substrate) and their
 * interconnections.
 */
class Assembly {
public:
    // Type definitions
    typedef std::string string_type;
    typedef std::vector<std::unique_ptr<Component>> component_list_type;
    typedef std::vector<std::unique_ptr<Interface>> interface_list_type;
    typedef std::vector<std::unique_ptr<Technology>> technology_list_type;
    typedef std::map<std::string, ConnectionStack> connection_stack_map_type;

    // Constructors and destructor
    Assembly();
    ~Assembly();

    // Non-copyable (contains unique_ptr members)
    Assembly(const Assembly&) = delete;
    Assembly& operator=(const Assembly&) = delete;

    // Move operations
    Assembly(Assembly&&) = default;
    Assembly& operator=(Assembly&&) = default;

    // Getters - metadata
    const string_type& name() const;
    const string_type& description() const;
    const string_type& author() const;
    const string_type& created() const;
    const string_type& modified() const;
    const string_type& units() const;
    const string_type& assembly_gds() const;
    // Verbatim assembly_gds path from the file (may be ${VAR}/relative); empty
    // for in-memory assemblies. Lets the writer round-trip the source string
    // instead of the resolved absolute path. See Component::layout_path_source.
    const string_type& assembly_gds_source() const;
    const string_type& io_technology() const;
    const string_type& interconnect_adapter() const;

    // Sorted unique connection ids of the dies (= interconnect method ids
    // for manifest-era assemblies). The per-die `connection:` is the source
    // of truth for which interconnect methods the assembly uses; the
    // assembly-level adapter is the legacy/family fallback
    // (LayerStackup::resolveInterconnectKeys applies the policy).
    std::vector<string_type> interconnect_method_ids() const;

    // Setters - metadata
    void set_name(const string_type& name);
    void set_description(const string_type& desc);
    void set_author(const string_type& author);
    void set_created(const string_type& created);
    void set_modified(const string_type& modified);
    void set_units(const string_type& units);
    void set_assembly_gds(const string_type& path);
    void set_assembly_gds_source(const string_type& path);
    void set_io_technology(const string_type& tech);
    void set_interconnect_adapter(const string_type& adapter);

    // Components
    void add_component(std::unique_ptr<Component> component);
    Component* component(const ComponentID& id) const;
    bool has_component(const ComponentID& id) const;
    bool remove_component(const ComponentID& id);
    const component_list_type& components() const;

    // Interfaces
    void add_interface(std::unique_ptr<Interface> iface);
    Interface* interface(const string_type& id) const;
    const interface_list_type& interfaces() const;

    // Netlist
    void set_netlist(const Netlist& netlist);
    void set_netlist(Netlist&& netlist);
    const Netlist& netlist() const;
    Netlist& netlist();

    // Flow definition
    void set_flow_definition(const FlowDefinition& def);
    void set_flow_definition(FlowDefinition&& def);
    const FlowDefinition& flow_definition() const;
    bool has_flow() const;

    // Technologies
    void add_technology(std::unique_ptr<Technology> tech);
    Technology* technology(const string_type& id) const;
    const technology_list_type& technologies() const;

    // Connection stacks
    void add_connection_stack(const ConnectionStack& stack);
    const ConnectionStack* connection_stack(const string_type& id) const;
    const connection_stack_map_type& connection_stacks() const;

    /**
     * Calculate z-position for a component from interposer stackup + connection stack.
     * Returns the sum of interposer top z and connection stack total height,
     * or 0.0 if the component has no connection or the interposer stackup is unavailable.
     */
    double calculate_component_z(const ComponentID& id) const;

    // Validation
    /**
     * Resolve a component's technology reference to a Technology object.
     * @param id ComponentID to resolve technology for
     * @return Pointer to Technology or nullptr if not found or component has no technology
     */
    Technology* resolve_component_technology(const ComponentID& id) const;

    /**
     * Validate the entire assembly.
     * Checks:
     * - All technologies are valid
     * - All component technology references resolve
     * - Component layout paths exist (if specified)
     * @return Validation result with errors and warnings
     */
    AssemblyValidation validate() const;

    /**
     * Quick check if assembly is valid.
     */
    bool is_valid() const;

private:
    string_type m_name;
    string_type m_description;
    string_type m_author;
    string_type m_created;
    string_type m_modified;
    string_type m_units = "um";  // Default to micrometers
    string_type m_assembly_gds;
    string_type m_assembly_gds_source;  // verbatim ${VAR}/relative path from the file
    string_type m_io_technology;  // wire_bond, flipped_bump, tsv_bump (informative)
    string_type m_interconnect_adapter;  // ADK interconnect adapter (e.g. ihp_cupillar, vendorx_microbump)
    component_list_type m_components;
    std::unordered_map<ComponentID, Component*> m_component_index;  // O(1) lookup
    interface_list_type m_interfaces;
    Netlist m_netlist;
    FlowDefinition m_flowDefinition;
    technology_list_type m_technologies;
    connection_stack_map_type m_connectionStacks;
};

} // namespace chiplet

#endif // CHIPLET_CORE_ASSEMBLY_H
