# Architecture

## Overview

Chiplet Studio is a 3D chiplet assembly design tool that uses KLayout as a library for 2D layout operations.

## Key Decisions

### Why a New Tool (Not Extending KLayout)
- Chiplets are inherently 3D; KLayout is fundamentally 2D
- KLayout's LayoutView assumes single 2D surface
- KLayout's transformations are 2D only (dbTrans.h)
- Adding OpenGL/3D to KLayout would be invasive

### KLayout Integration
KLayout is used as a Git submodule providing:
- GDS/OASIS file parsing (libklayout_db)
- Layout data structures (Layout, Cell, shapes)
- 2D visualization for drill-down (libklayout_laybasic)
- Utility functions (libklayout_tl)

## Directory Structure

```
chiplet-studio/
├── src/
│   ├── core/           # Data model (Assembly, Component, Interface)
│   ├── formats/        # .chiplet format parser/writer
│   ├── view3d/         # OpenGL 3D visualization
│   ├── view2d/         # KLayout bridge for 2D drill-down
│   └── ui/             # Qt UI components
├── extern/
│   └── klayout/        # KLayout submodule
├── tests/
└── docs/
```

## Data Model

### Assembly
Top-level container representing a chiplet package.

### Component
Individual element in the assembly:
- Die (with technology, layout file, position)
- Interposer
- Substrate

### Interface
Physical connection between components:
- MicroBump (die to interposer)
- CopperPillar (interposer to substrate)

## File Format

### .chiplet (YAML)
Human-readable source format for version control.
Contains: metadata, technologies, components, interfaces, netlist.

### .chiplet-cache (Binary)
Auto-generated cache for rendering performance.
Contains: meshes, textures, spatial index.

## Dependencies

- Qt6 (UI framework)
- OpenGL (3D rendering)
- yaml-cpp (YAML parsing)
- KLayout libraries (layout operations)
