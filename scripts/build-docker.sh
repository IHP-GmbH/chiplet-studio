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
PARALLEL=8

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

        # Build KLayout. This is not optional: without it the 2D view is a stub
        # and the geometry path is untested, while the build still succeeds and
        # reads as green. A KLayout failure has to stop the build here, where
        # the log that explains it is still on screen.
        # KLayout installs libraries to bin-release
        KLAYOUT_DIR=/workspace/extern/klayout/bin-release
        if [ -f \"\$KLAYOUT_DIR/libklayout_db.so\" ]; then
            echo 'KLayout libraries already built'
        else
            echo 'Building KLayout libraries...'
            cd /workspace/extern/klayout
            ./build.sh -j$PARALLEL -without-qtbinding 2>&1
            if [ ! -f \"\$KLAYOUT_DIR/libklayout_db.so\" ]; then
                echo 'KLayout build produced no libklayout_db.so' >&2
                exit 1
            fi
            echo 'KLayout libraries built and installed successfully'
        fi

        echo 'Building Chiplet Studio...'
        cd /workspace
        mkdir -p build && cd build
        cmake .. -DKLAYOUT_BUILD_DIR=\$KLAYOUT_DIR -DCHIPLET_REQUIRE_KLAYOUT=ON
        make -j$PARALLEL
    "

echo "Build complete. Binary at: build/chiplet-studio"
