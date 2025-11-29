/**
 * Assembly.cpp - Implementation
 */

#include "Assembly.h"

namespace chiplet {

Assembly::Assembly() = default;
Assembly::~Assembly() = default;

void Assembly::setName(const std::string& name)
{
    m_name = name;
}

const std::string& Assembly::name() const
{
    return m_name;
}

void Assembly::setDescription(const std::string& desc)
{
    m_description = desc;
}

const std::string& Assembly::description() const
{
    return m_description;
}

void Assembly::addComponent(std::unique_ptr<Component> component)
{
    m_components.push_back(std::move(component));
}

Component* Assembly::component(const std::string& id) const
{
    for (const auto& c : m_components) {
        if (c->id() == id) {
            return c.get();
        }
    }
    return nullptr;
}

const std::vector<std::unique_ptr<Component>>& Assembly::components() const
{
    return m_components;
}

void Assembly::addInterface(std::unique_ptr<Interface> iface)
{
    m_interfaces.push_back(std::move(iface));
}

Interface* Assembly::interface(const std::string& id) const
{
    for (const auto& i : m_interfaces) {
        if (i->id() == id) {
            return i.get();
        }
    }
    return nullptr;
}

const std::vector<std::unique_ptr<Interface>>& Assembly::interfaces() const
{
    return m_interfaces;
}

void Assembly::addTechnology(std::unique_ptr<Technology> tech)
{
    m_technologies.push_back(std::move(tech));
}

Technology* Assembly::technology(const std::string& id) const
{
    for (const auto& t : m_technologies) {
        if (t->id() == id) {
            return t.get();
        }
    }
    return nullptr;
}

} // namespace chiplet
