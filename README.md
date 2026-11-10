# Chiplet Studio

A 3D chiplet assembly design tool that uses KLayout as a library for 2D layout operations.

It reads the `.chiplet` interchange format, shows the assembly in an interactive
3D view, lets you drill down into the real GDS/OASIS layout in 2D, run a build
pipeline, inspect connectivity, and automate everything from an embedded Python
console.

## Status

> [!WARNING]
> Chiplet Studio is alpha software. Its menus and its scripting API can
> still change between versions.

What is tested: `scripts/run-gate.sh` builds Studio and runs its whole ctest
suite offline in a container. The suite covers reading and saving `.chiplet`
files, the commands behind undo and redo, the 3D and 2D views and the Python
API. The gate also reads the writer's output with a second YAML reader
(PyYAML), so a save that only Studio could read fails it.

What is not verified:

- Thicknesses in the 3D view are for display. Some are scaled on purpose (the
  silicon bulk is drawn 50 um thick, not as the 750 um wafer), so the view is no
  measurement of the stack.
- Nothing Studio shows or saves is a DRC, LVS or fabrication check. Those run in
  the PDK and ADK tools, not here.
- The portable bundle (`scripts/build-portable.sh`) is not part of the alpha
  and is not supported. In the alpha, Studio runs from the ADK tools image or
  from a build of this repository.
- A `.chiplet` file's `flow:` block names programs, and the Flow Pipeline panel
  runs them when you start them. Open only files you trust.

## Features

- **3D Visualization** - Interactive assembly view with orbit camera and per-component render modes
- **2D Drill-down** - KLayout-based GDS/OASIS viewing, with an assembly view and a shape-filtering slider
- **Flow Orchestrator** - Run the build steps a `.chiplet` file's `flow:` block defines (the Flow Pipeline panel appears when the file has one)
- **Net Graph** - Inspect the assembly's connectivity
- **Hierarchy Panel** - Tree view with component selection sync
- **Properties Panel** - Component details with unit conversion
- **Python Scripting** - Embedded Python interpreter for automation

## Python Scripting

Chiplet Studio includes an embedded Python interpreter for automation and scripting.

### Using the Python Console

1. Open **View > Python Console** (or press `` Ctrl+` ``)
2. Type Python commands interactively
3. Use the `chiplet_studio` module to manipulate the assembly

Saving through `save_assembly` also follows the document's safe-save policy:
an existing unrelated destination, or a loaded file changed externally, is
refused. Use a fresh filename for a new document; loading a file authorizes
replacing only its unchanged on-disk version.

### Example Script

```python
import chiplet_studio as cs

# Get the current assembly
asm = cs.get_current_assembly()

# Create a die
die = asm.create_component("my_die", "Die",
    width=5000.0, height=5000.0, thickness=100.0)

# Position it
die.set_position(1000.0, 2000.0, 50.0)

# Set technology
die.set_technology("ihp-sg13g2")

# List all components
for comp in asm.components():
    print(f"{comp.id}: {comp.position}")
```

### API Reference

**Assembly:**
- `get_current_assembly()` - Get the active assembly
- `create_component(id, type, width, height, thickness)` - Create component
- `component(id)` - Get component by ID
- `components()` - List all components
- `has_component(id)` - Check if component exists

**Component:**
- `id`, `type` - Read-only properties
- `name` - Read-write (settable from Python)
- `position`, `dimensions` - Read-only (use the setters below)
- `set_position(x, y, z)` - Set absolute position
- `move(dx, dy, dz)` - Relative movement
- `set_technology(tech_id)` - Set technology reference

## Build

### Prerequisites (Docker - Recommended)

```bash
# Build inside Docker
./scripts/build-docker.sh

# Run with display forwarding
./scripts/run-docker.sh
```

### Manual Build

Requires: Qt6, yaml-cpp, OpenGL, KLayout libraries

```bash
# Build KLayout first
cd extern/klayout && ./build.sh -j$(nproc)

# Build chiplet-studio
mkdir build && cd build
cmake .. -DKLAYOUT_BUILD_DIR=../extern/klayout/bin-release
make -j$(nproc)
```

## Running the application

The executable opens the GUI. Pass a `.chiplet` file to load an assembly on
startup:

```bash
./build/chiplet-studio                       # empty session
./build/chiplet-studio path/to/assembly.chiplet
```

### Portable bundle (not supported)

`scripts/build-portable.sh` once built a self-contained tarball and AppImage.
It has not been run since June 2026 and is not part of the alpha. Treat it as
unmaintained until it is revived and tested again.

## Project Structure

```
chiplet-studio/
├── src/
│   ├── main.cpp    # Application entry point
│   ├── core/       # Data model (Assembly, Component, Technology), flow engine
│   ├── formats/    # .chiplet consumer over the vendored chiplet_format_io library
│   ├── view3d/     # OpenGL 3D visualization
│   ├── view2d/     # KLayout integration
│   ├── scripting/  # Embedded Python (pybind11)
│   └── ui/         # Qt UI components
├── tests/          # Google Test suite, run through ctest
├── docs/           # Pages the ADK documentation includes
└── extern/klayout  # KLayout submodule
```

`src/formats/` no longer hand-parses the format: `ChipletFormat` is a thin
consumer that delegates `.chiplet` parsing and validation to the vendored,
Apache-2.0 `chiplet_format_io` reference library (a verbatim copy of
IHP-GmbH/chiplet-system-spec; see `src/formats/chiplet_format_io/VENDORED.md`)
and maps its `ChipletDocument` into `core/Assembly`.

## Documentation

The two contract documents this code is written against are maintained in
[IHP-GmbH/chiplet-system-spec](https://github.com/IHP-GmbH/chiplet-system-spec)
(Apache-2.0), so that the format can be implemented under any license:

- `docs/CHIPLET_FORMAT_SPEC.md` - the `.chiplet` format
- `docs/coord_frame_contract.md` - the coordinate frame, the `anchor:`
  convention and the z-mounting rule

Both are cited by section number throughout this source tree, and both are
backed here by tests: `tests/test_coord_frame_contract.cpp` and
`tests/test_chiplet_format*.cpp`.

This repository's own `docs/` holds only the pages the ADK documentation
includes: an index and a usage page, and the Python API reference, which
`scripts/gen-api-reference.py` generates from the built module. The contract
documents are not there, so a `docs/` citation in a source comment means the
spec repository above.

## Capabilities

- KLayout widget embedding
- Hierarchy panel with selection sync
- Properties panel with units
- ID-based component safety
- Command pattern (undo/redo)
- GDS3D tessellation integration
- Python scripting with pybind11

## Contributing

Contributions are welcome under GPL-3.0-or-later. We use a Developer Certificate
of Origin (DCO): sign your commits with `git commit -s`. See
[`CONTRIBUTING.md`](CONTRIBUTING.md) for details.

## License

Chiplet Studio is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

Copyright (C) 2026 IHP GmbH.

It is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
PURPOSE. See the [`LICENSE`](LICENSE) file for the full GPL-3.0 text.

Chiplet Studio links **KLayout** (GPL-2.0-or-later) and Qt 6 (LGPL-3.0), and
the combined work is licensed GPL-3.0-or-later. See [`THIRD-PARTY-LICENSES.md`](THIRD-PARTY-LICENSES.md)
for the licenses of all bundled and linked third-party components.
