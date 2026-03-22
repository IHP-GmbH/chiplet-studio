#!/usr/bin/env bash
# Visual verification script for Flow S3 - Integrated flow in ChipletFormat
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HETERO_PROJECT="/home/montanares/git/heterogenic_chip_design_project"
KLAYOUT_LIBS="$HETERO_PROJECT/chiplet-studio/extern/klayout/bin-release"
CHIPLET_FILE="$HETERO_PROJECT/kicad_designs/kicad_interposer_hyperlynx_to_gds/chiplet_files/chiplet_demo.chiplet"

echo "============================================"
echo "  Flow S3 Integration - Visual Verification"
echo "============================================"
echo ""
echo "Checklist (verify each after app opens):"
echo ""
echo "  [ ] 1. FlowPanel dock loads with 4 real steps from chiplet_demo.chiplet"
echo "  [ ] 2. Step names: Generate Netlist, Generate Cu-Pillars, Convert HYP to GDS, Run DRC"
echo "  [ ] 3. All steps show 'Pending' status initially"
echo "  [ ] 4. Dependencies are correct (each step depends on the previous one)"
echo "  [ ] 5. No double-parse: flow loads via Assembly, not loadFlowFromFile"
echo "  [ ] 6. Load a .chiplet WITHOUT flow section -> FlowPanel shows empty state"
echo "  [ ] 7. File > New -> FlowPanel clears properly"
echo "  [ ] 8. Save and reload -> flow section survives round-trip"
echo "  [ ] 9. 3D view, Hierarchy, Properties panels work normally"
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
