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
#   - the folder holding this checkout, or $HETERO_PROJECT (GDS files, chiplet files)
#   - ${HOME}/git/ihp_pdk (IHP SG13G2 techfiles)
#
# The mounts preserve absolute paths so .chiplet files work without modification.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Paths to mount (these contain GDS files and techfiles referenced by .chiplet
# files): the folder holding this checkout, with its sibling checkouts.
HETERO_PROJECT="${HETERO_PROJECT:-$(cd "$PROJECT_DIR/.." && pwd)}"
IHP_PDK="${HOME}/git/ihp_pdk"

# Default working directory (where .chiplet files are)
DEFAULT_WORK_DIR="$HETERO_PROJECT/adk-tools/examples/two_die_interposer/outputs"
WORK_DIR="${1:-$DEFAULT_WORK_DIR}"

# Docker image (pre-built with all dependencies)
IMAGE="chiplet-studio-build"

# KLayout library paths
KLAYOUT_LIBS="$PROJECT_DIR/extern/klayout/bin-release"

# PDK roots -> fixed container paths (defines PDK_MOUNTS)
source "$SCRIPT_DIR/pdk-env.sh"

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
    "${PDK_MOUNTS[@]}" \
    --user "$(id -u):$(id -g)" \
    --network host \
    "$IMAGE" \
    bash -c "
        export LD_LIBRARY_PATH=$KLAYOUT_LIBS:$KLAYOUT_LIBS/db_plugins
        cd '$WORK_DIR'
        $PROJECT_DIR/build/chiplet-studio
    "
