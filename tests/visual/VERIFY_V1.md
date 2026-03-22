# Visual Verification: V1 Render Modes

## Prerequisites
- Docker image chiplet-studio-build exists
- X11 display available
- Run: ./tests/visual/verify_v1.sh

## Checklist
- [ ] 1. On open: dies and interposer appear semi-transparent (see-through, alpha ~0.35)
- [ ] 2. Substrate appears opaque (solid)
- [ ] 3. Components are distinguishable through transparency (no z-fighting)
- [ ] 4. Right-click a die -> context menu shows render mode submenu
- [ ] 5. Switch a die to Wireframe -> only bounding box edges visible
- [ ] 6. Switch a die to Solid -> opaque, no see-through
- [ ] 7. Switch a die to Hidden -> disappears from 3D view
- [ ] 8. Switch a die to Detailed -> full GDS layer geometry loads (box fallback if no GDS)
- [ ] 9. Hierarchy panel shows render mode per component
- [ ] 10. Undo (Ctrl+Z) after render mode change -> reverts
- [ ] 11. Camera rotation with transparent objects -> no major sorting artifacts
- [ ] 12. Cross-section (clip plane) works with transparent objects
