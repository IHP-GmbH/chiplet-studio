#!/bin/bash
# run-chiplet-demo.sh - Launch Chiplet Studio (Docker) with a bundled demo .chiplet.
#
# Mounts the project root at the same absolute path inside the container so the
# absolute paths in the .chiplet (LYP files, GDS files referenced from the
# bundled examples/ and the PDK roots) resolve correctly.
#
# Paths are derived from the script location:
#   scripts/        -> this script
#   ..              -> chiplet-studio dir
#   ../..           -> project root: holds the adk-tools umbrella (bundled demo
#                      under adk-tools/examples/) and the sibling PDK checkouts
#                      (interposer/, interconnect_pdk/, gds_to_kicad/) the
#                      .chiplet's ${...PDK_ROOT} refs resolve against
# Override by exporting PROJECT_ROOT and/or CHIPLET_FILE, or pass a .chiplet as $1.
#
# Requires:
#   - chiplet-studio-build Docker image (build with scripts/build-docker.sh)
#   - chiplet-studio binary at chiplet-studio/build/chiplet-studio
#   - klayout libs under chiplet-studio/extern/klayout/bin-release
#
# WHAT THIS LAUNCH IS AND IS NOT. The project root is mounted whole and
# writable, and the image is named by a mutable tag, so a run here is a
# convenience launch against the working tree, not a reproducible or isolated
# one: what it exercises is whatever those paths hold at the time. Open a FRESH
# WRITABLE COPY of a coupon rather than the archived original, because the
# studio writes its session log beside the document it opens and a demo should
# not modify an artifact of record. Nothing about this script transfers a result
# from the isolated relocation probes; those run a pinned image by digest over
# read-only mounts, and this does neither.
#
# Usage: ./scripts/run-chiplet-demo.sh [path/to/file.chiplet]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CHIPLET_STUDIO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
# Project root holds both the adk-tools umbrella (bundled demo under
# adk-tools/examples/) and the sibling PDK checkouts the .chiplet resolves
# against; mounted whole so all of them are visible in the container.
PROJECT_ROOT="${PROJECT_ROOT:-$(cd "$CHIPLET_STUDIO_DIR/.." && pwd)}"

# PDK roots -> fixed container paths (defines PDK_MOUNTS)
source "$SCRIPT_DIR/pdk-env.sh"

DEFAULT_CHIPLET="${PROJECT_ROOT}/adk-tools/examples/two_die_interposer/outputs/two_die_interposer.chiplet"
CHIPLET_FILE="${1:-${CHIPLET_FILE:-$DEFAULT_CHIPLET}}"

if [ ! -f "$CHIPLET_FILE" ]; then
    echo "Error: chiplet file not found at: $CHIPLET_FILE"
    echo "Pass one as an argument or set CHIPLET_FILE / PROJECT_ROOT."
    exit 1
fi

xhost +local:docker > /dev/null

# What PYTHONPATH below does, stated accurately rather than dramatically.
# PYTHON_MODULE_DIR is baked at compile time from CMAKE_BINARY_DIR, so the path
# the interpreter inserts into sys.path is the one the binary was BUILT in. The
# accepted binary was built at /workspace/build, which is where this launcher
# mounts the studio, so under a normal launch the baked path already resolves as
# long as the mounted tree actually holds build/python. This flag removes the
# dependence on that coincidence: the module path stops being a property of
# where the binary was compiled.
#
# A normal-launcher failure was NOT measured. What was measured is a copy
# relocated to /products, where the baked path does not exist: the same start
# without the variable reproduces "chiplet_studio module not in Python path" and
# with it the warning is gone, both reaching the marker ScriptEngine prints
# after the import site. That result does not transfer to this launch, and it
# does not say which copy the interpreter imported from either; the embedded
# interpreter is never asked for the module's __file__.

docker run --rm \
    -e DISPLAY="$DISPLAY" \
    -e LD_LIBRARY_PATH=/workspace/extern/klayout/bin-release:/workspace/extern/klayout/bin-release/db_plugins \
    -e PYTHONPATH=/workspace/build/python \
    -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
    -v "${PROJECT_ROOT}:${PROJECT_ROOT}" \
    -v "${CHIPLET_STUDIO_DIR}:/workspace" \
    "${PDK_MOUNTS[@]}" \
    --network host \
    -u $(id -u):$(id -g) \
    chiplet-studio-build \
    /workspace/build/chiplet-studio "$CHIPLET_FILE"
