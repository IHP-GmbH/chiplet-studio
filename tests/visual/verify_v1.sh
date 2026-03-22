#!/usr/bin/env bash
# Visual verification script for V1 Render Modes
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HETERO_PROJECT="/home/montanares/git/heterogenic_chip_design_project"
KLAYOUT_LIBS="$HETERO_PROJECT/chiplet-studio/extern/klayout/bin-release"
CHIPLET_FILE="$SCRIPT_DIR/visual_render_modes.chiplet"

echo "============================================"
echo "  V1 Render Modes - Visual Verification"
echo "============================================"
echo ""
echo "Checklist (verify each after app opens):"
echo ""
echo "  [ ] 1. Dies + interposer are semi-transparent"
echo "  [ ] 2. Substrate is opaque (solid)"
echo "  [ ] 3. No z-fighting between layers"
echo "  [ ] 4. Right-click die -> Render Mode submenu"
echo "  [ ] 5. Wireframe mode -> box edges only"
echo "  [ ] 6. Solid mode -> opaque"
echo "  [ ] 7. Hidden mode -> component disappears"
echo "  [ ] 8. Detailed mode -> GDS geometry (or box fallback)"
echo "  [ ] 9. Hierarchy panel shows Mode column"
echo "  [ ] 10. Ctrl+Z undoes render mode change"
echo "  [ ] 11. Camera rotation -> no major transparency artifacts"
echo "  [ ] 12. Clip plane works with transparency"
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
