/**
 * Assembly.cpp - Implementation
 */

#include "Assembly.h"
#include "LayerStackup.h"
#include <algorithm>
#include <filesystem>

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

// Components

void Assembly::add_component(std::unique_ptr<Component> component)
{
    if (!component) {
        return;
    }
    const ComponentID& id = component->id();
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
    if (!comp || comp->connection().empty()) {
        return 0.0;
    }

    const ConnectionStack* stack = connection_stack(comp->connection());
    if (!stack) {
        return 0.0;
    }

    // Find the interposer component and get its stackup top
    double interposer_top = 0.0;
    for (const auto& c : m_components) {
        if (c->type() == ComponentType::Interposer) {
            // Use interposer thickness as the mounting surface height
            interposer_top = c->dimensions().thickness;
            // If we have a technology with a stackup, use that instead
            const std::string& techId = c->technology();
            if (!techId.empty()) {
                std::string stackupYaml = BlenderGDSConfigs::stackupPath(techId);
                if (!stackupYaml.empty()) {
                    LayerStackup stackup;
                    if (stackup.loadFromBlenderGDS(stackupYaml)) {
                        double max_z = 0.0;
                        for (const auto& layer : stackup.sortedLayers()) {
                            if (layer.z_top() > max_z) {
                                max_z = layer.z_top();
                            }
                        }
                        if (max_z > 0.0) {
                            interposer_top = max_z;
                        }
                    }
                }
            }
            break;
        }
    }

    return interposer_top + stack->total_height();
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
