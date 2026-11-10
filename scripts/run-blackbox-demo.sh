#!/bin/bash
# run-blackbox-demo.sh - Launch Chiplet Studio (Docker) with the black-box demo preloaded.
#
# Opens examples/blackbox/blackbox_demo.chiplet: a chiplet whose technology has
# NO .lyp, so it exercises the black-box render path -- default-layer pads + pad
# names in the 2D drill-down, and augmented-stackup pad slabs in 3D. The GDS is
# synthesized by gds_to_kicad/blackbox_chiplet.py from acme_phy.spec.json (pads
# on 205/0, names on 205/25, outline on 206/0).
#
# Mounts the project root (the directory holding this checkout) at the same
# absolute path inside the container so the .chiplet's relative GDS path
# resolves, and mounts this checkout at /workspace so the binary finds its
# KLayout libs (matches the build RUNPATH).
#
# Requires:
#   - chiplet-studio-build Docker image (build with scripts/build-docker.sh)
#   - chiplet-studio binary at chiplet-studio/build/chiplet-studio
#
# This entry point takes NO argument and honours NO PROJECT_ROOT override: it
# opens the bundled document of the checkout it sits in, and both paths come
# from this script's own location, never from HOME. So it is not the way to
# open a fresh writable copy of anything; use run-chiplet-demo.sh with an
# explicit path or PROJECT_ROOT for that.
#
# Usage: ./scripts/run-blackbox-demo.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CHIPLET_STUDIO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PROJECT_ROOT="$(cd "$CHIPLET_STUDIO_DIR/.." && pwd)"
CHIPLET_FILE="${CHIPLET_STUDIO_DIR}/examples/blackbox/blackbox_demo.chiplet"

# PDK roots -> fixed container paths (defines PDK_MOUNTS)
source "$SCRIPT_DIR/pdk-env.sh"

if [ ! -f "$CHIPLET_FILE" ]; then
    echo "Error: blackbox_demo.chiplet not found at: $CHIPLET_FILE"
    exit 1
fi

xhost +local:docker > /dev/null

# Same reason as the canonical launcher, and stated the same way: the baked
# PYTHON_MODULE_DIR is the compile-time build directory, which for the accepted
# binary is /workspace/build, the very place this demo mounts the studio. So the
# baked path already resolves here when the mounted tree holds build/python;
# this flag removes the dependence on where the binary happened to be compiled.
# No failure of this launcher was measured. The environment line and the path
# derivation are shared with the canonical launcher; the fixture and the
# missing PROJECT_ROOT override are this script's own.

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
