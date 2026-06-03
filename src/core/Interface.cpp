// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

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

void Interface::set_from(const endpoint_type& from)
{
    m_from = from;
}

const Interface::endpoint_type& Interface::from() const
{
    return m_from;
}

void Interface::set_to(const endpoint_type& to)
{
    m_to = to;
}

const Interface::endpoint_type& Interface::to() const
{
    return m_to;
}

void Interface::set_physical(const physical_type& physical)
{
    m_physical = physical;
}

const Interface::physical_type& Interface::physical() const
{
    return m_physical;
}

} // namespace chiplet
