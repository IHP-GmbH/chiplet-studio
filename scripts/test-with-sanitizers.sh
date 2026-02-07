#!/bin/bash
# test-with-sanitizers.sh - Run tests with AddressSanitizer and UndefinedBehaviorSanitizer
#
# This script builds the test suite with sanitizers enabled and runs all tests,
# catching memory errors and undefined behavior at runtime.
#
# Usage: ./scripts/test-with-sanitizers.sh [gtest args...]
# Example: ./scripts/test-with-sanitizers.sh --gtest_filter="*Load*"

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build-sanitizers"

echo "=== Chiplet Studio Sanitizer Tests ==="
echo ""

# Sanitizer environment configuration
export ASAN_OPTIONS="detect_leaks=1:halt_on_error=0:print_stacktrace=1:fast_unwind_on_malloc=0"
export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=0"
export LSAN_OPTIONS="suppressions=$PROJECT_DIR/lsan.supp"

# Use offscreen platform for Qt (headless)
export QT_QPA_PLATFORM=offscreen

echo ">>> Configuring build with sanitizers..."
cmake -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DENABLE_ASAN=ON \
    -DENABLE_UBSAN=ON \
    -DKLAYOUT_BUILD_DIR="$PROJECT_DIR/extern/klayout/bin-release"

echo ""
echo ">>> Building with sanitizers..."
cmake --build "$BUILD_DIR" -j$(nproc)

echo ""
echo ">>> Running tests with sanitizers..."
echo ""

# Set library path for KLayout
export LD_LIBRARY_PATH="$PROJECT_DIR/extern/klayout/bin-release:$PROJECT_DIR/extern/klayout/bin-release/db_plugins:$LD_LIBRARY_PATH"

# Run tests
"$BUILD_DIR/tests/chiplet_tests" "$@"

echo ""
echo "=== Sanitizer Tests Complete ==="
