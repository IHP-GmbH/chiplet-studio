// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Assembly.cpp - Implementation
 */

#include "Assembly.h"
#include "DecimalText.h"
#include "TechResolve.h"
#include "LayerStackup.h"
#include "Logger.h"
#include <QDebug>
#include <algorithm>
#include <filesystem>
#include <set>

namespace chiplet {

Assembly::Assembly() = default;

const DocumentPersistence& Assembly::document_persistence() const
{
    return m_documentPersistence;
}

void Assembly::set_document_persistence(DocumentPersistence state)
{
    m_documentPersistence = std::move(state);
}
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

const Assembly::string_type& Assembly::assembly_gds_source() const
{
    return m_assembly_gds_source;
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

void Assembly::set_assembly_gds_source(const string_type& path)
{
    m_assembly_gds_source = path;
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

const Component* Assembly::component(const ComponentID& id) const
{
    auto it = m_component_index.find(id);
    if (it != m_component_index.end()) {
        return it->second;
    }
    return nullptr;
}

Component* Assembly::component(const ComponentID& id)
{
    return const_cast<Component*>(
        static_cast<const Assembly*>(this)->component(id));
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
        [&id](const owning_ptr<Component>& c) {
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

Assembly::component_list_type& Assembly::components()
{
    return m_components;
}

// Interfaces

void Assembly::add_interface(std::unique_ptr<Interface> iface)
{
    m_interfaces.push_back(std::move(iface));
}

const Interface* Assembly::interface(const string_type& id) const
{
    for (const auto& i : m_interfaces) {
        if (i->id() == id) {
            return i.get();
        }
    }
    return nullptr;
}

Interface* Assembly::interface(const string_type& id)
{
    return const_cast<Interface*>(
        static_cast<const Assembly*>(this)->interface(id));
}

const Assembly::interface_list_type& Assembly::interfaces() const
{
    return m_interfaces;
}

Assembly::interface_list_type& Assembly::interfaces()
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
    m_flowSourceText.clear();
    m_flowDefinition = def;
}

void Assembly::set_flow_definition(FlowDefinition&& def)
{
    m_flowSourceText.clear();
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

void Assembly::set_flow_block_uncaptured(bool uncaptured)
{
    m_flowBlockUncaptured = uncaptured;
}

bool Assembly::flow_block_uncaptured() const
{
    return m_flowBlockUncaptured;
}

void Assembly::set_flow_definition_from_source(FlowDefinition&& def,
                                              const string_type& sourceText)
{
    m_flowDefinition = std::move(def);
    m_flowSourceText = sourceText;
}

const Assembly::string_type& Assembly::flow_source_text() const
{
    return m_flowSourceText;
}

// Technologies

void Assembly::add_technology(std::unique_ptr<Technology> tech)
{
    m_technologies.push_back(std::move(tech));
}

const Technology* Assembly::technology(const string_type& id) const
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

const Component* Assembly::mount_interposer() const
{
    for (const auto& c : m_components) {
        if (c->type() == ComponentType::Interposer) return c.get();
    }
    return nullptr;
}

double Assembly::calculate_component_z(const ComponentID& id) const
{
    const Component* comp = component(id);
    if (!comp) {
        return 0.0;
    }

    // A missing or unknown stack is zero height: the die sits on the
    // interposer, not at world z 0 (coord_frame_contract.md 3.4 / 5.5).
    const ConnectionStack* stack = nullptr;
    if (!comp->connection().empty()) {
        stack = connection_stack(comp->connection());
    }

    // The die sits on the z_bottom of its stack's first layer (the one that
    // attaches to the pad, e.g. CuPillar), found among the interconnect
    // bodies merged on the mount plane: THIS die's connection id selects the
    // fragment (method ids are fragment keys), the assembly adapter is the
    // fallback for stacks that are not method ids. The bodies are merged
    // into an empty stackup: the interposer stackup contributes no height, so
    // a stack layer named like a stackup layer (TopMetal2) cannot seat on
    // it. The render merges the same bodies on the same plane
    // (mergeBodiesOnInterposer), and only for an interposer with a
    // technology, as the render does. With no such body the die sits on the
    // plane.
    const Component* interposer = mount_interposer();
    double mounting_surface = interposer ? interposerMountPlane(*interposer) : 0.0;
    if (interposer && !interposer->technology().empty() && stack && !stack->layers.empty()) {
        LayerStackup bodies;
        mergeBodiesOnInterposer(bodies, *interposer,
            LayerStackup::resolveInterconnectKeys({comp->connection()},
                                                  m_interconnect_adapter));
        const std::string& firstLayerName = stack->layers.front().name;
        for (const auto& layer : bodies.sortedLayers()) {
            if (!firstLayerName.empty() && layer.name == firstLayerName) {
                mounting_surface = layer.z_bottom;
                break;
            }
        }
    }

    // Summed as decimals, so a die on 10.8103 under an 18 um stack is at
    // 28.8103 and is written so, not 28.810299999999998.
    const double connection_height = stack ? stack->total_height() : 0.0;
    return decimalSum(mounting_surface, connection_height);
}

// Validation

const Technology* Assembly::resolve_component_technology(const ComponentID& id) const
{
    if (!is_valid_id(id)) {
        return nullptr;
    }

    const Component* comp = component(id);
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
