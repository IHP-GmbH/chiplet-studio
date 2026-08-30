<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- SPDX-FileCopyrightText: 2026 IHP GmbH -->

# Vendored: chiplet_format_io (Apache-2.0)

Chiplet Studio is GPL-3.0-or-later (it links KLayout). To keep `.chiplet`
parsing in a permissively licensed layer that adk-tools and other downstream
tools can reuse, the format reader/writer lives in this vendored copy of the
C++ reference library rather than in a hand-written parser inside the GPL host.

This directory is a **verbatim copy** of the C++ reference reader/writer for the
`.chiplet` format:

- **Upstream:** `IHP-GmbH/chiplet-spec`, `reference/cpp/`
- **Commit:** `e94e77420468b18f7c56bfae37a1bceeffd56abb`
- **License:** Apache-2.0 (see the SPDX headers in each file)

## What is vendored

Only the two source files are copied; the upstream `CMakeLists.txt`, `README.md`
and `tests/` are deliberately left behind (the host has its own build glue and
its own contract tests):

- `include/chiplet_format_io/chiplet_format_io.hpp` (public header, std-only)
- `src/chiplet_format_io.cpp` (implementation; yaml-cpp confined here)

The `include/chiplet_format_io/` nesting is what makes the public include path
`<chiplet_format_io/chiplet_format_io.hpp>` work. The host wires this up in the
top-level `CMakeLists.txt`: it adds `src/formats/chiplet_format_io/include` to
the include path and compiles `src/formats/chiplet_format_io/src/chiplet_format_io.cpp`
directly into the studio target.

## How the host uses it

`ChipletFormat::load` delegates `.chiplet` parsing and validation to this
library, then maps its plain-struct `ChipletDocument` into Chiplet Studio's
`core/Assembly`. The library is dependency-clean (yaml-cpp only; no Qt, no
KLayout), which is exactly why parsing the *format* lives here in the permissive
layer, while the GPL host keeps the studio-specific concerns: path resolution,
diagnostic warnings, z auto-calculation, and flow handoff.

## Re-sync procedure

**Do not edit these files here.** Fix bugs upstream in `chiplet-spec`, then
re-vendor: copy *only* the two files above from the upstream `reference/cpp/`
tree and bump the commit hash in this note. Do not drag in the upstream
`CMakeLists.txt` or `tests/`; the studio supplies its own build and contract
tests.

The copy is byte-identical to upstream and is meant to stay that way, so the
check is a plain `diff` against a `chiplet-spec` checkout at the commit above.
There are no local patches and no cherry-picks to re-fold.

Vendoring an Apache-2.0 library into this GPL-3.0-or-later project is
license-compatible; the copied files keep their own Apache-2.0 SPDX headers.

## History: why this copy had drifted

For a while this note described a deferred full re-sync, a `attachment_surface_z`
cherry-pick applied surgically on top of `1a56f61c`, and a handoff of the full
re-vendor to another team. All of it rested on the claim that chiplet-spec HEAD
had *removed* `TechDef.stackup`, which the host still reads
(`ChipletFormat.cpp`, `Technology::stackup_path()`).

That claim was wrong. `stackup` was never in chiplet-spec, at any commit, on any
branch: it was added to this copy here, locally, by the File > Import GDS work,
and the deferral was built on the assumption that upstream had taken away
something it had never carried. The re-sync was blocked by a mistake rather than
by a real coupling.

`technologies.<id>.stackup` is now specified upstream (`CHIPLET_FORMAT_SPEC.md`,
Technologies Section) and implemented in the reference reader and writer with a
round-trip test, so this directory is a clean verbatim copy again and the
deferred work is discharged rather than handed on.
