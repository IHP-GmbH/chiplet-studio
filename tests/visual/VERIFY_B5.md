# B5 Net Graph Visualization - Visual Verification

Run: `tests/visual/verify_b5.sh`

## Checklist

- [ ] 1. Net Graph panel visible in bottom dock (tabbed with Console/Flow)
- [ ] 2. Toggle via View > Net Graph (Ctrl+G)
- [ ] 3. 4 component nodes (U1-U4) in circular layout
- [ ] 4. Nodes colored blue (all Dies)
- [ ] 5. GND: star topology, near-black edges connecting 4 nodes
- [ ] 6. VDD: red edge between U2 and U4
- [ ] 7. Signal nets: green edges
- [ ] 8. Hover over edge shows tooltip with net name and pin details
- [ ] 9. Click node in graph -> highlights component in 3D view and hierarchy
- [ ] 10. Click component in hierarchy -> highlights node in graph
- [ ] 11. Uncheck "Power" filter -> VDD edge disappears; re-check -> reappears
- [ ] 12. Legend in graph shows color key for each NetClass
- [ ] 13. File > New (no netlist) -> empty state message, no crash
- [ ] 14. Scroll and zoom work in graph view (scroll wheel, drag)
