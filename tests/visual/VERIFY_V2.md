# Visual Verification: V2 Assembly 2D View

## Prerequisites
- Docker image chiplet-studio-build exists
- X11 display available
- Run: ./tests/visual/verify_v2.sh

## Checklist
- [ ] 1. On open: 2D panel shows full assembly GDS (interposer layout visible)
- [ ] 2. 3D view shows transparent assembly (V1 render modes working)
- [ ] 3. Click a chiplet cell in 2D hierarchy -> corresponding component highlights in 3D
- [ ] 4. Click a component in 3D -> 2D pans/zooms to that component's wrapper cell
- [ ] 5. Double-click component in 3D -> 2D switches to component's individual GDS (drill-down)
- [ ] 6. Component switches to Detailed mode in 3D on drill-down
- [ ] 7. Back button in 2D -> returns to assembly GDS
- [ ] 8. Component returns to previous render mode after back
- [ ] 9. Layer toggles work in both assembly and drill-down mode
- [ ] 10. Loading .chiplet without assembly GDS -> 2D shows empty/message, no crash

## Notes
- The assembly GDS is auto-detected from the interposer layout path
  (chiplet_demo_interposer.gds -> chiplet_demo_complete.gds)
- Wrapper cell naming: U1_*, U2_*, U3_*, U4_* match component IDs
- If chiplet_demo_complete.gds doesn't exist, run hyp_to_gds with --with-chiplets

## Launch command

```bash
xhost +local:docker 2>/dev/null
HETERO_PROJECT="${HOME}/git/heterogenic_chip_design_project"
KLAYOUT_LIBS="$HETERO_PROJECT/chiplet-studio/extern/klayout/bin-release"
docker run --rm \
    -e DISPLAY=$DISPLAY -e QT_QPA_PLATFORM=xcb -e CHIPLET_NO_PYTHON=1 \
    -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
    -v "$HETERO_PROJECT:$HETERO_PROJECT:ro" \
    -v "${HOME}/git/ihp_pdk:${HOME}/git/ihp_pdk:ro" \
    --user "$(id -u):$(id -g)" --network host \
    chiplet-studio-build bash -c \
    "export LD_LIBRARY_PATH=$KLAYOUT_LIBS:$KLAYOUT_LIBS/db_plugins && \
     $HETERO_PROJECT/chiplet-studio/build/chiplet-studio \
     $HETERO_PROJECT/adk-tools/examples/interposer_wire_bonding_demo/outputs/interposer_wire_bonding_demo.chiplet"
```
