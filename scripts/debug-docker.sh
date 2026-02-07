#!/bin/bash
# debug-docker.sh - Run GDB inside Docker container for debugging
#
# This script launches an interactive Docker container with GDB attached
# to the chiplet-studio binary for debugging crashes.
#
# Usage: ./scripts/debug-docker.sh [gdb args...]
# Example: ./scripts/debug-docker.sh -ex "break ComponentMesh::upload"

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Allow X11 connections from Docker
xhost +local:docker 2>/dev/null || true

echo "=== Chiplet Studio Docker Debugger ==="
echo ""
echo "Launching GDB in Docker container..."
echo "Project dir: $PROJECT_DIR"
echo ""

# Run interactive Docker container with debugging capabilities
docker run -it --rm \
    --cap-add=SYS_PTRACE \
    --security-opt seccomp=unconfined \
    -e DISPLAY="$DISPLAY" \
    -e LD_LIBRARY_PATH=/workspace/extern/klayout/bin-release:/workspace/extern/klayout/bin-release/db_plugins \
    -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
    -v "$PROJECT_DIR:/workspace" \
    --user "$(id -u):$(id -g)" \
    chiplet-studio-build \
    bash -c "cd /workspace && gdb $* ./build/chiplet-studio"
