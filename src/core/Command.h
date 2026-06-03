// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Command.h - Abstract base class for undoable commands
 *
 * Implements the Command Pattern for safe, transactional editing.
 * All state-modifying operations should be encapsulated as Commands.
 */

#ifndef CHIPLET_CORE_COMMAND_H
#define CHIPLET_CORE_COMMAND_H

#include <string>
#include <memory>
#include <nlohmann/json.hpp>

namespace chiplet {

class Assembly;

/**
 * Abstract base class for all undoable commands.
 *
 * Commands encapsulate state changes and provide:
 * - execute(): Apply the change
 * - undo(): Reverse the change
 * - redo(): Re-apply after undo
 * - serialize(): Convert to JSON for journaling
 */
class Command {
public:
    virtual ~Command() = default;

    /**
     * Execute the command, applying changes to the assembly.
     * @param assembly The assembly to modify
     * @return true if execution succeeded, false otherwise
     */
    virtual bool execute(Assembly& assembly) = 0;

    /**
     * Undo the command, restoring previous state.
     * @param assembly The assembly to restore
     */
    virtual void undo(Assembly& assembly) = 0;

    /**
     * Redo the command after an undo.
     * Default implementation calls execute().
     * @param assembly The assembly to modify
     */
    virtual void redo(Assembly& assembly) { execute(assembly); }

    /**
     * Serialize the command to JSON for journaling.
     * @return JSON object representing this command
     */
    virtual nlohmann::json serialize() const = 0;

    /**
     * Get the command type identifier for deserialization.
     * @return Type string (e.g., "MoveComponent", "RenameComponent")
     */
    virtual std::string type() const = 0;

    /**
     * Get a human-readable description for the undo/redo menu.
     * @return Description string (e.g., "Move component 'die_a'")
     */
    virtual std::string description() const = 0;

    /**
     * Check if this command can be merged with another.
     * Used for coalescing rapid updates (e.g., drag operations).
     * @param other The command to potentially merge with
     * @return true if commands can be merged
     */
    virtual bool can_merge_with(const Command& other) const {
        (void)other;  // Suppress unused parameter warning
        return false;
    }

    /**
     * Merge another command into this one.
     * Only called if can_merge_with() returns true.
     * @param other The command to merge (will be consumed)
     */
    virtual void merge_with(Command& other) {
        (void)other;  // Suppress unused parameter warning
    }
};

/**
 * Smart pointer type for Commands.
 */
using CommandPtr = std::unique_ptr<Command>;

} // namespace chiplet

#endif // CHIPLET_CORE_COMMAND_H
