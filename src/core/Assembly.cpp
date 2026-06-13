// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Assembly.cpp - Implementation
 */

#include "Assembly.h"
#include "LayerStackup.h"
#include <algorithm>
#include <filesystem>
#include <set>

namespace chiplet {

Assembly::Assembly() = default;
Assembly::~Assembly() = default;

// Getters - metadata

const Assembly::string_type& Assembly::name() const
{
    return m_name;
}

const Assembly::string_type& Assembly::description() const
{
    return m_description;
}

const Assembly::string_type& Assembly::author() const
{
    return m_author;
}

const Assembly::string_type& Assembly::created() const
{
    return m_created;
}

const Assembly::string_type& Assembly::modified() const
{
    return m_modified;
}

const Assembly::string_type& Assembly::units() const
{
    return m_units;
}

const Assembly::string_type& Assembly::assembly_gds() const
{
    return m_assembly_gds;
}

const Assembly::string_type& Assembly::io_technology() const
{
    return m_io_technology;
}

const Assembly::string_type& Assembly::interconnect_adapter() const
{
    return m_interconnect_adapter;
}

std::vector<Assembly::string_type> Assembly::interconnect_method_ids() const
{
    std::vector<string_type> ids;
    for (const auto& c : m_components) {
        if (c->type() == ComponentType::Interposer) continue;
        const string_type& conn = c->connection();
        if (conn.empty()) continue;
        if (std::find(ids.begin(), ids.end(), conn) == ids.end()) {
            ids.push_back(conn);
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

// Setters - metadata

void Assembly::set_name(const string_type& name)
{
    m_name = name;
}

void Assembly::set_description(const string_type& desc)
{
    m_description = desc;
}

void Assembly::set_author(const string_type& author)
{
    m_author = author;
}

void Assembly::set_created(const string_type& created)
{
    m_created = created;
}

void Assembly::set_modified(const string_type& modified)
{
    m_modified = modified;
}

void Assembly::set_units(const string_type& units)
{
    m_units = units;
}

void Assembly::set_assembly_gds(const string_type& path)
{
    m_assembly_gds = path;
}

void Assembly::set_io_technology(const string_type& tech)
{
    m_io_technology = tech;
}

void Assembly::set_interconnect_adapter(const string_type& adapter)
{
    m_interconnect_adapter = adapter;
}

// Components

void Assembly::add_component(std::unique_ptr<Component> component)
{
    if (!component) {
        return;
    }
    const ComponentID& id = component->id();

    // Adding a component whose ID already exists previously left the old object
    // orphaned in the vector while the index pointed at the new one, so the
    // vector (and component_count) desynced from the index. Replace in place
    // instead, keeping both consistent.
    auto existing = m_component_index.find(id);
    if (existing != m_component_index.end()) {
        for (auto& slot : m_components) {
            if (slot->id() == id) {
                Component* ptr = component.get();
                slot = std::move(component);
                existing->second = ptr;
                return;
            }
        }
    }

    Component* ptr = component.get();
    m_components.push_back(std::move(component));
    m_component_index[id] = ptr;
}

Component* Assembly::component(const ComponentID& id) const
{
    auto it = m_component_index.find(id);
    if (it != m_component_index.end()) {
        return it->second;
    }
    return nullptr;
}

bool Assembly::has_component(const ComponentID& id) const
{
    return m_component_index.find(id) != m_component_index.end();
}

bool Assembly::remove_component(const ComponentID& id)
{
    auto it = m_component_index.find(id);
    if (it == m_component_index.end()) {
        return false;
    }

    // Remove from index
    m_component_index.erase(it);

    // Remove from vector
    auto vec_it = std::find_if(m_components.begin(), m_components.end(),
        [&id](const std::unique_ptr<Component>& c) {
            return c->id() == id;
        });

    if (vec_it != m_components.end()) {
        m_components.erase(vec_it);
    }

    return true;
}

const Assembly::component_list_type& Assembly::components() const
{
    return m_components;
}

// Interfaces

void Assembly::add_interface(std::unique_ptr<Interface> iface)
{
    m_interfaces.push_back(std::move(iface));
}

Interface* Assembly::interface(const string_type& id) const
{
    for (const auto& i : m_interfaces) {
        if (i->id() == id) {
            return i.get();
        }
    }
    return nullptr;
}

const Assembly::interface_list_type& Assembly::interfaces() const
{
    return m_interfaces;
}

// Netlist

void Assembly::set_netlist(const Netlist& netlist)
{
    m_netlist = netlist;
}

void Assembly::set_netlist(Netlist&& netlist)
{
    m_netlist = std::move(netlist);
}

const Netlist& Assembly::netlist() const
{
    return m_netlist;
}

Netlist& Assembly::netlist()
{
    return m_netlist;
}

// Flow definition

void Assembly::set_flow_definition(const FlowDefinition& def)
{
    m_flowDefinition = def;
}

void Assembly::set_flow_definition(FlowDefinition&& def)
{
    m_flowDefinition = std::move(def);
}

const FlowDefinition& Assembly::flow_definition() const
{
    return m_flowDefinition;
}

bool Assembly::has_flow() const
{
    return !m_flowDefinition.empty();
}

// Technologies

void Assembly::add_technology(std::unique_ptr<Technology> tech)
{
    m_technologies.push_back(std::move(tech));
}

Technology* Assembly::technology(const string_type& id) const
{
    for (const auto& t : m_technologies) {
        if (t->id() == id) {
            return t.get();
        }
    }
    return nullptr;
}

const Assembly::technology_list_type& Assembly::technologies() const
{
    return m_technologies;
}

// Connection stacks

void Assembly::add_connection_stack(const ConnectionStack& stack)
{
    m_connectionStacks[stack.id] = stack;
}

const ConnectionStack* Assembly::connection_stack(const string_type& id) const
{
    auto it = m_connectionStacks.find(id);
    return (it != m_connectionStacks.end()) ? &it->second : nullptr;
}

const Assembly::connection_stack_map_type& Assembly::connection_stacks() const
{
    return m_connectionStacks;
}

double Assembly::calculate_component_z(const ComponentID& id) const
{
    Component* comp = component(id);
    if (!comp) {
        return 0.0;
    }

    // Resolve the connection stack. Missing or unknown stacks fall
    // back to a zero-height mounting (the die sits directly on the
    // interposer body) per coord_frame_contract.md §3.4 / §5.5.
    // Earlier behavior was to return 0.0 for these cases; that put
    // dies at world z=0 instead of on top of the interposer body,
    // which the contract calls out as a bug because the formula must
    // hold in all cases.
    const ConnectionStack* stack = nullptr;
    if (!comp->connection().empty()) {
        stack = connection_stack(comp->connection());
    }

    // Mounting surface = z_bottom of the chosen connection stack's first
    // layer (the layer that physically attaches to the interposer pad,
    // e.g. CuPillar for cu-pillar stacks). Looking it up in the
    // interposer stackup gives the exact passivation-opening / pad-top
    // height. Adding stack->total_height() then lands the die on the
    // tip of the connection.
    //
    // Naive max_z(stackup) is wrong: it picks up Passiv (the
    // passivation around the opening, taller than TopMetal2) and adds
    // 1.9 um on top of the real mounting surface.
    double mounting_surface = 0.0;
    bool mounting_surface_found = false;
    if (stack && !stack->layers.empty()) {
        const std::string& firstLayerName = stack->layers.front().name;
        for (const auto& c : m_components) {
            if (c->type() != ComponentType::Interposer) continue;
            const std::string& techId = c->technology();
            if (techId.empty()) break;
            std::string stackupYaml = BlenderGDSConfigs::stackupPath(techId);
            if (stackupYaml.empty()) break;
            LayerStackup stackup;
            if (!stackup.loadFromBlenderGDS(stackupYaml)) break;
            // Merge the interconnect PDK's 3D bodies (CuPillar/SnAgCap or a
            // vendor's microbump), which the interconnect PDK owns rather than
            // the interposer stackup. Additive: it brings in bodies the
            // interposer stackup does not define, so the first-layer lookup
            // below resolves for whatever method the design selected. The
            // shared helper applies the fragment's z-reference rule (relative
            // to the stackup's declared attachment surface, or legacy
            // absolute) identically to the render path.
            //
            // Per-die: THIS die's connection id selects the fragment (method
            // ids are fragment keys), so a die using Option 3 seats on
            // Option-3 body heights even when other dies use other methods.
            // The assembly-level adapter is the fallback for stacks whose id
            // is not a method id (legacy/custom stacks).
            stackup.mergeInterconnectFragments(
                LayerStackup::resolveInterconnectKeys(
                    {comp->connection()}, m_interconnect_adapter));
            for (const auto& layer : stackup.sortedLayers()) {
                if (!firstLayerName.empty() && layer.name == firstLayerName) {
                    mounting_surface = layer.z_bottom;
                    mounting_surface_found = true;
                    break;
                }
            }
            break;
        }
    }

    // Fallback for stackups that don't visualize the connection layers
    // AND for the missing-connection / null-stack cases above: use the
    // interposer thickness so the die lands on top of the physical
    // interposer body. (Contract §3.4 collapses all three fallback
    // paths into the same formula.)
    if (!mounting_surface_found) {
        for (const auto& c : m_components) {
            if (c->type() == ComponentType::Interposer) {
                mounting_surface = c->dimensions().thickness;
                break;
            }
        }
    }

    const double connection_height = stack ? stack->total_height() : 0.0;
    return mounting_surface + connection_height;
}

// Validation

Technology* Assembly::resolve_component_technology(const ComponentID& id) const
{
    if (!is_valid_id(id)) {
        return nullptr;
    }

    Component* comp = component(id);
    if (!comp) {
        return nullptr;
    }

    const auto& tech_id = comp->technology();
    if (tech_id.empty()) {
        return nullptr;
    }

    return technology(tech_id);
}

AssemblyValidation Assembly::validate() const
{
    AssemblyValidation result;

    // Validate all technologies
    for (const auto& tech : m_technologies) {
        auto tv = tech->validate();
        if (!tv.valid || !tv.warnings.empty()) {
            result.merge(tv, "Technology '" + tech->id() + "'");
        }
    }

    // Validate all components
    for (const auto& comp : m_components) {
        const std::string comp_context = "Component '" + comp->id() + "'";

        // Check technology reference
        const auto& tech_id = comp->technology();
        if (!tech_id.empty()) {
            if (!technology(tech_id)) {
                result.add_error(comp_context + ": Technology '" + tech_id + "' not found");
            }
        } else {
            result.add_warning(comp_context + ": No technology specified");
        }

        // Check layout path
        const auto& layout_path = comp->layout_path();
        if (!layout_path.empty()) {
            if (!std::filesystem::exists(layout_path)) {
                result.add_error(comp_context + ": Layout file not found: " + layout_path);
            }
        }
    }

    return result;
}

bool Assembly::is_valid() const
{
    return validate().valid;
}

} // namespace chiplet
