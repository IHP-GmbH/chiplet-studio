# OpenROAD 3Dblox flow demo

Loads a small two-die assembly whose `flow:` block runs two steps from the
Flow Pipeline panel. Today the first step refuses this file; see Status
below.

1. **Export to 3Dblox** — converts this `.chiplet` into `.3dbv`/`.3dbx`
   (plus minimal technology LEFs) with the ADK's `chiplet2dbx` exporter.
2. **OpenROAD check_3dblox** — loads the export with `read_3dbx` and runs
   the `check_3dblox` assembly linter inside an OpenROAD container. The
   step fails on any linter warning, not only on crashes.

The `.chiplet` file stays the authoritative placement source; the 3Dblox
files are derived artifacts under `build_3dblox/`, regenerated on each run.

## Status: the export step refuses this demo

`chiplet2dbx` places a `gds_origin` die from the GDS bounding box in its die
contract (`--contract D1=DIR`) and refuses a die without one. On this file it
stops with:

```
die 'D1' declares anchor gds_origin, and its outline cannot be placed
without the die's GDS bbox; give its die contract with --contract D1=DIR
(the contract's die.bbox_um)
```

D1 and D2 here are synthetic: the file gives them no `layout` and there is
no die contract for either, so **Export to 3Dblox** stops with that message
and **OpenROAD check_3dblox** has nothing to read. The demo is kept as it is,
as a case the exporter refuses. It runs again once each die has a layout and
a die contract: a directory holding `die-contract.json` (schema
`die-output-contract`) bound to that layout, whose `die.bbox_um` gives the
die's GDS bounding box. Prerequisites and Run below describe the flow for
dies that have both.

## Prerequisites

- The `adk` repository checked out as a sibling of `chiplet-studio`
  (the export step calls `../../../adk/openroad/chiplet2dbx.py`).
- Docker, plus an OpenROAD image with the 3Dblox commands. Build one from
  the upstream OpenROAD repository (any master checkout from July 2026 or
  later) with its own Dockerfile:

  ```bash
  git clone --recurse-submodules https://github.com/The-OpenROAD-Project/OpenROAD.git
  cd OpenROAD
  docker build -t openroad-3dblox:$(git rev-parse --short=8 HEAD) .
  ```

  Point the check step at your tag via the `OPENROAD_3DBLOX_IMAGE`
  environment variable (default: `openroad-3dblox:ad9e7248`).

## Run

This section runs only once each die has a `layout:` in the file and a die
contract (see Status); with the file as shipped, the export step fails.

Open `openroad_3dblox_demo.chiplet` in Chiplet Studio; the Flow Pipeline
dock appears automatically. Click **Run All**. Both steps also run
headlessly from this directory:

```bash
python3 ../../../adk/openroad/chiplet2dbx.py \
    --chiplet openroad_3dblox_demo.chiplet --out-dir build_3dblox \
    --contract D1=<D1 contract dir> --contract D2=<D2 contract dir>
./run_check_3dblox.sh build_3dblox openroad_3dblox_demo
```

A clean run prints `READ_3DBX_OK` / `CHECK_3DBLOX_OK` and no warnings. To
see the pipeline catch a real problem, edit a die's `position.z` by one
micron and rerun: the export step itself turns red (the exporter verifies
`z == mount surface + stack height` exactly and refuses to emit
inconsistent geometry). Hand-editing a `z:` in the generated `.3dbx`
instead shows the same rule enforced on the OpenROAD side: `check_3dblox`
reports an invalid connection and the check step turns red.
