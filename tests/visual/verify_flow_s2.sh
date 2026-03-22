#!/usr/bin/env bash
# Visual verification script for Flow Pipeline Panel (Session 2)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HETERO_PROJECT="/home/montanares/git/heterogenic_chip_design_project"
KLAYOUT_LIBS="$HETERO_PROJECT/chiplet-studio/extern/klayout/bin-release"
CHIPLET_FILE="$SCRIPT_DIR/with_flow.chiplet"

echo "============================================"
echo "  Flow Pipeline Panel - Visual Verification"
echo "============================================"
echo ""
echo "Checklist (verify each after app opens):"
echo ""
echo "  [ ] 1. FlowPanel dock appears at bottom area (tabbed with Python Console)"
echo "  [ ] 2. View menu has 'Flow Pipeline' toggle (Ctrl+F works)"
echo "  [ ] 3. Step table shows all 4 steps with names and 'Pending' status (gray)"
echo "  [ ] 4. Click a step row -> log area is empty"
echo "  [ ] 5. Click 'Run' on Check Environment -> status goes Running (blue) then Success (green), time shown"
echo "  [ ] 6. Click that step row -> log area shows 'Environment OK'"
echo "  [ ] 7. Click 'Run All' -> steps execute in dependency order, statuses update live"
echo "  [ ] 8. Intentional Failure step shows Error (red)"
echo "  [ ] 9. While running: Run buttons and Run All are disabled"
echo "  [ ] 10. Existing panels (Hierarchy, Properties, 3D view) are unaffected"
echo "  [ ] 11. Load a .chiplet WITHOUT flow section -> FlowPanel shows 'No flow defined'"
echo ""
echo "Loading: $CHIPLET_FILE"
echo "============================================"
echo ""

# Allow Docker to access X11 display
xhost +local:docker 2>/dev/null || true

docker run --rm \
    -e DISPLAY="$DISPLAY" \
    -e QT_QPA_PLATFORM=xcb \
    -e CHIPLET_NO_PYTHON=1 \
    -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
    -v "$HETERO_PROJECT:$HETERO_PROJECT:ro" \
    --user "$(id -u):$(id -g)" \
    --network host \
    chiplet-studio-build bash -c \
    "export LD_LIBRARY_PATH=$KLAYOUT_LIBS:$KLAYOUT_LIBS/db_plugins && \
     $HETERO_PROJECT/chiplet-studio/build/chiplet-studio \
     $CHIPLET_FILE"
