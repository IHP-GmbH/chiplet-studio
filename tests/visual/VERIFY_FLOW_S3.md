# Flow S3 - Visual Verification Checklist

## Pre-conditions
- Build passes: `./scripts/build-docker.sh`
- All tests pass: `ctest --output-on-failure`

## Run
```bash
./tests/visual/verify_flow_s3.sh
```

## Checklist

### Flow Integration (chiplet_demo.chiplet)
- [ ] FlowPanel dock appears at bottom, tabbed with Python Console
- [ ] 4 steps loaded: Generate Netlist, Generate Cu-Pillars, Convert HYP to GDS, Run DRC
- [ ] All steps show "Pending" status (gray)
- [ ] Step dependencies visible (each step depends on previous)
- [ ] Click "Run" on Generate Netlist -- executes (may fail if .net missing, but should attempt)

### No Double Parse
- [ ] No YAML parse errors in console output (flow is parsed inside ChipletFormat::load)
- [ ] qDebug shows "Loaded flow pipeline with 4 steps"

### Backward Compatibility
- [ ] Load `tests/fixtures/minimal.chiplet` -- FlowPanel shows empty/no flow state
- [ ] No errors or warnings when loading files without flow section

### New File
- [ ] File > New -- FlowPanel clears (no stale steps from previous file)

### Round-trip
- [ ] Load chiplet_demo.chiplet, File > Save to new location
- [ ] Reopen saved file -- flow section present with same 4 steps

### No Regression
- [ ] Hierarchy panel shows components normally
- [ ] Properties panel works on component selection
- [ ] 3D view renders assembly
- [ ] Cross-section / clip plane controls work
