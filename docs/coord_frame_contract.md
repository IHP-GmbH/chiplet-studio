# Coordinate Frame Contract, Chiplet Pipeline

The canonical coordinate-frame contract now lives in the permissive spec
repository:

**[IHP-GmbH/chiplet-spec](https://github.com/IHP-GmbH/chiplet-spec)**
(Apache-2.0); see `docs/coord_frame_contract.md` there.

That document is the single source of truth for the `.chiplet` coordinate
frame, the `anchor:` convention, and the z-mounting rule that every writer and
reader must follow. It is licensed permissively so anyone may implement it
under any license, and it carries the reference-implementation notes for
Chiplet Studio's reader (`src/formats/ChipletFormat.*`, `src/view3d/*`,
`src/core/Assembly.cpp`) and the KiCad / plugin / `gds_to_kicad` writers.

This file is kept only as a pointer so existing path citations in code
comments (e.g. `src/core/IOPad.h`) and in companion docs keep resolving.
Section references in the code (for example "coord_frame_contract.md section 2"
or a "section 5" guard) refer to the numbered sections of the canonical
document in the chiplet-spec repository above, not to this stub.

The enforced post-conditions are exercised by `tests/test_coord_frame_contract.cpp`
and the `AssemblyView` world transform; the shared chiplet-to-scene mapping has
a single source of truth in `src/view3d/CoordFrame.h`.
