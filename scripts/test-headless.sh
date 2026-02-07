#!/bin/bash
# test-headless.sh - Run tests in headless (CI/Docker) environments
#
# Uses Qt's offscreen platform for headless rendering.
# KLayout GUI tests that require physical display will skip gracefully.
#
# Usage: ./scripts/test-headless.sh [gtest args...]
# Example: ./scripts/test-headless.sh --gtest_filter=KLayout*

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

TEST_BINARY="$PROJECT_DIR/build/tests/chiplet_tests"

if [ ! -f "$TEST_BINARY" ]; then
    echo "Error: Test binary not found at $TEST_BINARY"
    echo "Run ./scripts/build-docker.sh first"
    exit 1
fi

# Run tests with Qt offscreen platform (headless rendering)
# Note: KLayout GUI tests require physical display and will skip
export QT_QPA_PLATFORM=offscreen
exec "$TEST_BINARY" "$@"
