#!/bin/bash
# run-blackbox-demo.sh - Launch Chiplet Studio (Docker) with the black-box demo preloaded.
#
# Opens examples/blackbox/blackbox_demo.chiplet: a chiplet whose technology has
# NO .lyp, so it exercises the black-box render path -- default-layer pads + pad
# names in the 2D drill-down, and augmented-stackup pad slabs in 3D. The GDS is
# synthesized by gds_to_kicad/blackbox_chiplet.py from acme_phy.spec.json (pads
# on 205/0, names on 205/25, outline on 206/0).
#
# Mounts the project root at the same absolute path inside the container so the
# .chiplet's relative GDS path resolves, and mounts chiplet-studio at /workspace
# so the binary finds its KLayout libs (matches the build RUNPATH).
#
# Requires:
#   - chiplet-studio-build Docker image (build with scripts/build-docker.sh)
#   - chiplet-studio binary at chiplet-studio/build/chiplet-studio
#
# Usage: ./scripts/run-blackbox-demo.sh

set -e

PROJECT_ROOT="${HOME}/git/heterogenic_chip_design_project"
CHIPLET_FILE="${PROJECT_ROOT}/chiplet-studio/examples/blackbox/blackbox_demo.chiplet"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# PDK roots -> fixed container paths (defines PDK_MOUNTS)
source "$SCRIPT_DIR/pdk-env.sh"

if [ ! -f "$CHIPLET_FILE" ]; then
    echo "Error: blackbox_demo.chiplet not found at: $CHIPLET_FILE"
    exit 1
fi

xhost +local:docker > /dev/null

docker run --rm \
    -e DISPLAY="$DISPLAY" \
    -e LD_LIBRARY_PATH=/workspace/extern/klayout/bin-release:/workspace/extern/klayout/bin-release/db_plugins \
    -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
    -v "${PROJECT_ROOT}:${PROJECT_ROOT}" \
    -v "${PROJECT_ROOT}/chiplet-studio:/workspace" \
    "${PDK_MOUNTS[@]}" \
    --network host \
    -u $(id -u):$(id -g) \
    chiplet-studio-build \
    /workspace/build/chiplet-studio "$CHIPLET_FILE"
