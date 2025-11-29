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

## KLayout Integration Architecture

### Conditional Compilation (HAVE_KLAYOUT)

The project supports building with or without KLayout libraries:

```
CMake Detection:
├── KLAYOUT_BUILD_DIR set?
│   ├── Yes → Check for libklayout_db.so
│   │   ├── Found → HAVE_KLAYOUT=ON
│   │   └── Not found → Warning, HAVE_KLAYOUT=OFF
│   └── No → Warning, HAVE_KLAYOUT=OFF
```

Benefits:
- Project builds without KLayout dependencies
- CI/CD can run without building KLayout
- Developers can work on UI without KLayout setup

### KLayoutBridge (PIMPL Pattern)

```cpp
// Header (no KLayout includes when HAVE_KLAYOUT undefined)
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

### Validation Architecture

```
TechnologyValidation
├── valid: bool
├── errors: vector<string>
└── warnings: vector<string>
    Methods:
    ├── add_error() → sets valid=false
    └── add_warning() → keeps valid=true

AssemblyValidation
├── Inherits TechnologyValidation structure
└── merge(TechnologyValidation, context)
    → Prefixes messages with context
```

Validation checks:
- **Technology**: ID not empty, .lyp file exists, DBU positive
- **Assembly**: All tech refs resolve, layout files exist

## Dependencies

- Qt6 (UI framework)
- OpenGL (3D rendering)
- yaml-cpp (YAML parsing)
- KLayout libraries (optional - layout operations)
