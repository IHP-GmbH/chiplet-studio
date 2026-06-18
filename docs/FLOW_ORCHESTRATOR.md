# Flow Orchestrator

You have a chiplet assembly and a set of tools that need to run on it (generate a netlist, place bumps, convert to GDS, run DRC). The flow orchestrator lets you describe that pipeline once, inside the `.chiplet` file, and run it from inside Chiplet Studio: each step runs as a subprocess, steps run in dependency order, and the FlowPanel shows live status and output.

The pipeline lives in the optional top-level `flow:` section of a `.chiplet` file. Files without it load normally; `assembly->has_flow()` returns false and the FlowPanel shows an empty state.

## YAML Format Reference

```yaml
flow:
  working_directory: /path/to/project    # Optional, defaults to the .chiplet file's directory
  environment:                            # Optional, extra env vars merged onto the inherited environment
    PDK_ROOT: /opt/pdk
    DEBUG: "1"
  steps:
    - id: step_id                         # Required, unique identifier
      name: "Human-Readable Name"         # Optional, defaults to id
      tool: python3                       # Required, interpreter or binary path
      script: /path/to/script.py          # Optional, used with tool as the interpreter
      args:                               # Optional, command-line arguments
        - --input
        - file.gds
        - "${assembly.name}"              # Variable substitution supported
      input_files: [input.gds]            # Optional, expected inputs (informational)
      output_files: [output.gds]          # Optional, expected outputs (informational)
      depends_on: [other_step_id]         # Optional, prerequisite step IDs
```

`input_files` and `output_files` are recorded for documentation; the engine does not check or stage them.

### Tool/Script mapping

| YAML | interpreter | tool_path | Command executed |
|------|-------------|-----------|-----------------|
| `tool: python3` + `script: run.py` | `python3` | `run.py` | `python3 run.py <args>` |
| `tool: /usr/bin/klayout` (no script) | (empty) | `/usr/bin/klayout` | `/usr/bin/klayout <args>` |

When `script` is present, `tool` becomes the interpreter and `script` is prepended as the first argument; otherwise `tool` is run directly.

## Variable Substitution

Variables use `${category.id.field}` syntax and are resolved at parse time against the Assembly data model. They can appear in `working_directory`, `environment` values, `tool`, `script`, `args`, `input_files`, and `output_files`.

Assembly fields:
- `${assembly.name}`, `${assembly.description}`, `${assembly.author}`, `${assembly.units}`

Component fields (`<ID>` is the component id):
- `${component.<ID>.id}`, `${component.<ID>.name}`, `${component.<ID>.technology}`
- `${component.<ID>.layout_path}`
- `${component.<ID>.position.x}`, `.y`, `.z`
- `${component.<ID>.dimensions.width}`, `.height`, `.thickness`

Technology fields (`<ID>` is the technology id):
- `${technology.<ID>.id}`, `${technology.<ID>.description}`
- `${technology.<ID>.layer_properties_path}`, `${technology.<ID>.dbu}`

## Execution Model

`run_all()` topologically sorts the steps and runs them strictly sequentially, one `QProcess` at a time; there is no parallel dispatch yet even when steps are independent. `run_step(id)` runs a single step in isolation. Both reset the target steps (status back to `Pending`, log cleared, `exit_code` to `-1`) before launching.

Per-step state, visible in the FlowPanel:

- `status`: `Pending`, `Running`, `Success`, `Error`, or `Skipped`.
- `log`: captured output, with the step's stdout and stderr merged into one stream.
- `exit_code`: process exit code, `-1` if it never produced one (for example, the tool failed to start).

### Stop-on-failure and cancel

Failure stops the whole run. When a step exits non-zero or crashes, fails to start, or times out, the engine marks it `Error`, marks every remaining queued step `Skipped`, and emits `flow_finished(false)`. There is no continue-on-error mode. `cancel()` kills the running process and marks the remaining queued steps `Skipped`.

### Signals (integration surface)

`FlowEngine` is a `QObject`; the FlowPanel and any other consumer wire up to these signals for live updates:

- `step_started(QString id)`
- `step_output(QString id, QString text)`, emitted incrementally as the process writes stdout or stderr.
- `step_finished(QString id, bool success)`
- `flow_finished(bool all_success)`, emitted once per `run_all`/`run_step` invocation, including on early stop and cancel.

## Architecture

### Data flow

```
.chiplet file
    |
    v
ChipletFormat::load()             [worker thread via QtConcurrent]
    |
    v
FlowDefinition struct             [plain C++ data, no QObject]
    |  stored in Assembly
    v
MainWindow::populateFlowEngine()  [main thread]
    |
    v
FlowEngine (QObject)              [executes steps via QProcess]
    |
    v
FlowPanel (QWidget)               [step table, status, run buttons, per-step log]
```

### Why FlowDefinition instead of FlowEngine in Assembly

`ChipletFormat::load()` runs in a worker thread. `FlowEngine` is a `QObject`, and creating QObjects on a worker thread then transferring them to the GUI thread requires `moveToThread()`, which is fragile and error-prone. `FlowDefinition` is a plain struct (vector, map, string): thread-safe and trivially copyable, so the parser fills it on the worker and the main thread copies it into a fresh `FlowEngine`.

### Key classes

- **FlowDefinition** (`src/core/flow/FlowDefinition.h`): thread-safe data container.
- **FlowConfig** (`src/core/flow/FlowConfig.h`): YAML parsing and writing for both FlowEngine and FlowDefinition.
- **FlowEngine** (`src/core/flow/FlowEngine.h`): QObject-based subprocess executor.
- **FlowPanel** (`src/ui/FlowPanel.h`): Qt widget with a Step/Status/Time/Run table, a Run All button, per-row run buttons, and a per-step log pane.
- **Assembly** (`src/core/Assembly.h`): owns the FlowDefinition via `has_flow()` and `flow_definition()`.

## Step Dependency Graph

Steps run in topological order based on `depends_on`. `FlowEngine::topological_sort()` (Kahn's algorithm) runs before execution; a cycle yields an empty sort and `run_all()` returns false without launching anything. A `depends_on` entry that names an unknown step id is silently ignored, so it does not constrain ordering; check your ids if a step runs earlier than expected.

For example, with steps declaring `generate_netlist -> generate_cupillars -> convert_hyp_to_gds -> run_drc` via `depends_on`:

```
generate_netlist
       |
       v
generate_cupillars
       |
       v
convert_hyp_to_gds
       |
       v
run_drc
```

A minimal, real two-step pipeline lives in `tests/fixtures/with_flow.chiplet` (`step_hello -> step_goodbye` over `/bin/echo`).

## Adding New Flow Steps

1. Add a step entry to the `flow.steps` sequence in your `.chiplet` file.
2. Set `depends_on` to the ids of steps that must complete first.
3. Use variable substitution to reference assembly paths dynamically.
4. Reload the file in Chiplet Studio (F5); the FlowPanel will show the new step.
