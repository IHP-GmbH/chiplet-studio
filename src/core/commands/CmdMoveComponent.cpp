// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * CmdMoveComponent.cpp - Implementation
 */

#include "CmdMoveComponent.h"
#include "core/Assembly.h"

namespace chiplet {

CmdMoveComponent::CmdMoveComponent(const ComponentID& id,
                                   const Position3D& old_pos,
                                   const Position3D& new_pos)
    : m_componentId(id)
    , m_oldPosition(old_pos)
    , m_newPosition(new_pos)
{
}

bool CmdMoveComponent::execute(Assembly& assembly)
{
    Component* comp = assembly.component(m_componentId);
    if (!comp) {
        return false;
    }

    comp->set_position(m_newPosition);
    return true;
}

void CmdMoveComponent::undo(Assembly& assembly)
{
    Component* comp = assembly.component(m_componentId);
    if (!comp) {
        return;
    }

    comp->set_position(m_oldPosition);
}

nlohmann::json CmdMoveComponent::serialize() const
{
    return nlohmann::json{
        {"type", type()},
        {"data", {
            {"id", m_componentId},
            {"old", {m_oldPosition.x, m_oldPosition.y, m_oldPosition.z}},
            {"new", {m_newPosition.x, m_newPosition.y, m_newPosition.z}}
        }}
    };
}

std::string CmdMoveComponent::description() const
{
    return "Move component '" + m_componentId + "'";
}

bool CmdMoveComponent::can_merge_with(const Command& other) const
{
    // Only merge if it's a MoveComponent command for the same component
    if (other.type() != type()) {
        return false;
    }

    const auto* otherMove = dynamic_cast<const CmdMoveComponent*>(&other);
    if (!otherMove) {
        return false;
    }

    return otherMove->m_componentId == m_componentId;
}

void CmdMoveComponent::merge_with(Command& other)
{
    auto* otherMove = dynamic_cast<CmdMoveComponent*>(&other);
    if (!otherMove) {
        return;
    }

    // Keep our old position, take their new position
    // This coalesces rapid drag updates into a single command
    m_newPosition = otherMove->m_newPosition;
}

CommandPtr CmdMoveComponent::deserialize(const nlohmann::json& j)
{
    const auto& data = j.at("data");
    ComponentID id = data.at("id").get<std::string>();

    // A corrupt/truncated journal can carry "old"/"new" that are not 3-element
    // arrays. nlohmann's operator[] would be UB on a non-array or out-of-range
    // index, so validate before indexing. Throwing here is safe: CommandFactory
    // and the journal replay loop both catch and log deserialization errors.
    auto parsePosition = [&data](const char* key) -> Position3D {
        const auto& arr = data.at(key);
        if (!arr.is_array() || arr.size() != 3) {
            throw std::runtime_error(
                std::string("CmdMoveComponent: '") + key +
                "' must be a 3-element array");
        }
        return Position3D{arr[0].get<double>(), arr[1].get<double>(),
                          arr[2].get<double>()};
    };

    Position3D oldPos = parsePosition("old");
    Position3D newPos = parsePosition("new");

    return std::make_unique<CmdMoveComponent>(id, oldPos, newPos);
}

} // namespace chiplet
