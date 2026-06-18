# Architecture

## Overview

Chiplet Studio is a desktop tool for visualizing and assembling heterogeneous
chiplet packages. It reads the `.chiplet` interchange format, shows the package
in an interactive OpenGL 3D view, lets you drill from any component down into its
real GDS layers in an embedded KLayout 2D view, runs an external build pipeline
through a flow orchestrator, and exposes the whole data model to an embedded
Python console.

KLayout is used as a library (GDS/OASIS parsing, layout data structures, and the
2D drill-down widget); it is not the host application. Chiplets are inherently
3D, KLayout's `LayoutView` and transforms (`dbTrans.h`) are 2D only, and adding
OpenGL to KLayout would be invasive, so 3D assembly lives in this tool instead.

Related docs:

- [CHIPLET_FORMAT_SPEC.md](CHIPLET_FORMAT_SPEC.md) - pointer to the canonical
  `.chiplet` spec (IHP-GmbH/chiplet-spec).
- [coord_frame_contract.md](coord_frame_contract.md) - the coordinate/anchor
  contract shared with downstream tools.
- [interconnect_render_contract.md](interconnect_render_contract.md) -
  interconnect body rendering and die seating.
- [FLOW_ORCHESTRATOR.md](FLOW_ORCHESTRATOR.md) - the flow engine in detail.

## Directory Structure

```
chiplet-studio/
├── src/
│   ├── core/                       # Data model (Assembly, Component, Interface,
│   │   │                           #   Technology, Netlist, ConnectionStack, IOPad)
│   │   ├── commands/               # Undoable commands (CmdMoveComponent, ...)
│   │   └── flow/                   # Flow engine (FlowEngine, FlowDefinition, FlowStep)
│   ├── formats/                    # .chiplet host layer (ChipletFormat)
│   │   └── chiplet_format_io/      # VENDORED Apache-2.0 reference reader/writer
│   ├── view3d/                     # OpenGL 3D visualization (AssemblyView, meshing)
│   ├── view2d/                     # KLayout 2D drill-down (KLayout2DView, KLayoutBridge)
│   ├── scripting/                  # Embedded Python (ScriptEngine, PyBindings)
│   └── ui/                         # Qt UI (MainWindow, panels)
├── extern/
│   ├── klayout/                    # KLayout submodule (prebuilt libs in bin-release)
│   └── GDS3D/                      # Vendored GDS3D, built inline as chiplet_gds3d
├── tests/
└── docs/
```

## Data Model

The model is a `chiplet::Assembly` (see `src/core/Assembly.h`): a non-copyable,
move-only container holding components, interfaces, technologies, connection
stacks, a netlist, and an optional flow definition, plus package metadata
(`name`, `author`, `units`, and `assembly_gds`, the path to the merged layout
used by the 2D view).

### Component

A `Component` (`src/core/Component.h`) is one physical element. `ComponentType`
has four values: `Die`, `DieArray`, `Interposer`, and `Substrate`. Beyond the
obvious identity, technology reference, layout path, and `Position3D` /
`Dimensions3D` geometry (all in micrometers), each component carries:

- `Orientation` (`FaceUp` for wirebond, `FaceDown` for flip-chip, which mirrors
  in X).
- `Anchor` (`GdsOrigin` or `BboxCenter`): how the local mesh is built relative to
  the declared `position`. This is normative; see
  [coord_frame_contract.md](coord_frame_contract.md) section 2.
- `RenderMode` (per-component 3D style; see the render pipeline below).
- A connection-stack reference (`connection()`), used to auto-compute Z.
- One or more cell names for flat multi-cell GDS files.
- `ComponentArray` config for `DieArray` (grid/linear pitch and counts).
- External `IOPad`s (e.g. wire-bond pads on the interposer).

### Interface

An `Interface` (`src/core/Interface.h`) is a physical connection between
components. `InterfaceType` has four values: `MicroBump` (die to interposer,
fine pitch), `CopperPillar` (interposer to substrate), `TSV` (through-silicon
via), and `WireBond`. Endpoints reference a component, a surface (`top` /
`bottom`), and a port layer; physical parameters (pitch, diameter, height) are
in micrometers.

## File Format

### `.chiplet` (YAML)

The single human-readable source format. The canonical spec lives in
IHP-GmbH/chiplet-spec; this repo only points at it via
[CHIPLET_FORMAT_SPEC.md](CHIPLET_FORMAT_SPEC.md). There is no binary cache
format; the assembly references its merged layout through `assembly_gds` rather
than a generated cache.

A document carries package metadata, `technologies`, `connection_stacks`,
`components`, an optional `interconnect` block, `interfaces`, a `netlist`, and an
optional `flow` build block.

### Parsing: the vendored `chiplet_format_io` library

`.chiplet` parsing is no longer hand-written. It is delegated to a vendored copy
of the C++ reference reader/writer from IHP-GmbH/chiplet-spec, living at
`src/formats/chiplet_format_io/` (see its `VENDORED.md`; upstream commit
`1a56f61`, Apache-2.0). Only the public header and the implementation are
vendored; the host wires them in via the top-level `CMakeLists.txt`
(`src/formats/chiplet_format_io/include` on the include path,
`<chiplet_format_io/chiplet_format_io.hpp>`).

The split is deliberate. Chiplet Studio is GPL-3.0-or-later because it links
KLayout; keeping format parsing in a permissively licensed, dependency-clean
layer (yaml-cpp only, no Qt, no KLayout) lets adk-tools and other downstream
consumers reuse it. The library produces a plain-struct `ChipletDocument`
(`technologies`, `connection_stacks`, `components`, optional `interconnect`,
`interfaces`, `netlist`, plus `flow_yaml` preserved verbatim).

`chiplet::ChipletFormat` (`src/formats/ChipletFormat.h/.cpp`) is the thin host
consumer: `load()` calls the library to parse and validate, then maps the
`ChipletDocument` into `core/Assembly` and layers on the studio-specific
concerns the reference library stays out of: path resolution (relative-to-file
plus `${VAR}` expansion), `qWarning` diagnostics, Z auto-calculation from
connection stacks, GDS3D techfile auto-load, and flow parsing. The writer
(`save()`) still emits via yaml-cpp directly.

## 3D Render Pipeline

`AssemblyView` (`src/view3d/AssemblyView.cpp`) is the OpenGL widget. Each frame
it buckets visible components by their `RenderMode` and renders in three passes:

1. Opaque (depth write on): `Solid`, `Detailed`, `DetailedNoSubstrate`.
2. Transparent (depth write off, sorted back-to-front): `Transparent`.
3. Wireframe overlays (depth write off): `Wireframe`.

`Hidden` components are skipped. `Detailed` and `DetailedNoSubstrate` trigger
full GDS layer tessellation (the latter omits the silicon bulk slab), backed by
`GDSLayerExtractor`, `LayerMeshBuilder`, and `ShapeFilter`; the other modes use
box meshes.

A logarithmic depth buffer keeps both micron-scale features and millimetre-scale
packages crisp without Z-fighting. The fragment shaders write
`gl_FragDepth = log2(1 + w) * Fcoef_half`, with `Fcoef_half` derived from the
camera's `fcoef()` and uploaded as a uniform each pass.

Default render modes are set per type (transparent for dies/interposer, solid
for substrate). For the contract on how interconnect bodies and die seating
relate to component geometry, see
[interconnect_render_contract.md](interconnect_render_contract.md).

## 2D Drill-Down (KLayout)

Two distinct pieces use the KLayout libraries:

- `KLayout2DView` (`src/view2d/KLayout2DView.h`) is the interactive 2D widget. It
  embeds `lay::LayoutViewWidget` with a layer panel and is the drill-down target
  from the 3D view. `DrillDownPanel` wraps it with navigation and a layer/
  hierarchy side panel; `CellComponentMapper` links GDS wrapper cells back to
  component IDs so a 3D selection navigates the 2D view and vice versa.
  `MainWindow` owns `m_klayout2DView` and `m_drillDownPanel`.
- `KLayoutBridge` (`src/view2d/KLayoutBridge.h`) is a headless `db::Layout`
  wrapper for parsing, bounding boxes, and cell queries, with no visualization.

### KLayoutBridge (PIMPL pattern)

`KLayoutBridge` hides all KLayout types behind a PIMPL so the rest of the build
stays decoupled and can compile without KLayout:

```cpp
// Header (no KLayout includes when HAVE_KLAYOUT is undefined)
class KLayoutBridge {
    struct Impl;
    std::unique_ptr<Impl> m_impl;
public:
    bool load_layout(const std::string& path);
    // ...
};

// Implementation
#ifdef HAVE_KLAYOUT
    struct KLayoutBridge::Impl {
        std::unique_ptr<db::Layout> mp_layout;  // KLayout type
    };
    // Full implementation
#else
    struct KLayoutBridge::Impl { /* empty */ };
    // Stub implementation (returns false/empty)
#endif
```

## Command / Undo Model

All state-modifying operations go through `CommandProcessor`
(`src/core/CommandProcessor.h`). `execute(CommandPtr)` runs a command and pushes
it on the undo stack (clearing redo); `undo()` / `redo()` move commands between
the two stacks. History is capped at `MAX_UNDO_DEPTH = 100`, an optional
`CommandJournal` provides crash recovery, and Qt signals (`command_executed`,
`stack_changed`, `can_undo_changed`, `can_redo_changed`) drive the UI. Concrete
commands live in `src/core/commands/`: `CmdMoveComponent`, `CmdRenameComponent`,
`CmdSetRenderMode`. The Python console mutates the model through this same
processor, so scripted edits are undoable.

## Scripting (Embedded Python)

When built with Python support, the app embeds a Python interpreter via
pybind11. `ScriptEngine` (`src/scripting/ScriptEngine.h`) owns it: `initialize()`
brings up the interpreter and the `chiplet_studio` module, `set_assembly(Assembly*,
CommandProcessor*)` exposes the live model and its undo processor to scripts, and
`execute()` / `execute_file()` / `execute_line()` run code (the last with REPL
multiline buffering). stdout/stderr are redirected to the GUI console via the
`output` / `error_output` signals. `is_available()` reports whether the binary
was compiled with Python at all.

The bindings are defined in `src/scripting/PyBindings.cpp` and built two ways
from one source: linked into the main executable (`pybind11::embed`, the in-
process console), and as a standalone module `chiplet_studio.so`
(`pybind11_add_module(chiplet_studio_py)`, output in `${CMAKE_BINARY_DIR}/python`)
for running scripts under a system Python. The standalone module and the embedded
interpreter have separate global state, so model sharing only works in-process.

## Flow Engine

`FlowEngine` (`src/core/flow/FlowEngine.h`) is a generic subprocess pipeline
runner with zero business logic. It holds a collection of `FlowStep`s, resolves
dependencies with `topological_sort()`, and runs each step asynchronously through
`QProcess`, emitting `step_started`, `step_finished`, `step_output`, and
`flow_finished`. An `Assembly` carries an optional `FlowDefinition`
(`has_flow()`), parsed from the `.chiplet` `flow` block. See
[FLOW_ORCHESTRATOR.md](FLOW_ORCHESTRATOR.md) for the full design and the
`FlowPanel` UI.

## Build Configuration

Two independent options gate optional subsystems.

### Python (`ENABLE_PYTHON_SCRIPTING`, default ON)

```
ENABLE_PYTHON_SCRIPTING?
├── ON  → find_package(Python3) found?
│         ├── Yes → FetchContent pybind11, HAVE_PYTHON=TRUE
│         └── No  → warn, HAVE_PYTHON=FALSE
└── OFF → HAVE_PYTHON=FALSE
```

`HAVE_PYTHON` gates the embedded interpreter and the standalone module.

### KLayout (`KLAYOUT_BUILD_DIR` → `HAVE_KLAYOUT`)

KLayout is built separately and pointed at via `KLAYOUT_BUILD_DIR`. `HAVE_KLAYOUT`
defaults to FALSE; CMake only flips it ON when a KLayout library is found there:

```
KLAYOUT_BUILD_DIR set?
├── Yes → libklayout_db.{so,dylib} / klayout_db.dll present?
│         ├── Yes → HAVE_KLAYOUT=TRUE
│         └── No  → warn, HAVE_KLAYOUT stays FALSE
└── No  → warn, HAVE_KLAYOUT stays FALSE
```

The build is usable in either state: without KLayout the project still compiles
(PIMPL stubs), CI does not need to build KLayout, and UI work proceeds without a
KLayout setup.

## Validation Architecture

`TechnologyValidation` (`src/core/Technology.h`) and `AssemblyValidation`
(`src/core/Assembly.h`) are separate structs with the same shape:
`{ valid, errors, warnings }`, where `add_error()` appends and sets `valid=false`
while `add_warning()` only appends. `AssemblyValidation::merge(TechnologyValidation,
context)` folds a technology result in, prefixing each message with the context
label.

Checks performed:

- Technology: ID not empty, `.lyp` file exists, DBU positive (and a warning if
  DBU exceeds 1 um).
- Assembly: every technology reference resolves, and every referenced layout file
  exists.

## Dependencies

- Qt6 (UI framework, `QProcess` for the flow engine).
- OpenGL (3D rendering).
- yaml-cpp (YAML parsing; confined to the vendored `chiplet_format_io` library
  and the `ChipletFormat` writer).
- nlohmann/json (fetched via FetchContent).
- Python3 + pybind11 (optional, via `ENABLE_PYTHON_SCRIPTING`; pybind11 fetched
  via FetchContent).
- GDS3D (vendored under `extern/GDS3D`, built inline as the `chiplet_gds3d`
  target; used for GDS3D techfile parsing, `GDSProcess`).
- KLayout libraries (optional, via `KLAYOUT_BUILD_DIR`; GDS/OASIS parsing and the
  2D drill-down view).
