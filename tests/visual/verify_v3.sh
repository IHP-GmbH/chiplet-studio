#!/usr/bin/env bash
# Visual verification script for V3 Shape Filtering Slider
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HETERO_PROJECT="/home/montanares/git/heterogenic_chip_design_project"
KLAYOUT_LIBS="$HETERO_PROJECT/chiplet-studio/extern/klayout/bin-release"
CHIPLET_FILE="$SCRIPT_DIR/visual_render_modes.chiplet"

echo "============================================"
echo "  V3 Shape Filter - Visual Verification"
echo "============================================"
echo ""
echo "Checklist (verify each after app opens):"
echo ""
echo "  [ ] 1. Slider visible in 3D toolbar (Filter: label + slider + %)"
echo "  [ ] 2. Double-click a die -> Detailed mode, all shapes visible"
echo "  [ ] 3. Slider at 0% -> all shapes visible"
echo "  [ ] 4. Slide to 50% -> small shapes disappear"
echo "  [ ] 5. Slide to 100% -> only largest shapes remain"
echo "  [ ] 6. Return slider to 0% -> all shapes restored"
echo "  [ ] 7. Return die to Transparent -> slider has no effect"
echo "  [ ] 8. Rotation smooth with filtered geometry"
echo "  [ ] 9. Undo still works for render mode changes"
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
