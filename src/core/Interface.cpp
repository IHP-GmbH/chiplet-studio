// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Interface.cpp - Implementation
 */

#include "Interface.h"

#include <array>
#include <utility>

namespace chiplet {

namespace {

// The mapping, in one place, read in both directions. Keeping it as a single
// table is what makes interface_type_name total and InterfaceTypeValue's lookup
// its exact inverse; two hand-written switches drift, and the drift is silent
// because each one alone still compiles and still answers.
constexpr std::array<std::pair<InterfaceType, const char*>, 5> kMapping = {{
    {InterfaceType::MicroBump,    "micro_bump"},
    {InterfaceType::CopperPillar, "copper_pillar"},
    {InterfaceType::TSV,          "tsv"},
    {InterfaceType::WireBond,     "wire_bond"},
    {InterfaceType::SolderBump,   "solder_bump"},
}};

} // namespace

const char* interface_type_name(InterfaceType t)
{
    for (const auto& entry : kMapping) {
        if (entry.first == t) return entry.second;
    }
    // Unreachable for any enumerator in the table. Returning a spelling that is
    // not in the vocabulary is deliberate: if an enumerator is ever added
    // without a table row, this writes something a reader will reject loudly
    // rather than a plausible wrong type that rounds-trips silently.
    return "";
}

InterfaceTypeValue::InterfaceTypeValue(std::string declared)
    : m_declared(std::move(declared))
{
}

InterfaceTypeValue::InterfaceTypeValue(InterfaceType mapped)
    : m_declared(interface_type_name(mapped))
{
}

const std::string& InterfaceTypeValue::declared() const
{
    return m_declared;
}

std::optional<InterfaceType> InterfaceTypeValue::mapped() const
{
    for (const auto& entry : kMapping) {
        if (m_declared == entry.second) return entry.first;
    }
    return std::nullopt;
}

Interface::Interface(const std::string& id, const InterfaceTypeValue& type)
    : m_id(id)
    , m_type(type)
{
}

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

const InterfaceTypeValue& Interface::type() const
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
