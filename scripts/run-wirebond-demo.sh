#!/bin/bash
# run-wirebond-demo.sh - Launch Chiplet Studio (Docker) with the interposer
# wire-bonding demo project preloaded.
#
# Mounts the project root at the same absolute path inside the container so the
# absolute paths in the .chiplet (LYP files, GDS files referenced from
# kicad_designs/, gds_to_kicad/, interposer/) resolve correctly.
#
# Paths are derived from the script location:
#   scripts/        -> this script
#   ..              -> chiplet-studio dir
#   ../..           -> project root (heterogenic_chip_design_project)
# Override by exporting PROJECT_ROOT and/or CHIPLET_FILE, or pass a .chiplet as $1.
#
# Requires:
#   - chiplet-studio-build Docker image (build with scripts/build-docker.sh)
#   - chiplet-studio binary at chiplet-studio/build/chiplet-studio
#   - klayout libs under chiplet-studio/extern/klayout/bin-release
#
# Usage: ./scripts/run-wirebond-demo.sh [path/to/file.chiplet]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CHIPLET_STUDIO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PROJECT_ROOT="${PROJECT_ROOT:-$(cd "$CHIPLET_STUDIO_DIR/.." && pwd)}"

# PDK roots -> fixed container paths (defines PDK_MOUNTS)
source "$SCRIPT_DIR/pdk-env.sh"

DEFAULT_CHIPLET="${PROJECT_ROOT}/kicad_designs/interposer_wire_bonding_demo/interposer_wire_bonding_demo.chiplet"
CHIPLET_FILE="${1:-${CHIPLET_FILE:-$DEFAULT_CHIPLET}}"

if [ ! -f "$CHIPLET_FILE" ]; then
    echo "Error: chiplet file not found at: $CHIPLET_FILE"
    echo "Pass one as an argument or set CHIPLET_FILE / PROJECT_ROOT."
    exit 1
fi

xhost +local:docker > /dev/null

docker run --rm \
    -e DISPLAY="$DISPLAY" \
    -e LD_LIBRARY_PATH=/workspace/extern/klayout/bin-release:/workspace/extern/klayout/bin-release/db_plugins \
    -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
    -v "${PROJECT_ROOT}:${PROJECT_ROOT}" \
    -v "${CHIPLET_STUDIO_DIR}:/workspace" \
    "${PDK_MOUNTS[@]}" \
    --network host \
    -u $(id -u):$(id -g) \
    chiplet-studio-build \
    /workspace/build/chiplet-studio "$CHIPLET_FILE"
