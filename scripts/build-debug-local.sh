#!/bin/bash
# build-debug-local.sh - Build debug version locally using AppImage libraries
#
# This script builds chiplet-studio with debug symbols and sanitizers,
# using the bundled libraries from the AppImage for dependencies.
#
# Usage: ./scripts/build-debug-local.sh [--asan] [--ubsan]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
APPIMAGE="$PROJECT_DIR/dist/Chiplet_Studio-x86_64.AppImage"
EXTRACT_DIR="/tmp/chiplet-debug"
BUILD_DIR="$PROJECT_DIR/build-debug"

# Parse arguments
ENABLE_ASAN="OFF"
ENABLE_UBSAN="OFF"

for arg in "$@"; do
    case $arg in
        --asan)
            ENABLE_ASAN="ON"
            shift
            ;;
        --ubsan)
            ENABLE_UBSAN="ON"
            shift
            ;;
        *)
            ;;
    esac
done

echo "=== Chiplet Studio Local Debug Build ==="
echo ""
echo "Options:"
echo "  ASan: $ENABLE_ASAN"
echo "  UBSan: $ENABLE_UBSAN"
echo ""

# Check if AppImage exists and extract if needed
if [ -f "$APPIMAGE" ]; then
    if [ ! -d "$EXTRACT_DIR/squashfs-root" ]; then
        echo ">>> Extracting AppImage for libraries..."
        mkdir -p "$EXTRACT_DIR"
        cd "$EXTRACT_DIR"
        "$APPIMAGE" --appimage-extract > /dev/null 2>&1
        cd "$PROJECT_DIR"
        echo "    Extracted to $EXTRACT_DIR/squashfs-root"
    fi

    # Set library path from extracted AppImage
    export LD_LIBRARY_PATH="$EXTRACT_DIR/squashfs-root/usr/lib:$LD_LIBRARY_PATH"
    echo ">>> Using libraries from extracted AppImage"
else
    echo ">>> No AppImage found, using system libraries"
fi

# Configure with debug settings
echo ""
echo ">>> Configuring debug build..."
cmake -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DENABLE_ASAN=$ENABLE_ASAN \
    -DENABLE_UBSAN=$ENABLE_UBSAN \
    -DKLAYOUT_BUILD_DIR="$PROJECT_DIR/extern/klayout/bin-release"

# Build
echo ""
echo ">>> Building..."
cmake --build "$BUILD_DIR" -j$(nproc)

echo ""
echo "=== Build Complete ==="
echo ""
echo "Debug binary: $BUILD_DIR/chiplet-studio"
echo ""
echo "Run with GDB:"
echo "  LD_LIBRARY_PATH=$EXTRACT_DIR/squashfs-root/usr/lib:\$LD_LIBRARY_PATH \\"
echo "  gdb $BUILD_DIR/chiplet-studio"
echo ""

if [ "$ENABLE_ASAN" = "ON" ]; then
    echo "ASan environment variables:"
    echo "  export ASAN_OPTIONS=detect_leaks=1:halt_on_error=0:print_stacktrace=1"
    echo ""
fi

if [ "$ENABLE_UBSAN" = "ON" ]; then
    echo "UBSan environment variables:"
    echo "  export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0"
    echo ""
fi
