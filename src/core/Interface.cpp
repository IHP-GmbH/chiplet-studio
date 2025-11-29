/**
 * Interface.cpp - Implementation
 */

#include "Interface.h"

namespace chiplet {

Interface::Interface(const std::string& id, InterfaceType type)
    : m_id(id)
    , m_type(type)
{
}

Interface::~Interface() = default;

const std::string& Interface::id() const
{
    return m_id;
}

InterfaceType Interface::type() const
{
    return m_type;
}

void Interface::setFrom(const InterfaceEndpoint& from)
{
    m_from = from;
}

const InterfaceEndpoint& Interface::from() const
{
    return m_from;
}

void Interface::setTo(const InterfaceEndpoint& to)
{
    m_to = to;
}

const InterfaceEndpoint& Interface::to() const
{
    return m_to;
}

void Interface::setPhysical(const InterfacePhysical& physical)
{
    m_physical = physical;
}

const InterfacePhysical& Interface::physical() const
{
    return m_physical;
}

} // namespace chiplet
