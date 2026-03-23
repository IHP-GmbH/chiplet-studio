# Visual Verification: V3 Shape Filtering Slider

## Prerequisites
- Docker image chiplet-studio-build exists
- X11 display available
- Run: ./tests/visual/verify_v3.sh

## Checklist
- [ ] 1. Slider visible in 3D toolbar labeled "Filter:" with percentage display
- [ ] 2. Double-click a die -> Detailed mode, all shapes visible
- [ ] 3. Slider at 0% -> all shapes visible (no filtering)
- [ ] 4. Slide to 50% -> small shapes disappear progressively
- [ ] 5. Slide to 100% -> only largest shapes remain
- [ ] 6. Return slider to 0% -> all shapes restored
- [ ] 7. Return die to Transparent mode -> slider has no visual effect
- [ ] 8. Rotation smooth with filtered geometry (no freezing during slider drag)
- [ ] 9. Undo still works for render mode changes after filtering
