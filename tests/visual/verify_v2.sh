#!/usr/bin/env bash
# Visual verification script for V2 Assembly 2D View
set -euo pipefail

HETERO_PROJECT="${HOME}/git/heterogenic_chip_design_project"
KLAYOUT_LIBS="$HETERO_PROJECT/chiplet-studio/extern/klayout/bin-release"
CHIPLET_FILE="$HETERO_PROJECT/adk-tools/examples/interposer_wire_bonding_demo/outputs/interposer_wire_bonding_demo.chiplet"

echo "============================================"
echo "  V2 Assembly 2D View - Visual Verification"
echo "============================================"
echo ""
echo "Checklist (verify each after app opens):"
echo ""
echo "  [ ] 1. On open: 2D panel shows full assembly GDS"
echo "  [ ] 2. 3D view shows transparent assembly"
echo "  [ ] 3. Click chiplet cell in 2D hierarchy -> highlights in 3D"
echo "  [ ] 4. Click component in 3D -> 2D navigates to wrapper cell"
echo "  [ ] 5. Double-click in 3D -> 2D drills into component GDS"
echo "  [ ] 6. Component switches to Detailed mode on drill-down"
echo "  [ ] 7. Back button -> returns to assembly GDS"
echo "  [ ] 8. Component restores previous render mode"
echo "  [ ] 9. Layer toggles work in both modes"
echo "  [ ] 10. .chiplet without assembly GDS -> no crash"
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
    -v "${HOME}/git/ihp_pdk:${HOME}/git/ihp_pdk:ro" \
    --user "$(id -u):$(id -g)" \
    --network host \
    chiplet-studio-build bash -c \
    "export LD_LIBRARY_PATH=$KLAYOUT_LIBS:$KLAYOUT_LIBS/db_plugins && \
     $HETERO_PROJECT/chiplet-studio/build/chiplet-studio \
     $CHIPLET_FILE"
