#!/bin/bash
# test-visualization.sh - Run visualization and loading tests
#
# Usage:
#   ./scripts/test-visualization.sh              # Run all visualization tests
#   ./scripts/test-visualization.sh --filter=X   # Run tests matching filter X
#   ./scripts/test-visualization.sh --verbose    # Show verbose output
#
# Environment:
#   Tests run with QT_QPA_PLATFORM=offscreen for headless mode
#   Tests that require display will be skipped automatically

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"
TEST_BINARY="$BUILD_DIR/tests/chiplet_tests"

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Parse arguments
FILTER=""
VERBOSE=""
for arg in "$@"; do
    case $arg in
        --filter=*)
            FILTER="${arg#*=}"
            ;;
        --verbose)
            VERBOSE="--gtest_print_time=1"
            ;;
        --help)
            echo "Usage: $0 [--filter=PATTERN] [--verbose]"
            echo ""
            echo "Options:"
            echo "  --filter=PATTERN   Run only tests matching PATTERN"
            echo "  --verbose          Show verbose test output"
            echo ""
            echo "Example patterns:"
            echo "  --filter=Visualization*    Run all visualization tests"
            echo "  --filter=*Load*            Run tests with 'Load' in name"
            echo "  --filter=KLayout2DView*    Run KLayout 2D view tests"
            exit 0
            ;;
    esac
done

# Check if test binary exists
if [ ! -f "$TEST_BINARY" ]; then
    echo -e "${RED}Error: Test binary not found at $TEST_BINARY${NC}"
    echo "Build the project first with: ./scripts/build-docker.sh"
    exit 1
fi

echo -e "${GREEN}=== Chiplet Studio Visualization Tests ===${NC}"
echo ""

# Set environment for headless testing
export QT_QPA_PLATFORM=offscreen
export QT_LOGGING_RULES="qt.qpa.*=false"

# Build gtest filter
GTEST_FILTER="*Visualization*:*2DView*:*ChipletFormat*"
if [ -n "$FILTER" ]; then
    GTEST_FILTER="$FILTER"
fi

echo "Environment:"
echo "  QT_QPA_PLATFORM: $QT_QPA_PLATFORM"
echo "  Test binary: $TEST_BINARY"
echo "  Filter: $GTEST_FILTER"
echo ""

# Run tests
echo -e "${YELLOW}Running tests...${NC}"
echo ""

"$TEST_BINARY" --gtest_filter="$GTEST_FILTER" $VERBOSE

EXIT_CODE=$?

echo ""
if [ $EXIT_CODE -eq 0 ]; then
    echo -e "${GREEN}All visualization tests passed!${NC}"
else
    echo -e "${RED}Some tests failed (exit code: $EXIT_CODE)${NC}"
fi

exit $EXIT_CODE
