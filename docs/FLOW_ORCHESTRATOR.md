# Flow Orchestrator

The flow orchestrator executes multi-step tool pipelines defined in the `flow:` section of `.chiplet` files. It runs scripts as subprocesses, tracks dependencies between steps, and displays progress in the FlowPanel UI.

## YAML Format Reference

```yaml
flow:
  working_directory: /path/to/project    # Optional, defaults to .chiplet file directory
  environment:                            # Optional, extra env vars for all steps
    PDK_ROOT: /opt/pdk
    DEBUG: "1"
  steps:
    - id: step_id                         # Required, unique identifier
      name: "Human-Readable Name"         # Optional, defaults to id
      tool: python3                       # Required, interpreter or binary path
      script: /path/to/script.py          # Optional, used with tool as interpreter
      args:                               # Optional, command-line arguments
        - --input
        - file.gds
        - "${assembly.name}"              # Variable substitution supported
      input_files: [input.gds]            # Optional, expected inputs
      output_files: [output.gds]          # Optional, expected outputs
      depends_on: [other_step_id]         # Optional, prerequisite step IDs
```

### Tool/Script Mapping

| YAML | interpreter | tool_path | Command executed |
|------|-------------|-----------|-----------------|
| `tool: python3` + `script: run.py` | `python3` | `run.py` | `python3 run.py <args>` |
| `tool: /usr/bin/klayout` (no script) | (empty) | `/usr/bin/klayout` | `/usr/bin/klayout <args>` |

## Variable Substitution

Variables use `${category.id.field}` syntax and are resolved at parse time against the Assembly data model.

### Assembly fields
- `${assembly.name}`, `${assembly.description}`, `${assembly.author}`, `${assembly.units}`

### Component fields
- `${component.<ID>.id}`, `${component.<ID>.name}`, `${component.<ID>.technology}`
- `${component.<ID>.layout_path}`
- `${component.<ID>.position.x}`, `.y`, `.z`
- `${component.<ID>.dimensions.width}`, `.height`, `.thickness`

### Technology fields
- `${technology.<ID>.id}`, `${technology.<ID>.description}`
- `${technology.<ID>.layer_properties_path}`, `${technology.<ID>.dbu}`

Variables can appear in `working_directory`, `environment` values, `tool`, `script`, `args`, `input_files`, and `output_files`.

## Architecture

### Data flow

```
.chiplet file
    |
    v
ChipletFormat::load()          [worker thread via QtConcurrent]
    |
    v
FlowDefinition struct          [plain C++ data, no QObject]
    |  stored in Assembly
    v
MainWindow::populateFlowEngine()  [main thread]
    |
    v
FlowEngine (QObject)           [executes steps via QProcess]
    |
    v
FlowPanel (QWidget)            [displays step status, run buttons]
```

### Why FlowDefinition instead of FlowEngine in Assembly?

ChipletFormat::load() runs in a worker thread. FlowEngine inherits QObject, and creating QObjects on worker threads then transferring them to the GUI thread requires moveToThread() -- fragile and error-prone. FlowDefinition is a plain struct (vector + map + string), fully thread-safe, and trivially copyable/movable.

### Key classes

- **FlowDefinition** (`src/core/flow/FlowDefinition.h`) -- Thread-safe data container
- **FlowConfig** (`src/core/flow/FlowConfig.h`) -- YAML parsing/writing for both FlowEngine and FlowDefinition
- **FlowEngine** (`src/core/flow/FlowEngine.h`) -- QObject-based subprocess executor
- **FlowPanel** (`src/ui/FlowPanel.h`) -- Qt widget showing step list, status, run buttons
- **Assembly** (`src/core/Assembly.h`) -- Owns FlowDefinition via `has_flow()` / `flow_definition()`

## Step Dependency Graph

Steps execute in topological order based on `depends_on`. FlowEngine performs topological sort before execution. Steps with no dependencies can potentially run in parallel (not yet implemented -- currently sequential).

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

## Adding New Flow Steps

1. Add a step entry to the `flow.steps` sequence in your `.chiplet` file
2. Set `depends_on` to the IDs of steps that must complete first
3. Use variable substitution to reference assembly paths dynamically
4. Reload the file in Chiplet Studio -- the FlowPanel will show the new step

## Backward Compatibility

The `flow:` section is optional. Files without it load normally with `assembly->has_flow()` returning false and the FlowPanel showing an empty state.
