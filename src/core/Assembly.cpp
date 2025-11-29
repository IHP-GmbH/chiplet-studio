/**
 * Assembly.cpp - Implementation
 */

#include "Assembly.h"
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
    m_components.push_back(std::move(component));
}

Component* Assembly::component(const string_type& id) const
{
    for (const auto& c : m_components) {
        if (c->id() == id) {
            return c.get();
        }
    }
    return nullptr;
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

// Validation

Technology* Assembly::resolve_component_technology(const Component* component) const
{
    if (!component) {
        return nullptr;
    }

    const auto& tech_id = component->technology();
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
