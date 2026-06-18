<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- SPDX-FileCopyrightText: 2026 IHP GmbH -->

# Vendored: chiplet_format_io (Apache-2.0)

This directory is a **verbatim copy** of the C++ reference reader/writer for the
`.chiplet` format, vendored from the permissive spec repository:

- **Upstream:** `IHP-GmbH/chiplet-spec`, `reference/cpp/`
- **Commit:** `1a56f61c321b1e2ed62bfa4b8ff81dfbcad9de41`
- **License:** Apache-2.0 (see the SPDX headers in each file)

`ChipletFormat::load` delegates `.chiplet` parsing + validation to this library
and then maps its plain-struct `ChipletDocument` into Chiplet Studio's
`core/Assembly`. The library is dependency-clean (yaml-cpp only — no Qt, no
KLayout), which is exactly why parsing the *format* lives here, in the permissive
layer, while the GPL host keeps the studio-specific concerns (path resolution,
warnings, z auto-calculation, flow handoff).

**Do not edit these files here.** Fix bugs upstream in `chiplet-spec`, then
re-vendor (copy the files over and bump the commit hash above). Vendoring an
Apache-2.0 library into this GPL-3.0-or-later project is license-compatible; the
copied files keep their own Apache-2.0 SPDX headers.
