#!/bin/bash
# run-local.sh - Run Chiplet Studio locally using bundled libraries
#
# This script extracts the AppImage (if needed) and runs the application
# with the bundled libraries. This avoids compatibility issues with the
# AppImage runtime while still providing all necessary dependencies.
#
# Usage: ./scripts/run-local.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
APPIMAGE="$PROJECT_DIR/dist/Chiplet_Studio-x86_64.AppImage"
EXTRACT_DIR="/tmp/chiplet-studio-extracted"

# Check if AppImage exists
if [ ! -f "$APPIMAGE" ]; then
    echo "Error: AppImage not found at $APPIMAGE"
    echo "Run ./scripts/build-appimage.sh first"
    exit 1
fi

# Extract AppImage if not already extracted or if AppImage is newer
if [ ! -d "$EXTRACT_DIR/squashfs-root" ] || [ "$APPIMAGE" -nt "$EXTRACT_DIR/squashfs-root" ]; then
    echo "Extracting AppImage..."
    rm -rf "$EXTRACT_DIR"
    mkdir -p "$EXTRACT_DIR"
    cd "$EXTRACT_DIR"
    "$APPIMAGE" --appimage-extract > /dev/null 2>&1
    cd "$PROJECT_DIR"
    echo "Extraction complete."
fi

# Run the application with bundled libraries
export LD_LIBRARY_PATH="$EXTRACT_DIR/squashfs-root/usr/lib:$LD_LIBRARY_PATH"
exec "$EXTRACT_DIR/squashfs-root/usr/bin/chiplet-studio" "$@"
