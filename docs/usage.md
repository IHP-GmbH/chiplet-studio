<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 IHP GmbH -->

# Usage

## Opening an assembly

```bash
chiplet-studio                        # empty session
chiplet-studio path/to/assembly.chiplet
```

In the ADK tools image, `chiplet-studio` is on the `PATH`. Studio takes one
argument, the `.chiplet` file to open. A path that does not exist is reported
on the console and Studio starts empty. Studio exits 0 when
its window closes, also after a file failed to load, so a script that needs to
know whether a file is valid should check it before starting Studio.

Once open:

- the 3D view shows each component at its position; right-click a component in
  the Hierarchy panel and pick Render Mode > Detailed to see its layers;
- the 2D view opens the selected die's layout in KLayout;
- the Hierarchy and Properties panels list the components and their values;
- the Net Graph (View menu) shows the assembly's connectivity;
- the Flow Pipeline panel appears when the file has a `flow:` block. Its steps
  run programs the file names, so open only files you trust.

## Python console

View > Python Console (`` Ctrl+` ``) opens an interpreter with the
`chiplet_studio` module loaded. `get_current_assembly()` returns the open
assembly. `save_assembly` refuses an existing file that is not the loaded
one, and a loaded file that changed on disk since it was opened.

## Environment

| Variable | Effect |
|----------|--------|
| `CHIPLET_LOG_DIR` | where the log file goes (default: a `logs/` folder next to the opened file, else the current directory) |
| `CHIPLET_NO_LOGFILE` | when set, write no log file |
| `CHIPLET_NO_PYTHON` | when set, start without the embedded interpreter |
| `INTERCONNECT_PDK_ROOT` | the interconnect PDK checkout searched first for bump and pillar stackup fragments |
