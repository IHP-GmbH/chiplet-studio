/**
 * CmdMoveComponent.h - Command to move a component's position
 */

#ifndef CHIPLET_CORE_COMMANDS_CMD_MOVE_COMPONENT_H
#define CHIPLET_CORE_COMMANDS_CMD_MOVE_COMPONENT_H

#include "core/Command.h"
#include "core/ComponentID.h"
#include "core/Component.h"

namespace chiplet {

/**
 * Command to move a component to a new position.
 * Stores both old and new positions for undo/redo.
 */
class CmdMoveComponent : public Command {
public:
    /**
     * Constructor.
     * @param id The component ID to move
     * @param old_pos Previous position (for undo)
     * @param new_pos New position (for execute/redo)
     */
    CmdMoveComponent(const ComponentID& id, const Position3D& old_pos, const Position3D& new_pos);

    bool execute(Assembly& assembly) override;
    void undo(Assembly& assembly) override;
    nlohmann::json serialize() const override;
    std::string type() const override { return "MoveComponent"; }
    std::string description() const override;

    bool can_merge_with(const Command& other) const override;
    void merge_with(Command& other) override;

    /**
     * Factory for deserialization.
     * @param j JSON object containing command data
     * @return New CmdMoveComponent instance
     */
    static CommandPtr deserialize(const nlohmann::json& j);

    // Accessors for testing
    const ComponentID& component_id() const { return m_componentId; }
    const Position3D& old_position() const { return m_oldPosition; }
    const Position3D& new_position() const { return m_newPosition; }

private:
    ComponentID m_componentId;
    Position3D m_oldPosition;
    Position3D m_newPosition;
};

} // namespace chiplet

#endif // CHIPLET_CORE_COMMANDS_CMD_MOVE_COMPONENT_H
