# Chiplet Studio

A 3D chiplet assembly design tool that uses KLayout as a library for 2D layout operations.

## Features

- **3D Visualization** - Interactive assembly view with orbit camera
- **2D Drill-down** - KLayout-based GDS/OASIS viewing
- **Cross-section View** - Shader-based clipping planes
- **Hierarchy Panel** - Tree view with component selection sync
- **Properties Panel** - Component details with unit conversion

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
- `docs/CHANGELOG.md` - Development history
- `docs/CODE_STYLE.md` - Coding standards
- `docs/ARCHITECTURE.md` - Technical decisions

## Status

**Phase 3 Complete:** 170/170 tests passing

- [x] KLayout widget embedding
- [x] Hierarchy panel with selection sync
- [x] Properties panel with units
- [ ] 2D drill-down (partially complete)

## License

Private - All rights reserved
