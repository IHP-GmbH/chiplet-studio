<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 IHP GmbH -->

# Chiplet Studio

Chiplet Studio shows a chiplet assembly described by a `.chiplet` file. It draws
the assembly in 3D, opens each die's GDS or OASIS layout in 2D through KLayout,
runs the build steps the file defines, and can be driven from an embedded
Python console.

It is alpha software. It runs no DRC, LVS or fabrication check, and the
thicknesses in its 3D view are for display. The `.chiplet` format itself is
specified in chiplet-system-spec, not here.

```{toctree}
:maxdepth: 1

usage
reference/python-api
```
