/**
 * FlowStep.h - Data types for flow pipeline steps
 *
 * A FlowStep represents a single tool invocation in a flow pipeline.
 * The FlowEngine executes steps as subprocesses, tracking status and output.
 */

#ifndef CHIPLET_CORE_FLOW_FLOWSTEP_H
#define CHIPLET_CORE_FLOW_FLOWSTEP_H

#include <string>
#include <vector>

namespace chiplet {

/**
 * Execution status of a flow step.
 */
enum class StepStatus {
    Pending,
    Running,
    Success,
    Error,
    Skipped
};

/**
 * Convert StepStatus to string representation.
 */
std::string step_status_to_string(StepStatus status);

/**
 * Parse a StepStatus from string. Returns Pending for unknown values.
 */
StepStatus step_status_from_string(const std::string& s);

/**
 * FlowStep describes a single tool invocation in a flow pipeline.
 *
 * Configuration fields (serialized to YAML):
 *   id, name, tool_path, interpreter, args, input_files, output_files, depends_on
 *
 * Runtime state (set by FlowEngine during execution):
 *   status, log, exit_code
 */
struct FlowStep {
    std::string id;                         // Unique step identifier
    std::string name;                       // Human-readable display name
    std::string tool_path;                  // Script or binary path
    std::string interpreter;                // e.g. "python3" (empty for native binaries)
    std::vector<std::string> args;          // Command-line arguments
    std::vector<std::string> input_files;   // Expected input files
    std::vector<std::string> output_files;  // Expected output files
    std::vector<std::string> depends_on;    // IDs of prerequisite steps

    // Runtime state
    StepStatus status = StepStatus::Pending;
    std::string log;                        // Captured stdout + stderr
    int exit_code = -1;
};

} // namespace chiplet

#endif // CHIPLET_CORE_FLOW_FLOWSTEP_H
