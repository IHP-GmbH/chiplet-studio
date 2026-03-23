#!/usr/bin/env bash
# Visual verification script for B5 Net Graph Visualization
set -euo pipefail

HETERO_PROJECT="${HOME}/git/heterogenic_chip_design_project"
KLAYOUT_LIBS="$HETERO_PROJECT/chiplet-studio/extern/klayout/bin-release"
CHIPLET_FILE="$HETERO_PROJECT/kicad_designs/kicad_interposer_hyperlynx_to_gds/chiplet_files/chiplet_demo.chiplet"

echo "============================================"
echo "  B5 Net Graph - Visual Verification"
echo "============================================"
echo ""
echo "Checklist (verify each after app opens):"
echo ""
echo "  [ ] 1. Net Graph panel visible in bottom dock (tabbed with Console/Flow)"
echo "  [ ] 2. Toggle via View > Net Graph (Ctrl+G)"
echo "  [ ] 3. 4 component nodes (U1-U4) in circular layout"
echo "  [ ] 4. Nodes colored blue (all Dies)"
echo "  [ ] 5. GND: star topology, near-black edges connecting 4 nodes"
echo "  [ ] 6. VDD: red edge between U2 and U4"
echo "  [ ] 7. Signal nets: green edges"
echo "  [ ] 8. Hover over edge -> tooltip with net name + pins"
echo "  [ ] 9. Click node -> highlights in 3D + hierarchy"
echo "  [ ] 10. Click component in hierarchy -> highlights node in graph"
echo "  [ ] 11. Uncheck Power filter -> VDD edge disappears"
echo "  [ ] 12. Legend shows color key for each NetClass"
echo "  [ ] 13. Load file without netlist -> empty state, no crash"
echo "  [ ] 14. Scroll/zoom works in graph view"
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
