/**
 * FlowConfig.h - YAML parsing and writing for flow pipeline configuration
 *
 * Handles the "flow" section in .chiplet YAML files.
 * Supports variable substitution: ${assembly.field}, ${component.ID.field},
 * ${technology.ID.field} resolved against the Assembly data model.
 */

#ifndef CHIPLET_CORE_FLOW_FLOWCONFIG_H
#define CHIPLET_CORE_FLOW_FLOWCONFIG_H

#include <string>

// Forward declarations to avoid exposing yaml-cpp in header
namespace YAML {
    class Node;
    class Emitter;
}

namespace chiplet {

class Assembly;
class FlowEngine;
struct FlowStep;

class FlowConfig {
public:
    FlowConfig();
    ~FlowConfig();

    /**
     * Parse a flow section from YAML and populate a FlowEngine.
     * Variables (${...}) are resolved against the Assembly at parse time.
     * @throws ChipletFormatException on parse errors or unresolved variables
     */
    void parse_flow(const YAML::Node& node, const Assembly& assembly,
                    FlowEngine& engine);

    /**
     * Write the flow configuration to a YAML emitter.
     */
    void write_flow(const FlowEngine& engine, YAML::Emitter& out);

private:
    FlowStep parse_step(const YAML::Node& node, const Assembly& assembly);
    std::string resolve_variables(const std::string& input,
                                  const Assembly& assembly);
    std::string resolve_field(const std::string& category,
                              const std::string& id,
                              const std::string& field,
                              const Assembly& assembly);
};

} // namespace chiplet

#endif // CHIPLET_CORE_FLOW_FLOWCONFIG_H
