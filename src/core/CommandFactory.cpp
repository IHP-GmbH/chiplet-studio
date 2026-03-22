/**
 * CommandFactory.cpp - Implementation
 */

#include "CommandFactory.h"
#include "commands/CmdMoveComponent.h"
#include "commands/CmdRenameComponent.h"
#include "commands/CmdSetRenderMode.h"

#include <QDebug>

namespace chiplet {

std::map<std::string, CommandFactory::Deserializer>& CommandFactory::registry()
{
    static std::map<std::string, Deserializer> s_registry;
    return s_registry;
}

void CommandFactory::register_command(const std::string& type, Deserializer deserializer)
{
    registry()[type] = std::move(deserializer);
}

CommandPtr CommandFactory::create(const std::string& type, const nlohmann::json& json)
{
    auto& reg = registry();
    auto it = reg.find(type);
    if (it != reg.end()) {
        try {
            return it->second(json);
        } catch (const std::exception& e) {
            qWarning("Failed to deserialize command '%s': %s", type.c_str(), e.what());
        }
    }
    return nullptr;
}

bool CommandFactory::is_registered(const std::string& type)
{
    return registry().count(type) > 0;
}

void CommandFactory::register_builtin_commands()
{
    register_command("MoveComponent", CmdMoveComponent::deserialize);
    register_command("RenameComponent", CmdRenameComponent::deserialize);
    register_command("SetRenderMode", CmdSetRenderMode::deserialize);
}

std::function<CommandPtr(const std::string&, const nlohmann::json&)>
CommandFactory::get_journal_factory()
{
    return [](const std::string& type, const nlohmann::json& json) {
        return create(type, json);
    };
}

} // namespace chiplet
