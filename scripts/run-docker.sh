#!/bin/bash
# run-docker.sh - Run Chiplet Studio from Docker container
#
# Usage: ./scripts/run-docker.sh [project_dir]
# Arguments:
#   project_dir   Directory containing .chiplet files (default: current dir)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Project files directory (for .chiplet files)
WORK_DIR="${1:-$(pwd)}"

cd "$PROJECT_DIR"

# Check if built
if [ ! -f "build/chiplet-studio" ]; then
    echo "Error: chiplet-studio not built. Run ./scripts/build-docker.sh first."
    exit 1
fi

# Allow X11 connections from Docker
xhost +local:docker 2>/dev/null || true

echo "Starting Chiplet Studio..."
docker run --rm -it \
    -e DISPLAY="$DISPLAY" \
    -e QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}" \
    -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
    -v "$PROJECT_DIR/build:/app:ro" \
    -v "$PROJECT_DIR/extern/klayout/bin-release:/app/lib:ro" \
    -v "$WORK_DIR:/project" \
    --user $(id -u):$(id -g) \
    --network host \
    ubuntu:22.04 \
    bash -c "
        export LD_LIBRARY_PATH=/app/lib
        cd /project
        /app/chiplet-studio
    "
