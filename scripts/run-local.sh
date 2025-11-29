#!/bin/bash
# run-local.sh - Run Chiplet Studio locally
#
# Requirements: Qt6, OpenGL, yaml-cpp installed locally
#
# Usage: ./scripts/run-local.sh [project_dir]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Project files directory
WORK_DIR="${1:-$(pwd)}"

# Check if built
if [ ! -f "$PROJECT_DIR/build/chiplet-studio" ]; then
    echo "Error: chiplet-studio not built."
    echo "Run ./scripts/build-docker.sh or build manually."
    exit 1
fi

# Set library path for KLayout libraries
export LD_LIBRARY_PATH="$PROJECT_DIR/extern/klayout/bin-release:$LD_LIBRARY_PATH"

cd "$WORK_DIR"
exec "$PROJECT_DIR/build/chiplet-studio" "$@"
