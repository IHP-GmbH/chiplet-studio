#!/bin/bash
# run-docker.sh - Run Chiplet Studio from Docker container
#
# Usage: ./scripts/run-docker.sh [work_dir]
#
# Arguments:
#   work_dir   Directory to open (default: chiplet_files)
#
# Environment:
#   CHIPLET_NO_PYTHON=1   Disable Python scripting (if crashes)
#
# This script mounts:
#   - ${HOME}/git/heterogenic_chip_design_project (GDS files, chiplet files)
#   - ${HOME}/git/ihp_pdk (IHP SG13G2 techfiles)
#
# The mounts preserve absolute paths so .chiplet files work without modification.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Default working directory (where .chiplet files are)
DEFAULT_WORK_DIR="${HOME}/git/heterogenic_chip_design_project/kicad_designs/kicad_interposer_hyperlynx_to_gds/chiplet_files"
WORK_DIR="${1:-$DEFAULT_WORK_DIR}"

# Docker image (pre-built with all dependencies)
IMAGE="chiplet-studio-build"

# Paths to mount (these contain GDS files and techfiles referenced by .chiplet files)
HETERO_PROJECT="${HOME}/git/heterogenic_chip_design_project"
IHP_PDK="${HOME}/git/ihp_pdk"

# KLayout library paths
KLAYOUT_LIBS="$HETERO_PROJECT/chiplet-studio/extern/klayout/bin-release"

cd "$PROJECT_DIR"

# Check if built
if [ ! -f "build/chiplet-studio" ]; then
    echo "Error: chiplet-studio not built. Run ./scripts/build-docker.sh first."
    exit 1
fi

# Check if image exists
if ! docker image inspect "$IMAGE" &>/dev/null; then
    echo "Docker image '$IMAGE' not found. Building..."
    "$SCRIPT_DIR/build-docker.sh"
fi

# Allow X11 connections from Docker
xhost +local:docker 2>/dev/null || true

echo "============================================"
echo "  Chiplet Studio"
echo "============================================"
echo "  Working dir: $WORK_DIR"
echo "  Image: $IMAGE"
echo ""
echo "  Mounts:"
echo "    - $HETERO_PROJECT (GDS, chiplet files)"
echo "    - $IHP_PDK (IHP techfiles)"
echo ""
echo "  Press Ctrl+C to stop"
echo "============================================"
echo ""

# Build environment variables
ENV_VARS="-e DISPLAY=$DISPLAY -e QT_QPA_PLATFORM=xcb"
if [ -n "$CHIPLET_NO_PYTHON" ]; then
    ENV_VARS="$ENV_VARS -e CHIPLET_NO_PYTHON=1"
fi

# Run the container
docker run --rm \
    $ENV_VARS \
    -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
    -v "$HETERO_PROJECT:$HETERO_PROJECT:ro" \
    -v "$IHP_PDK:$IHP_PDK:ro" \
    --user "$(id -u):$(id -g)" \
    --network host \
    "$IMAGE" \
    bash -c "
        export LD_LIBRARY_PATH=$KLAYOUT_LIBS:$KLAYOUT_LIBS/db_plugins
        cd '$WORK_DIR'
        $HETERO_PROJECT/chiplet-studio/build/chiplet-studio
    "
