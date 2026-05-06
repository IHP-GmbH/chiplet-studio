/**
 * CmdSetRenderMode.cpp - Implementation
 */

#include "CmdSetRenderMode.h"
#include "core/Assembly.h"

namespace chiplet {

CmdSetRenderMode::CmdSetRenderMode(const ComponentID& id,
                                   RenderMode old_mode,
                                   RenderMode new_mode)
    : m_componentId(id)
    , m_oldMode(old_mode)
    , m_newMode(new_mode)
{
}

bool CmdSetRenderMode::execute(Assembly& assembly)
{
    Component* comp = assembly.component(m_componentId);
    if (!comp) {
        return false;
    }

    comp->set_render_mode(m_newMode);
    return true;
}

void CmdSetRenderMode::undo(Assembly& assembly)
{
    Component* comp = assembly.component(m_componentId);
    if (!comp) {
        return;
    }

    comp->set_render_mode(m_oldMode);
}

nlohmann::json CmdSetRenderMode::serialize() const
{
    return nlohmann::json{
        {"type", type()},
        {"data", {
            {"id", m_componentId},
            {"old", renderModeToInt(m_oldMode)},
            {"new", renderModeToInt(m_newMode)}
        }}
    };
}

std::string CmdSetRenderMode::description() const
{
    return "Set render mode for '" + m_componentId + "'";
}

CommandPtr CmdSetRenderMode::deserialize(const nlohmann::json& j)
{
    const auto& data = j.at("data");
    ComponentID id = data.at("id").get<std::string>();
    RenderMode oldMode = renderModeFromInt(data.at("old").get<int>());
    RenderMode newMode = renderModeFromInt(data.at("new").get<int>());
    return std::make_unique<CmdSetRenderMode>(id, oldMode, newMode);
}

int CmdSetRenderMode::renderModeToInt(RenderMode mode)
{
    return static_cast<int>(mode);
}

RenderMode CmdSetRenderMode::renderModeFromInt(int value)
{
    if (value >= 0 && value <= 5) {
        return static_cast<RenderMode>(value);
    }
    return RenderMode::Solid;  // Safe default
}

} // namespace chiplet
