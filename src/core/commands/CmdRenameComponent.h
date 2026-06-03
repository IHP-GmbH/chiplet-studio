// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * CmdRenameComponent.h - Command to rename a component's display name
 */

#ifndef CHIPLET_CORE_COMMANDS_CMD_RENAME_COMPONENT_H
#define CHIPLET_CORE_COMMANDS_CMD_RENAME_COMPONENT_H

#include "core/Command.h"
#include "core/ComponentID.h"

namespace chiplet {

/**
 * Command to rename a component's display name.
 * Stores both old and new names for undo/redo.
 * Note: This changes the display name, not the immutable component ID.
 */
class CmdRenameComponent : public Command {
public:
    /**
     * Constructor.
     * @param id The component ID to rename
     * @param old_name Previous name (for undo)
     * @param new_name New name (for execute/redo)
     */
    CmdRenameComponent(const ComponentID& id,
                       const std::string& old_name,
                       const std::string& new_name);

    bool execute(Assembly& assembly) override;
    void undo(Assembly& assembly) override;
    nlohmann::json serialize() const override;
    std::string type() const override { return "RenameComponent"; }
    std::string description() const override;

    /**
     * Factory for deserialization.
     * @param j JSON object containing command data
     * @return New CmdRenameComponent instance
     */
    static CommandPtr deserialize(const nlohmann::json& j);

    // Accessors for testing
    const ComponentID& component_id() const { return m_componentId; }
    const std::string& old_name() const { return m_oldName; }
    const std::string& new_name() const { return m_newName; }

private:
    ComponentID m_componentId;
    std::string m_oldName;
    std::string m_newName;
};

} // namespace chiplet

#endif // CHIPLET_CORE_COMMANDS_CMD_RENAME_COMPONENT_H
