# Chiplet Studio

A 3D chiplet assembly design tool that uses KLayout as a library for 2D layout operations.

## Features

- **3D Visualization** - Interactive assembly view with orbit camera
- **2D Drill-down** - KLayout-based GDS/OASIS viewing
- **Cross-section View** - Shader-based clipping planes
- **Hierarchy Panel** - Tree view with component selection sync
- **Properties Panel** - Component details with unit conversion
- **Python Scripting** - Embedded Python interpreter for automation

## Python Scripting

Chiplet Studio includes an embedded Python interpreter for automation and scripting.

### Using the Script Console

1. Open **View > Script Console** (or press `Ctrl+P`)
2. Type Python commands interactively
3. Use `chiplet_studio` module to manipulate the assembly

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
- `id`, `name`, `type` - Read-only properties
- `position`, `dimensions` - Read-only (use setters)
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

## Project Structure

```
chiplet-studio/
├── src/
│   ├── core/      # Data model (Assembly, Component, Technology)
│   ├── formats/   # .chiplet YAML parser
│   ├── view3d/    # OpenGL 3D visualization
│   ├── view2d/    # KLayout integration
│   └── ui/        # Qt UI components
├── tests/         # Google Test suite (170 tests)
├── docs/          # Documentation
└── extern/klayout # KLayout submodule
```

## Documentation

- `docs/PLAN.md` - Implementation roadmap
- `docs/FEATURES.md` - Feature tracking
- `docs/CODE_STYLE.md` - Coding standards
- `docs/ARCHITECTURE.md` - Technical decisions

## Status

**Phase 7 Complete:** Full refactoring finished

- [x] KLayout widget embedding
- [x] Hierarchy panel with selection sync
- [x] Properties panel with units
- [x] ID-based component safety
- [x] Command pattern (undo/redo)
- [x] GDS3D tessellation integration
- [x] Python scripting with pybind11
- [x] Final integration verification

## License

Private - All rights reserved
