// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * FlowStep.h - Data types for flow pipeline steps
 *
 * A FlowStep represents a single tool invocation in a flow pipeline.
 * The FlowEngine executes steps as subprocesses, tracking status and output.
 */

#ifndef CHIPLET_CORE_FLOW_FLOWSTEP_H
#define CHIPLET_CORE_FLOW_FLOWSTEP_H

#include <optional>
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

    // The `tool:` and `script:` keys exactly as the document declared them,
    // present only when the author wrote that key. tool_path and interpreter
    // above are the DERIVED runtime view; these two are what a save writes,
    // the same rule IOPad follows for io_class and layer: optionals with no
    // default, emit only what was declared.
    //
    // They exist because `tool:` and `script:` are not two spellings of one
    // field, and collapsing them into interpreter + tool_path has no inverse
    // for one of the three shapes an author can write:
    //   tool: X, script: Y  ->  interpreter=X, tool_path=Y     round-trips
    //   tool: X             ->  interpreter="", tool_path=X    round-trips
    //   script: Y           ->  both empty, and the key is gone
    // The third shape used to be dropped at parse (script: was read only
    // inside `if (node["tool"])`) and the writer then emitted neither key, so
    // the step came back as a bare id. Worse than the lost bytes: with both
    // fields empty, FlowEngine::build_program returned "" and Run started a
    // process with no program at all.
    std::optional<std::string> declared_tool;
    std::optional<std::string> declared_script;

    // What this step would actually run, or nullopt when the declared shape is
    // one this studio has no rule for. Script-only is exactly that: running the
    // script as its own program assumes a shebang and an exec bit the document
    // never promised. Returning an optional rather than an empty string forces
    // the caller to say what it does about it, instead of starting "".
    std::optional<std::string> program() const {
        if (!interpreter.empty()) return interpreter;
        if (!tool_path.empty()) return tool_path;
        return std::nullopt;
    }
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
