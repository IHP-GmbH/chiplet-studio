#!/bin/bash
# test-x11.sh - Run tests with X11 forwarding to host display
#
# Usage: ./scripts/test-x11.sh [gtest args...]
# Example: ./scripts/test-x11.sh --gtest_filter=KLayout*
#
# Requires: X11 display available on host
# Result: 222/222 tests pass (including KLayout GUI tests)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Allow X11 connections from Docker
xhost +local:docker 2>/dev/null || true

docker run --rm \
    -e DISPLAY="$DISPLAY" \
    -e LD_LIBRARY_PATH=/workspace/extern/klayout/bin-release:/workspace/extern/klayout/bin-release/db_plugins \
    -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
    -v "$PROJECT_DIR:/workspace" \
    --user "$(id -u):$(id -g)" \
    chiplet-studio-build \
    /workspace/build/tests/chiplet_tests "$@"
