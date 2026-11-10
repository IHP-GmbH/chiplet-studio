// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * FlowConfig.cpp - YAML parsing and writing for flow pipeline configuration
 */

#include "FlowConfig.h"
#include "FlowStep.h"
#include "FlowEngine.h"
#include "core/Assembly.h"
#include "formats/ChipletFormat.h"  // ChipletFormatException
#include "formats/YamlScalarEmit.h"

#include <yaml-cpp/yaml.h>
#include <regex>
#include <sstream>

namespace chiplet {

FlowConfig::FlowConfig() = default;
FlowConfig::~FlowConfig() = default;

// -- Parsing ----------------------------------------------------------------

void FlowConfig::parse_flow(const YAML::Node& node, const Assembly& assembly,
                            FlowEngine& engine)
{
    if (!node.IsMap()) {
        throw ChipletFormatException("flow section must be a map", 0, "flow");
    }

    if (node["working_directory"]) {
        std::string dir = resolve_variables(
            node["working_directory"].as<std::string>(), assembly);
        engine.set_working_directory(dir);
    }

    if (node["environment"] && node["environment"].IsMap()) {
        for (const auto& item : node["environment"]) {
            std::string key = item.first.as<std::string>();
            std::string value = resolve_variables(
                item.second.as<std::string>(), assembly);
            engine.set_environment(key, value);
        }
    }

    if (node["steps"]) {
        if (!node["steps"].IsSequence()) {
            throw ChipletFormatException(
                "flow.steps must be a sequence", 0, "flow.steps");
        }
        for (const auto& stepNode : node["steps"]) {
            engine.add_step(parse_step(stepNode, assembly));
        }
    }
}

FlowStep FlowConfig::parse_step(const YAML::Node& node,
                                 const Assembly& assembly)
{
    if (!node.IsMap()) {
        throw ChipletFormatException("step must be a map", 0, "flow.steps");
    }

    if (!node["id"]) {
        throw ChipletFormatException(
            "Missing required field", 0, "flow.steps[].id");
    }

    FlowStep step;
    step.id = node["id"].as<std::string>();
    step.name = step.id;  // default

    if (node["name"]) {
        step.name = node["name"].as<std::string>();
    }

    // Record the keys the author actually wrote, then derive the runtime view
    // from them. Reading `script:` only inside `if (node["tool"])` is what used
    // to drop a script-only step's program entirely (see FlowStep.h).
    if (node["tool"]) {
        step.declared_tool = resolve_variables(
            node["tool"].as<std::string>(), assembly);
    }
    if (node["script"]) {
        step.declared_script = resolve_variables(
            node["script"].as<std::string>(), assembly);
    }

    // tool/script mapping:
    //   tool + script -> interpreter = tool, tool_path = script
    //   tool only     -> interpreter = "", tool_path = tool
    //   script only   -> neither; step.program() is nullopt and the engine
    //                    refuses to run it rather than starting "".
    if (step.declared_tool && step.declared_script) {
        step.interpreter = *step.declared_tool;
        step.tool_path = *step.declared_script;
    } else if (step.declared_tool) {
        step.tool_path = *step.declared_tool;
    }

    if (node["args"] && node["args"].IsSequence()) {
        for (const auto& arg : node["args"]) {
            step.args.push_back(
                resolve_variables(arg.as<std::string>(), assembly));
        }
    }

    if (node["input_files"] && node["input_files"].IsSequence()) {
        for (const auto& f : node["input_files"]) {
            step.input_files.push_back(
                resolve_variables(f.as<std::string>(), assembly));
        }
    }

    if (node["output_files"] && node["output_files"].IsSequence()) {
        for (const auto& f : node["output_files"]) {
            step.output_files.push_back(
                resolve_variables(f.as<std::string>(), assembly));
        }
    }

    if (node["depends_on"] && node["depends_on"].IsSequence()) {
        for (const auto& dep : node["depends_on"]) {
            step.depends_on.push_back(dep.as<std::string>());
        }
    }

    return step;
}

// -- Variable substitution --------------------------------------------------

std::string FlowConfig::resolve_variables(const std::string& input,
                                          const Assembly& assembly)
{
    static const std::regex var_pattern(R"(\$\{([^}]+)\})");

    std::string result;
    std::sregex_iterator it(input.begin(), input.end(), var_pattern);
    std::sregex_iterator end;
    size_t last_pos = 0;

    for (; it != end; ++it) {
        const auto& match = *it;
        result += input.substr(last_pos, match.position() - last_pos);

        std::string expr = match[1].str();

        // Split by '.' into parts
        std::vector<std::string> parts;
        std::istringstream ss(expr);
        std::string part;
        while (std::getline(ss, part, '.')) {
            parts.push_back(part);
        }

        if (parts.size() < 2) {
            throw ChipletFormatException(
                "Invalid variable: ${" + expr + "}", 0, "flow");
        }

        std::string category = parts[0];
        if (category == "assembly") {
            // ${assembly.field}
            result += resolve_field("assembly", "", parts[1], assembly);
        } else if (parts.size() < 3) {
            throw ChipletFormatException(
                "Variable requires category.id.field: ${" + expr + "}",
                0, "flow");
        } else {
            // ${component.ID.field} or ${technology.ID.field}
            // field may be compound: position.x -> join parts[2..end]
            std::string field;
            for (size_t i = 2; i < parts.size(); ++i) {
                if (i > 2) field += ".";
                field += parts[i];
            }
            result += resolve_field(category, parts[1], field, assembly);
        }

        last_pos = match.position() + match.length();
    }

    result += input.substr(last_pos);
    return result;
}

std::string FlowConfig::resolve_field(const std::string& category,
                                      const std::string& id,
                                      const std::string& field,
                                      const Assembly& assembly)
{
    if (category == "assembly") {
        if (field == "name")        return assembly.name();
        if (field == "description") return assembly.description();
        if (field == "author")      return assembly.author();
        if (field == "units")       return assembly.units();
        throw ChipletFormatException(
            "Unknown assembly field: " + field, 0, "flow");
    }

    if (category == "component") {
        const Component* comp = assembly.component(id);
        if (!comp) {
            throw ChipletFormatException(
                "Unknown component: " + id, 0, "flow");
        }
        if (field == "id")          return comp->id();
        if (field == "name")        return comp->name();
        if (field == "technology")  return comp->technology();
        if (field == "layout_path") return comp->layout_path();
        if (field == "position.x")  return std::to_string(comp->position().x);
        if (field == "position.y")  return std::to_string(comp->position().y);
        if (field == "position.z")  return std::to_string(comp->position().z);
        if (field == "dimensions.width")
            return std::to_string(comp->dimensions().width);
        if (field == "dimensions.height")
            return std::to_string(comp->dimensions().height);
        if (field == "dimensions.thickness")
            return std::to_string(comp->dimensions().thickness);
        throw ChipletFormatException(
            "Unknown component field: " + field, 0, "flow");
    }

    if (category == "technology") {
        const Technology* tech = assembly.technology(id);
        if (!tech) {
            throw ChipletFormatException(
                "Unknown technology: " + id, 0, "flow");
        }
        if (field == "id")          return tech->id();
        if (field == "description") return tech->description();
        if (field == "layer_properties_path")
            return tech->layer_properties_path();
        if (field == "dbu")         return std::to_string(tech->dbu());
        throw ChipletFormatException(
            "Unknown technology field: " + field, 0, "flow");
    }

    throw ChipletFormatException(
        "Unknown variable category: " + category, 0, "flow");
}

// -- Writing ----------------------------------------------------------------

void FlowConfig::write_step(const FlowStep& step, YAML::Emitter& out)
{
    out << YAML::BeginMap;
    out << YAML::Key << "id" << YAML::Value << ps(step.id);

    if (step.name != step.id) {
        out << YAML::Key << "name" << YAML::Value << ps(step.name);
    }

    // Exactly the keys the author declared, and no others. Deriving them back
    // from interpreter/tool_path cannot distinguish `script: Y` from `tool: Y`,
    // so it renamed the author's key; and for a script-only step it emitted
    // neither, losing the program altogether.
    if (step.declared_tool) {
        out << YAML::Key << "tool" << YAML::Value << ps(*step.declared_tool);
    }
    if (step.declared_script) {
        out << YAML::Key << "script" << YAML::Value << ps(*step.declared_script);
    }
    // A step built in memory rather than parsed declares neither, so fall back
    // to the derived view to keep programmatic flows serialisable.
    if (!step.declared_tool && !step.declared_script) {
        if (!step.interpreter.empty()) {
            out << YAML::Key << "tool" << YAML::Value << ps(step.interpreter);
            out << YAML::Key << "script" << YAML::Value << ps(step.tool_path);
        } else if (!step.tool_path.empty()) {
            out << YAML::Key << "tool" << YAML::Value << ps(step.tool_path);
        }
    }

    if (!step.args.empty()) {
        out << YAML::Key << "args" << YAML::Value
            << YAML::Flow << YAML::BeginSeq;
        for (const auto& arg : step.args) {
            out << ps(arg);
        }
        out << YAML::EndSeq;
    }

    if (!step.input_files.empty()) {
        out << YAML::Key << "input_files" << YAML::Value
            << YAML::Flow << YAML::BeginSeq;
        for (const auto& f : step.input_files) {
            out << ps(f);
        }
        out << YAML::EndSeq;
    }

    if (!step.output_files.empty()) {
        out << YAML::Key << "output_files" << YAML::Value
            << YAML::Flow << YAML::BeginSeq;
        for (const auto& f : step.output_files) {
            out << ps(f);
        }
        out << YAML::EndSeq;
    }

    if (!step.depends_on.empty()) {
        out << YAML::Key << "depends_on" << YAML::Value
            << YAML::Flow << YAML::BeginSeq;
        for (const auto& dep : step.depends_on) {
            out << ps(dep);
        }
        out << YAML::EndSeq;
    }

    out << YAML::EndMap;
}

void FlowConfig::write_flow(const FlowEngine& engine, YAML::Emitter& out)
{
    out << YAML::Key << "flow" << YAML::Value << YAML::BeginMap;

    if (!engine.working_directory().empty()) {
        out << YAML::Key << "working_directory"
            << YAML::Value << ps(engine.working_directory());
    }

    if (!engine.environment().empty()) {
        out << YAML::Key << "environment" << YAML::Value << YAML::BeginMap;
        for (const auto& [key, value] : engine.environment()) {
            out << YAML::Key << ps(key) << YAML::Value << ps(value);
        }
        out << YAML::EndMap;
    }

    if (engine.step_count() > 0) {
        out << YAML::Key << "steps" << YAML::Value << YAML::BeginSeq;
        for (const auto& step : engine.steps()) {
            write_step(step, out);
        }
        out << YAML::EndSeq;
    }

    out << YAML::EndMap;
}

// -- FlowDefinition parsing/writing ----------------------------------------

FlowDefinition FlowConfig::parse_flow_definition(const YAML::Node& node,
                                                   const Assembly& assembly)
{
    if (!node.IsMap()) {
        throw ChipletFormatException("flow section must be a map", 0, "flow");
    }

    FlowDefinition def;

    if (node["working_directory"]) {
        def.working_directory = resolve_variables(
            node["working_directory"].as<std::string>(), assembly);
    }

    if (node["environment"] && node["environment"].IsMap()) {
        for (const auto& item : node["environment"]) {
            std::string key = item.first.as<std::string>();
            std::string value = resolve_variables(
                item.second.as<std::string>(), assembly);
            def.environment[key] = value;
        }
    }

    if (node["steps"]) {
        if (!node["steps"].IsSequence()) {
            throw ChipletFormatException(
                "flow.steps must be a sequence", 0, "flow.steps");
        }
        for (const auto& stepNode : node["steps"]) {
            def.steps.push_back(parse_step(stepNode, assembly));
        }
    }

    return def;
}

void FlowConfig::write_flow_definition(const FlowDefinition& def,
                                        YAML::Emitter& out)
{
    out << YAML::Key << "flow" << YAML::Value << YAML::BeginMap;

    if (!def.working_directory.empty()) {
        out << YAML::Key << "working_directory"
            << YAML::Value << ps(def.working_directory);
    }

    if (!def.environment.empty()) {
        out << YAML::Key << "environment" << YAML::Value << YAML::BeginMap;
        for (const auto& [key, value] : def.environment) {
            out << YAML::Key << ps(key) << YAML::Value << ps(value);
        }
        out << YAML::EndMap;
    }

    if (!def.steps.empty()) {
        out << YAML::Key << "steps" << YAML::Value << YAML::BeginSeq;
        for (const auto& step : def.steps) {
            write_step(step, out);
        }
        out << YAML::EndSeq;
    }

    out << YAML::EndMap;
}

} // namespace chiplet
