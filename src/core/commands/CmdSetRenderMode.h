// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * CmdSetRenderMode.h - Command to change a component's render mode
 */

#ifndef CHIPLET_CORE_COMMANDS_CMD_SET_RENDER_MODE_H
#define CHIPLET_CORE_COMMANDS_CMD_SET_RENDER_MODE_H

#include "core/Command.h"
#include "core/ComponentID.h"
#include "core/Component.h"

namespace chiplet {

/**
 * Command to change a component's per-component render mode.
 * Stores old and new modes for undo/redo.
 */
class CmdSetRenderMode : public Command {
public:
    CmdSetRenderMode(const ComponentID& id, RenderMode old_mode, RenderMode new_mode);

    bool execute(Assembly& assembly) override;
    void undo(Assembly& assembly) override;
    nlohmann::json serialize() const override;
    std::string type() const override { return "SetRenderMode"; }
    std::string description() const override;

    static CommandPtr deserialize(const nlohmann::json& j);

    // Accessors for testing
    const ComponentID& component_id() const { return m_componentId; }
    RenderMode old_mode() const { return m_oldMode; }
    RenderMode new_mode() const { return m_newMode; }

private:
    ComponentID m_componentId;
    RenderMode m_oldMode;
    RenderMode m_newMode;

    static int renderModeToInt(RenderMode mode);
    static RenderMode renderModeFromInt(int value);
};

} // namespace chiplet

#endif // CHIPLET_CORE_COMMANDS_CMD_SET_RENDER_MODE_H
