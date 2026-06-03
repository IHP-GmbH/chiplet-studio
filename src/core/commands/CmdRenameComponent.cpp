// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * CmdRenameComponent.cpp - Implementation
 */

#include "CmdRenameComponent.h"
#include "core/Assembly.h"
#include "core/Component.h"

namespace chiplet {

CmdRenameComponent::CmdRenameComponent(const ComponentID& id,
                                       const std::string& old_name,
                                       const std::string& new_name)
    : m_componentId(id)
    , m_oldName(old_name)
    , m_newName(new_name)
{
}

bool CmdRenameComponent::execute(Assembly& assembly)
{
    Component* comp = assembly.component(m_componentId);
    if (!comp) {
        return false;
    }

    comp->set_name(m_newName);
    return true;
}

void CmdRenameComponent::undo(Assembly& assembly)
{
    Component* comp = assembly.component(m_componentId);
    if (!comp) {
        return;
    }

    comp->set_name(m_oldName);
}

nlohmann::json CmdRenameComponent::serialize() const
{
    return nlohmann::json{
        {"type", type()},
        {"data", {
            {"id", m_componentId},
            {"old", m_oldName},
            {"new", m_newName}
        }}
    };
}

std::string CmdRenameComponent::description() const
{
    return "Rename component '" + m_componentId + "' to '" + m_newName + "'";
}

CommandPtr CmdRenameComponent::deserialize(const nlohmann::json& j)
{
    const auto& data = j.at("data");
    ComponentID id = data.at("id").get<std::string>();
    std::string oldName = data.at("old").get<std::string>();
    std::string newName = data.at("new").get<std::string>();

    return std::make_unique<CmdRenameComponent>(id, oldName, newName);
}

} // namespace chiplet
