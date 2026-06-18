# Chiplet Studio

A 3D chiplet assembly design tool that uses KLayout as a library for 2D layout operations.

It reads the `.chiplet` interchange format, shows the assembly in an interactive
3D view, lets you drill down into the real GDS/OASIS layout in 2D, run a build
pipeline, inspect connectivity, and automate everything from an embedded Python
console.

## Features

- **3D Visualization** - Interactive assembly view with orbit camera and per-component render modes
- **2D Drill-down** - KLayout-based GDS/OASIS viewing, with an assembly view and a shape-filtering slider
- **Flow Orchestrator** - Run a configurable build pipeline against the assembly (View > Flow Pipeline)
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

### Portable bundle (no Docker, no root)

To run on machines without Docker or system dependencies, build a
self-contained bundle with `scripts/build-portable.sh`:

```bash
./scripts/build-portable.sh             # tarball, bundled software-GL fallback
./scripts/build-portable.sh --appimage  # also emit an AppImage
./scripts/build-portable.sh --lean      # smaller, host OpenGL only (~65 MB)
```

This writes to `dist/`:

- **`chiplet-studio-portable.tar.gz`** (recommended); extract anywhere and run:
  ```bash
  tar xf chiplet-studio-portable.tar.gz
  ./chiplet-studio-portable/AppRun [assembly.chiplet]
  ```
- **`Chiplet_Studio-x86_64.AppImage`**, single file:
  ```bash
  chmod +x Chiplet_Studio-x86_64.AppImage
  ./Chiplet_Studio-x86_64.AppImage [assembly.chiplet]
  # on systems without FUSE2:
  ./Chiplet_Studio-x86_64.AppImage --appimage-extract-and-run [assembly.chiplet]
  ```

The bundle runs on any glibc 2.35+ Linux (Ubuntu 22.04 or newer). By default it
uses the host GPU when it provides OpenGL 3.3, otherwise it transparently falls
back to a bundled software renderer (llvmpipe). Force a mode with the
`CHIPLET_GL` environment variable:

```bash
CHIPLET_GL=hardware ./AppRun   # always use the host GPU
CHIPLET_GL=software ./AppRun   # always use the bundled software renderer
```

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
├── tests/          # Google Test suite (562 tests via ctest)
├── docs/           # Documentation
└── extern/klayout  # KLayout submodule
```

`src/formats/` no longer hand-parses the format: `ChipletFormat` is a thin
consumer that delegates `.chiplet` parsing and validation to the vendored,
Apache-2.0 `chiplet_format_io` reference library (a verbatim copy of
IHP-GmbH/chiplet-spec; see `src/formats/chiplet_format_io/VENDORED.md`) and maps
its `ChipletDocument` into `core/Assembly`.

## Documentation

- `docs/ARCHITECTURE.md` - Architecture and technical decisions
- `docs/CHIPLET_FORMAT_SPEC.md` - Pointer to the canonical `.chiplet` spec (IHP-GmbH/chiplet-spec)
- `docs/coord_frame_contract.md` - Coordinate frame contract for the pipeline
- `docs/interconnect_render_contract.md` - Interconnect body rendering and die seating contract
- `docs/FLOW_ORCHESTRATOR.md` - Flow orchestration
- `docs/CODE_STYLE.md` - Coding standards

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

Chiplet Studio links **KLayout** (GPL-3.0-or-later), so the combined work is
licensed GPL-3.0-or-later. See [`THIRD-PARTY-LICENSES.md`](THIRD-PARTY-LICENSES.md)
for the licenses of all bundled and linked third-party components.
