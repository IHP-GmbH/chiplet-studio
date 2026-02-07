/**
 * CommandFactory.h - Factory for creating commands from serialized data
 *
 * Provides a registry for command deserializers to support journal replay.
 */

#ifndef CHIPLET_CORE_COMMAND_FACTORY_H
#define CHIPLET_CORE_COMMAND_FACTORY_H

#include <string>
#include <functional>
#include <map>
#include "Command.h"

namespace chiplet {

/**
 * CommandFactory maintains a registry of command deserializers.
 *
 * Usage:
 *   // During initialization:
 *   CommandFactory::register_builtin_commands();
 *
 *   // To create a command from JSON:
 *   CommandPtr cmd = CommandFactory::create("MoveComponent", json);
 */
class CommandFactory {
public:
    /**
     * Deserializer function type.
     */
    using Deserializer = std::function<CommandPtr(const nlohmann::json&)>;

    /**
     * Register a command deserializer.
     * @param type Command type string (e.g., "MoveComponent")
     * @param deserializer Function to deserialize the command
     */
    static void register_command(const std::string& type, Deserializer deserializer);

    /**
     * Create a command from JSON data.
     * @param type Command type string
     * @param json JSON data containing the command
     * @return New command instance, or nullptr if type unknown
     */
    static CommandPtr create(const std::string& type, const nlohmann::json& json);

    /**
     * Check if a command type is registered.
     * @param type Command type string
     * @return true if the type is registered
     */
    static bool is_registered(const std::string& type);

    /**
     * Register all built-in command types.
     * Call this during application initialization.
     */
    static void register_builtin_commands();

    /**
     * Get the factory function for CommandJournal::replay().
     * @return Factory function compatible with CommandJournal
     */
    static std::function<CommandPtr(const std::string&, const nlohmann::json&)>
        get_journal_factory();

private:
    static std::map<std::string, Deserializer>& registry();
};

} // namespace chiplet

#endif // CHIPLET_CORE_COMMAND_FACTORY_H
