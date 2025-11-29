#!/bin/bash
# build-docker.sh - Build Chiplet Studio inside Docker container
#
# Usage: ./scripts/build-docker.sh [options]
# Options:
#   --clean     Clean build (remove build directories first)
#   --parallel  Number of parallel jobs (default: auto)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

CLEAN=0
PARALLEL=$(nproc)

while [[ $# -gt 0 ]]; do
    case $1 in
        --clean)
            CLEAN=1
            shift
            ;;
        --parallel)
            PARALLEL="$2"
            shift 2
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

cd "$PROJECT_DIR"

# Clean if requested
if [ "$CLEAN" -eq 1 ]; then
    echo "Cleaning build directories..."
    rm -rf build extern/klayout/build-release extern/klayout/bin-release
fi

# Build Docker image
echo "Building Docker image..."
docker build -t chiplet-studio-build \
    --build-arg UID=$(id -u) \
    --build-arg GID=$(id -g) \
    -f docker/Dockerfile.build .

# Run build inside container
echo "Building project (parallel jobs: $PARALLEL)..."
docker run --rm \
    -v "$PROJECT_DIR:/workspace" \
    -u $(id -u):$(id -g) \
    chiplet-studio-build \
    bash -c "
        set -e

        # Try to build KLayout libraries (optional)
        KLAYOUT_DIR=/workspace/extern/klayout/bin-release
        if [ -f \"\$KLAYOUT_DIR/libklayout_db.so\" ]; then
            echo 'KLayout libraries already built'
        else
            echo 'Attempting to build KLayout libraries...'
            cd /workspace/extern/klayout
            if ./build.sh -j$PARALLEL -without-qtbinding 2>&1; then
                echo 'KLayout build successful'
            else
                echo 'KLayout build failed (Qt not available?) - continuing without KLayout'
                KLAYOUT_DIR=''
            fi
        fi

        echo 'Building Chiplet Studio...'
        cd /workspace
        mkdir -p build && cd build
        if [ -n \"\$KLAYOUT_DIR\" ]; then
            cmake .. -DKLAYOUT_BUILD_DIR=\$KLAYOUT_DIR
        else
            cmake ..
        fi
        make -j$PARALLEL
    "

echo "Build complete. Binary at: build/chiplet-studio"
