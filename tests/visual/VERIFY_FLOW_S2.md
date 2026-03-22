# Visual Verification: Flow Pipeline Panel (Session 2)

## Prerequisites
- Docker image chiplet-studio-build exists
- X11 display available
- Run: ./tests/visual/verify_flow_s2.sh

## Checklist
- [ ] 1. FlowPanel dock appears at bottom area (tabbed with Python Console)
- [ ] 2. View menu has "Flow Pipeline" toggle (Ctrl+F works)
- [ ] 3. Step table shows all 4 steps with names and "Pending" status (gray)
- [ ] 4. Click a step row -> log area is empty
- [ ] 5. Click "Run" on Check Environment -> status goes Running (blue) then Success (green), time shown
- [ ] 6. Click that step row -> log area shows "Environment OK"
- [ ] 7. Click "Run All" -> steps execute in dependency order, statuses update live
- [ ] 8. Intentional Failure step shows Error (red)
- [ ] 9. While running: Run buttons and Run All are disabled
- [ ] 10. Existing panels (Hierarchy, Properties, 3D view) are unaffected
- [ ] 11. Load a .chiplet WITHOUT flow section -> FlowPanel shows "No flow defined"

## Launch command

```bash
xhost +local:docker 2>/dev/null
HETERO_PROJECT="${HOME}/git/heterogenic_chip_design_project"
KLAYOUT_LIBS="$HETERO_PROJECT/chiplet-studio/extern/klayout/bin-release"
docker run --rm \
    -e DISPLAY=$DISPLAY -e QT_QPA_PLATFORM=xcb -e CHIPLET_NO_PYTHON=1 \
    -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
    -v "$HETERO_PROJECT:$HETERO_PROJECT:ro" \
    --user "$(id -u):$(id -g)" --network host \
    chiplet-studio-build bash -c \
    "export LD_LIBRARY_PATH=$KLAYOUT_LIBS:$KLAYOUT_LIBS/db_plugins && \
     $HETERO_PROJECT/chiplet-studio/build/chiplet-studio \
     $HETERO_PROJECT/chiplet-studio/tests/visual/with_flow.chiplet"
```
