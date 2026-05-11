# Coordinate Frame Contract — Chiplet Pipeline

**Status:** Adopted 2026-05-07
**Owner:** chiplet-studio (reader and contract owner); all writers cite
this document by path.
**Companion:** [`CHIPLET_FORMAT_SPEC.md`](./CHIPLET_FORMAT_SPEC.md)
(general schema — this doc adds frame and anchor semantics).

---

## 0. TL;DR

1. All `position:` values in a `.chiplet` file are expressed in
   **GDS-bbox-corner of the interposer top_cell**, y-up cartesian,
   units micrometers.
2. `position:` is the component's **geometric center**, not its corner.
3. Each component declares an explicit `anchor:` field:
   - `anchor: gds_origin` — the component mesh is built around its own
     GDS (0,0). Used by dies produced by `gds_to_kicad`.
   - `anchor: bbox_center` — the component mesh is centered on its own
     GDS bounding box. Used by interposers.
4. Z-mounting for dies on connection stacks is fixed by the formula
   in §3.
5. `hyp_to_gds.py --update-chiplet-file` is **mandatory** in the
   canonical path. KiCad's `pcbnew` GUI export produces an
   intermediate `.chiplet` whose positions live in the wrong frame
   (PCB-bbox-corner) and is not directly consumable by chiplet-studio.

Any tool that writes a `.chiplet` file MUST conform to this contract.
Any tool that reads one MUST validate that the contract is followed
(or fail loudly).

---

## 1. Canonical Coordinate Frame

### 1.1 Definition

The canonical frame for `.chiplet` `position:` values is:

| Property | Value |
|---|---|
| Reference object | Interposer's GDS top_cell |
| Origin (0, 0) | Lower-left corner of the interposer's GDS bbox |
| X axis | Increases rightward |
| Y axis | Increases upward (y-up cartesian) |
| Units | Micrometers (µm) |
| Float precision | At least 6 decimal places (1 pm theoretical) |

### 1.2 Why GDS-bbox-corner

- The GDS file is the physical fabrication artifact. The interposer
  GDS is the ground truth of the layout.
- KLayout, the 3D scene in chiplet-studio, and any downstream
  packaging tool all consume positions as offsets within the GDS
  bbox.
- The PCB Edge.Cuts bounding box (which KiCad uses natively) **does
  not always match** the GDS bbox. In the wire-bond demo the shift is
  (-200 µm, -780 µm) even though widths and heights agree to the µm.
  The GDS frame is the only one with no such hidden shift relative to
  what gets fabricated.

### 1.3 Diagram (top-down view)

```
                                  +--------------------------+
                                  |  GDS bbox of interposer  |
                                  |     (the reference)      |
                                  |                          |
                                  |    (die center)          |
                                  |        +                 |
                                  |       /                  |
                                  |      / position.y        |
                                  |     /                    |
                                  | (0,0)----- position.x    |
                                  +--------------------------+
                                  ^
                                  |
                          GDS-bbox-corner = canonical origin
```

`(0,0)` is the lower-left of the interposer's GDS bbox. Every
`position:` x and y in the .chiplet is measured from this corner,
y-up, in µm.

### 1.4 Position semantics

`position:` always refers to the component's **geometric center**.

For a die of width 1000 µm and height 2000 µm placed with its
lower-left corner at (250, 250) inside the interposer:
```yaml
position:
  x: 750.0      # 250 + 1000/2
  y: 1250.0     # 250 + 2000/2
  z: 61.83      # see §3 for Z mounting
dimensions:
  width: 1000.0
  height: 2000.0
  thickness: 50.0
```

The reader uses `position` and `dimensions` together to compute the
3D world placement (see §5).

---

## 2. Anchor Convention

### 2.1 The `anchor:` field

Each component declares how its local mesh is built:

| Value | Meaning | Use cases |
|---|---|---|
| `gds_origin` | The component mesh is built around its own GDS (0, 0). The `position:` value is added to the GDS-origin of the cell, no extra centering. | Dies produced by `gds_to_kicad` (footprint anchor at GDS (0,0)). Any die where the layout author put the cell origin where the placement reference should be. |
| `bbox_center` | The component mesh is centered on its own GDS bounding box. `position:` places the bbox center. | Interposers. Components whose GDS layout uses absolute pcbnew-derived coordinates and whose meaningful "placement reference" is the geometric center. |

The anchor is **declared by the writer** based on knowledge of how
the component's GDS was produced. The reader does **not** infer it
from `ComponentType` (the previous heuristic, removed in this
contract).

### 2.2 Default behavior

If `anchor:` is absent:
- The reader must default to `bbox_center` and emit a warning.
- This is for legacy `.chiplet` files only (D6, no auto-migration).
- New files MUST declare `anchor:` explicitly.

### 2.3 Example

```yaml
components:
  - id: interposer
    type: interposer
    technology: interposer_tech
    anchor: bbox_center
    layout: ./interposer.gds
    top_cell: TOP
    position:
      x: 1326.576    # half of GDS bbox width = 2653.152 / 2
      y: 2313.005    # half of GDS bbox height = 4626.009 / 2
      z: 0
    dimensions:
      width: 2653.152
      height: 4626.009
      thickness: 13.83

  - id: U1
    type: die
    technology: sg13g2
    anchor: gds_origin
    connection: cupillar_opt2
    orientation: flip_chip
    layout: ./Metal_Test.gds
    top_cell: Metal_Test
    position:
      x: 1503.584    # die center in interposer-local frame
      y: 1822.939
      z: 61.83       # see §3
    dimensions:
      width: 770.0
      height: 2606.339
      thickness: 0
```

---

## 3. Z-Mounting Rule

### 3.1 Formula

For dies that mount on a connection stack (cu-pillar, solder bump,
etc.):

```
z_die = mounting_surface + connection.total_height()
```

where:
- `mounting_surface` = `z_bottom` of the interposer stackup layer
  whose name matches the connection stack's first layer.
- `connection.total_height()` = sum of `height` for every layer in
  the connection stack.

### 3.2 Worked example (wire-bond demo)

Interposer technology: `interposer_tech` (IHP SG13G2 BEOL).
Die connection: `cupillar_opt2` (PacTech, two layers: CuPillar 32 µm
+ SnAgCap 16 µm = total 48 µm).

`cupillar_opt2.layers[0].name` = `CuPillar`. The interposer stackup
contains a layer named `TopMetal2` whose `z_bottom = 13.83`. The
connection stack physically attaches to TopMetal2.

Wait — the connection stack's first layer name (`CuPillar`) does not
appear in the interposer stackup. This is the common case for
visualization-only stacks. The lookup falls back to the interposer
stackup's TopMetal2 in the rule below:

```
1. Look up cupillar_opt2.layers[0].name = "CuPillar" in interposer
   stackup. NOT FOUND.
2. Fallback: use the interposer's physical thickness as the mounting
   surface, OR — in the chiplet-studio implementation — the
   stackup's last "real" layer top (TopMetal2, z_top = 13.83).
3. mounting_surface = 13.83.
4. z_die = 13.83 + 48 = 61.83 µm.
```

### 3.3 Reference implementation

`chiplet-studio/src/core/Assembly.cpp::calculate_component_z`,
lines 261–319 (post-`e903b16`).

```cpp
// Mounting surface = z_bottom of the chosen connection stack's first
// layer (the layer that physically attaches to the interposer pad,
// e.g. CuPillar for cu-pillar stacks). Looking it up in the
// interposer stackup gives the exact passivation-opening / pad-top
// height. Adding stack->total_height() then lands the die on the
// tip of the connection.
```

### 3.4 Edge cases (must be handled by reader)

| Case | Behavior |
|---|---|
| Die has no `connection:` field | **Fallback**: use `interposer.thickness`. (Currently returns 0.0 at `Assembly.cpp:264`; must be fixed in execution gate 2.) |
| `connection_stack` not defined in technology | **Fallback**: use `interposer.thickness`. |
| Connection's first layer not in interposer stackup | **Fallback**: use `interposer.thickness`. (Already handled at `Assembly.cpp:309-316`.) |
| Die has `position.z` explicitly set non-zero | Use the explicit value, do not auto-calculate. |

---

## 4. Writer Contract

Every tool that writes a `.chiplet` file MUST:
1. Express all `position:` x, y in the canonical frame (§1).
2. Use geometric center semantics (§1.4).
3. Declare `anchor:` explicitly per component (§2).
4. Set `z` to either the explicit user value, 0 to defer to
   auto-calc, or the auto-calculated value per §3.

### 4.1 KiCad `export_chiplet.cpp`

**Path:** `kicad/pcbnew/exporters/export_chiplet.cpp`

**Current behavior (audit 2026-05-07):**

| Output | Frame | Code |
|---|---|---|
| Interposer position | PCB-bbox-center (in PCB-bbox-corner local frame) | lines 263-265 |
| Die position | PCB-bbox-corner (footprint center, Y-inverted) | lines 377-378 |
| io_pad position | PCB-bbox-corner (footprint center, Y-inverted) | lines 307-308 |
| Y inversion | `interposer_y_max - pos.y` | line 240, 308, 378 |
| Bbox source | `GetBoardEdgesBoundingBox()` with fallback to `ComputeBoundingBox(false)` | lines 224, 228 |

**Required behavior — Option (a) adopted (locked 2026-05-07):**

KiCad cannot produce the canonical frame on its own (it does not
read the interposer GDS). The decision is to keep the two-step
pipeline and formalize it:

**Option (a) — Intermediate output, finalized by hyp_to_gds.py**
- Continue emitting in PCB-bbox-corner.
- Add a `_metadata:` block at the top of the YAML so consumers know
  the file is intermediate:
  ```yaml
  _metadata:
    frame: pcb-bbox-corner
    finalize_required: true
    finalizer: hyp_to_gds.py --update-chiplet-file
  ```
- Emit `anchor:` per component (writer knows: `bbox_center` for
  interposer, `gds_origin` for dies that came from `gds_to_kicad`).
- chiplet-studio MUST refuse to load files where
  `_metadata.finalize_required: true`.

**Option (b) — KiCad shells out to `hyp_to_gds.py` (rejected for now)**
The deeper integration (KiCad GUI invokes `hyp_to_gds.py`
automatically) is captured in §9 as future consolidation. It is a
separate effort, not part of this contract's execution. Reasons to
defer: (i) requires `hyp_to_gds.py` to be discoverable from KiCad's
runtime (PATH, plugin packaging, or bundling), which is a
distribution-level change; (ii) keeps the contract execution
focused on the alignment fix.

### 4.2 `hyp_to_gds.py::update_chiplet_file`

**Path:**
`kicad_designs/kicad_interposer_hyperlynx_to_gds/hyp_to_gds.py`

**Current behavior (audit 2026-05-07):**

| Output | Frame | Code |
|---|---|---|
| Interposer position | GDS-bbox-center (override of KiCad value) | lines 1576-1577 |
| Die position | GDS-bbox-corner (re-anchor from HYP-absolute) | lines 1685-1701 |
| io_pad position | **HYP-absolute (1e5+ µm), pass-through, BUG** | lines 1594-1596 |

**Required behavior:**

- Drop the band-aid override comments at lines 1565-1573 ("Setting
  position to (0,0) here used to be the convention but breaks
  against current Chiplet Studio…"). Replace with citation:
  `# Per chiplet-studio/docs/coord_frame_contract.md §1, position is
  in GDS-bbox-corner frame, geometric center.`
- Re-anchor `io_pads` x/y from HYP-absolute to GDS-bbox-corner using
  the same `(gds_left, gds_bottom)` shift already used for dies at
  lines 1685-1701. This is the residual bug. Pseudocode:
  ```python
  for pad in component['io_pads']:
      pad['position']['x'] -= gds_left
      pad['position']['y'] -= gds_bottom
  ```
- Emit `anchor:` per component:
  - Interposer: `bbox_center`
  - Dies: `gds_origin` (matches `gds_to_kicad` footprint origin
    convention; see §4.4)
  - io_pads: not applicable — io_pads are nested under the
    interposer and inherit its frame; they do not declare anchor
    themselves.
- Gate 3 locked **Option (a)** (§4.1): KiCad does not emit in the
  canonical frame; the finalizer in `update_chiplet_file` is the
  only place that owns the GDS bbox and performs the frame
  conversion. The interposer position override (lines 1545-1565
  post-Gate-3) and the die x/y re-anchor (lines 1690-1711
  post-Gate-3) are therefore **legitimate finalizer logic**, not
  band-aids. They stay. See §8 for the updated band-aid disposition.

### 4.3 `hyp_to_gds.py::add_io_pads` (and upstream JSON producer)

**Path:** `hyp_to_gds.py::add_io_pads` consumes `io_pads.json`
produced by `kicad_pcb_to_iopads.py`.

**Current behavior (audit 2026-05-07):**

The JSON contains pad coordinates in HYP-absolute (1e5+ µm). The
function passes them through to GDS at the same coords. This is
correct for GDS-internal placement (since the GDS is in HYP-absolute
already), but those same numbers leak into the `.chiplet` if the
caller passes the JSON directly.

**Required behavior:**

The JSON itself is allowed to remain HYP-absolute (it is consumed by
GDS-side placement which lives in absolute coords). The conversion
to GDS-bbox-corner is the responsibility of `update_chiplet_file`
(§4.2 above, which already owns the GDS bbox lookup at line
1528-1530).

### 4.4 `gds_to_kicad` (footprint origin convention — settled)

**Path:** `gds_to_kicad/gds_to_kicad.py`

**Behavior (settled, no changes required):**

The tool sets the KiCad footprint reference text at `(at 0 0)`
(line 273-279), placing the footprint origin at GDS cell (0, 0).
Pads are placed at their GDS-derived `(center_x, center_y)` relative
to (0, 0) (line 289-295). The `--flip-chip` flag mirrors X only.

This convention is the reason dies declare `anchor: gds_origin` —
their GDS (0,0) is the reference point KiCad uses.

### 4.5 Future writers

Any new tool emitting `.chiplet` files (manual editor, plugin,
script) MUST cite this document by path in a comment near the writer
code. Reviewers MUST reject writers that don't.

---

## 5. Reader Contract

Every tool that reads a `.chiplet` file MUST:
1. Parse `anchor:` and store it on the component.
2. Use `anchor:` (not `ComponentType`) to drive mesh centering.
3. Validate position values are in plausible range; warn loudly if
   any `position.x` or `position.y` exceeds 1e5 µm (heuristic
   indicator of HYP-absolute leakage).

### 5.1 `chiplet-studio/src/formats/ChipletFormat.cpp`

**Current behavior (audit 2026-05-07):**

Frame-agnostic parser; reads positions verbatim
(lines 319-321, 335-338, 381-419). Stores in `Component`/`IOPad`
without transformation.

**Required behavior:**

- Parse `anchor:` field per component (default to `bbox_center` on
  absence with `[chiplet] WARN: anchor not declared, defaulting to
  bbox_center` log).
- Reject files declaring `_metadata.finalize_required: true` with a
  clear error: `error: this .chiplet is intermediate (PCB-bbox-corner
  frame); run hyp_to_gds.py --update-chiplet-file to finalize`.
- Validate `|position.x|, |position.y| < 1e5 µm` per component;
  warn on violation.

### 5.2 `chiplet-studio/src/view3d/LayerMeshBuilder.cpp`

**Current behavior (audit 2026-05-07):**

The constructor takes a `useGdsOriginAsAnchor` boolean parameter
(line 161). Centering decision at lines 200-210:
```cpp
if (has_polygons && !useGdsOriginAsAnchor) {
    center_offset_x = (global_min_x + global_max_x) / 2.0;
    center_offset_y = (global_min_y + global_max_y) / 2.0;
}
```

**Required behavior:**

- Rename the parameter to align with the schema:
  `Anchor anchor` (an enum: `GdsOrigin` or `BboxCenter`).
- Logic stays the same (logically equivalent), but the source of
  truth becomes the schema field, not a per-call flag computed by
  the caller.

### 5.3 `chiplet-studio/src/view3d/AssemblyView.cpp`

**Current behavior (audit 2026-05-07):**

Line 1484:
```cpp
bool useGdsOrigin = (comp.type() == ComponentType::Die);
```
This is the **central band-aid** (`bbc9c60`). It encodes "all dies
use GDS origin, all interposers use bbox center" as a hard-coded
heuristic.

**Required behavior:**

- Replace line 1484 with:
  ```cpp
  Anchor anchor = comp.anchor();
  ```
- Pass `anchor` to `LayerMeshBuilder` instead of the boolean.
- Coordinate mapping at lines 1499-1502 stays as-is:
  ```cpp
  geometry.transform.translate(
      static_cast<float>(pos.x / 1000.0),    // chiplet X -> 3D X (mm)
      static_cast<float>(pos.z / 1000.0),    // chiplet Z -> 3D Y (mm)
      static_cast<float>(-pos.y / 1000.0));  // chiplet Y -> 3D -Z (mm)
  ```
- flipZ scale at lines 1504-1506 stays as-is (orthogonal to anchor).

### 5.4 `chiplet-studio/src/core/Component.h` / `IOPad.h`

**Required additions:**

- Add `enum class Anchor { GdsOrigin, BboxCenter }` in a new header
  `chiplet-studio/src/core/Anchor.h` (or inline in `Component.h`,
  decide in execution).
- Add `Anchor m_anchor = Anchor::BboxCenter` to `Component` with
  `anchor()` getter and `set_anchor()` setter.
- Add string conversion helpers:
  `anchor_to_string(Anchor)` and `string_to_anchor(const std::string&)`.
- Update `IOPad.h:27-31` doc comment from
  ```
  2D pad position in micrometers (interposer-global coordinates).
  ```
  to
  ```
  2D pad position in micrometers, in the canonical frame defined in
  chiplet-studio/docs/coord_frame_contract.md (interposer-local,
  GDS-bbox-corner).
  ```

### 5.5 `chiplet-studio/src/core/Assembly.cpp::calculate_component_z`

**Current behavior (audit 2026-05-07):**

Lines 264, 268-270 return `0.0` for dies with missing `connection`
or undefined `connection_stack`.

**Required behavior:**

Replace the `return 0.0` early exits with a fallback to
`interposer.thickness`, matching the existing fallback at lines
309-316. New behavior table:

| Case | New behavior |
|---|---|
| `comp.connection().empty()` | `mounting_surface = interposer.thickness; total_height = 0` |
| `connection_stack(comp.connection())` returns null | same as above |
| First layer name not in interposer stackup | (already handled, lines 309-316) |

The Z-mounting formula §3 holds in all cases.

---

## 6. io_pads Convention

### 6.1 Frame

io_pads `position:` lives in the same canonical frame as components
(GDS-bbox-corner of the interposer top_cell, geometric center of the
pad, µm). io_pads are nested under their interposer in the schema
and inherit the interposer's frame.

io_pads do **not** declare an `anchor:` field. They are points (size
is 2D extent, not a centering rule).

### 6.2 Current bug

The wire-bond demo today contains io_pads in HYP-absolute (e.g.,
`x: 131613.916, y: -70729.257`). The interposer GDS bbox is
~2653 × 4626 µm, so values of 1e5 µm are clearly outside the frame.

The bug is in `hyp_to_gds.py::update_chiplet_file` which
pass-throughs JSON values without re-anchoring (see §4.2, §4.3).

### 6.3 Why this hasn't broken anything yet

io_pads are not currently rendered in 3D in chiplet-studio. The
reader stores them as metadata (per audit, no view3d/ or view2d/
code consumes them). The bug is silent today but is a footgun for
the wire-bond visualization TODO and for any future tool that
consumes io_pad positions geometrically.

---

## 7. Verification Fixtures

Three fixtures, all required (D7).

### 7.1 Synthetic fixture (unit test)

**Files:**
- `chiplet-studio/tests/fixtures/coord_contract_synth.chiplet`
- `chiplet-studio/tests/test_coord_frame_contract.cpp`

**Fixture contents:**
- 1 interposer, 1000 × 1000 µm, `anchor: bbox_center`,
  `position: (500, 500, 0)`.
- 2 dies of 100 × 100 µm thickness 50, `anchor: gds_origin`:
  - Die A at `position: (250, 250, 50)`.
  - Die B at `position: (750, 750, 50)`.
- 4 io_pads at the corners of the interposer:
  `(50, 50)`, `(950, 50)`, `(50, 950)`, `(950, 950)`.

**Test assertions:**
- `Component::anchor()` returns the parsed value for each component.
- After loading, the interposer's resolved 3D world position
  (computed via the same code path as `AssemblyView`) is
  `(0.5, 0.0, -0.5) mm` (or whatever follows from the canonical
  mapping; spell out the exact expected vector in the test).
- Die A's 3D world position is `(0.25, 0.05, -0.25) mm`.
- Die B's 3D world position is `(0.75, 0.05, -0.75) mm`.
- io_pad positions parse correctly and are stored verbatim.

### 7.2 Demo round-trip (integration test)

**Trigger:** regenerate `interposer_wire_bonding_demo.chiplet`
end-to-end via KiCad export + `hyp_to_gds.py --update-chiplet-file`
against the post-execution code.

**Test assertion (in `test_coord_frame_contract.cpp`):**
load the regenerated `.chiplet`, find U1, assert:
- `U1.anchor() == Anchor::GdsOrigin`
- `U1.position().z == 61.83 ± 0.01 µm` (sum of TopMetal2 z_top +
  cupillar_opt2 total_height)
- For each U1 pad-on-TopMetal2 in the U1 GDS, its world XY is
  within 1 µm of the closest cu-pillar SnAgCap cap center on the
  interposer.

This is the regression net for the 6 alignment incidents.

### 7.3 KLayout-independent check (geometric verification)

**File:**
`kicad_designs/kicad_interposer_hyperlynx_to_gds/tests/check_complete_gds_alignment.py`

**Behavior:**
- Take a `*_complete.gds` path as argument.
- Use klayout pya to:
  - Find the `TOP` cell.
  - Locate U1's flipped instance (cell name pattern
    `*_flipped` or via the U1 reference in connection_stacks).
  - Compute U1's flipped-instance bbox in TOP coords.
  - Compute the cu-pillar array bbox in TOP coords (filter by the
    cu-pillar layer pair).
  - Assert: U1 bbox and cu-pillar array bbox overlap; centroid
    distance within 1 µm.
- Exit 0 on success, non-zero with a clear message on mismatch.

This script is independent of chiplet-studio so a chiplet-studio
bug cannot mask a real GDS misalignment.

### 7.4 Running all three

```bash
# 1. Unit tests (chiplet-studio synthetic + round-trip)
cd chiplet-studio/build
./tests/chiplet_tests --gtest_filter='CoordFrameContract*'

# 2. Demo regen
cd kicad_designs/kicad_interposer_hyperlynx_to_gds
python3 hyp_to_gds.py \
  --hyp ../../interposer_wire_bonding_demo/test.hyp \
  --update-chiplet-file ../../interposer_wire_bonding_demo/interposer_wire_bonding_demo.chiplet

# 3. KLayout-independent geometric check
python3 tests/check_complete_gds_alignment.py \
  ../../interposer_wire_bonding_demo/interposer_wire_bonding_demo_complete.gds
```

All three must pass before declaring this contract implemented.

---

## 8. Band-Aid Disposition

Gate 6 audited each of the four pre-execution band-aids against the
final Option (a) writer/reader contract. Two were removed, two were
re-categorized as legitimate Option (a) finalizer logic. See the
**Gate 6 disposition** column for what actually happened.

Removed during execution:

| Commit | Repo | Description | Gate 6 disposition |
|---|---|---|---|
| `bbc9c60` | chiplet-studio | `useGdsOriginAsAnchor` per-`ComponentType` heuristic at `AssemblyView.cpp:1484`. Replaced by `comp.anchor()`. | **Removed in Gate 2** (`56033db`). Sweep-audit at Gate 6 confirms zero residual references. |
| `e901d35` | KiCad fork | Interposer position at PCB-bbox-center (`width/2, height/2`). | **Removed in Gate 6.** KiCad now emits `interposer.position = (0, 0, 0)` as an intermediate placeholder; the canonical value is computed by the finalizer. Chiplet-studio refuses to load the intermediate file anyway (via `_metadata.finalize_required`). |

Kept and documented (not removed):

| Commit | Repo | Description | Why kept |
|---|---|---|---|
| `6537e38` step 1 | hyp_to_gds.py | Override of interposer position to `(gds_width/2, gds_height/2)`. | **Legitimate Option (a) finalizer.** KiCad emits in PCB-bbox-corner; it does not own the GDS bbox. Converting PCB-bbox-corner → GDS-bbox-corner for the interposer is the finalizer's job. Removing this would leak the wrong frame to the canonical file for any design where PCB-bbox ≠ GDS-bbox (i.e. the general case). Pre-Gate-6 the §8 table mislabelled this as a band-aid; corrected here. |
| `6537e38` step 2 | hyp_to_gds.py | Re-anchor of die x/y from HYP-absolute to GDS-bbox-corner using `dev.x * 1e6 - gds_left`. | **Legitimate Option (a) finalizer.** Same reasoning as step 1, for dies. Pre-Gate-6 the §8 table mislabelled this as a band-aid; corrected here. The wire-bond demo's `U1.position = (1503.58, 1822.94)` depends on this conversion; removing it would shift U1 by `(-200, -780) µm` from canonical (verified by the round-trip test added in Gate 5). |
| `d166da9` | chiplet-studio | Exclude connection layers from interposer max_z | Z-mounting formula uses connection-stack first-layer lookup; this exclusion is part of the formula. Documented in §3. |
| `e903b16` | chiplet-studio | Connection-stack first-layer z_bottom lookup | The Z mounting rule itself. Documented in §3. |
| `e903b16` | chiplet-studio | Skip non-stackup GDS layers at build time | Orthogonal to coord frames. Avoids spurious magenta sheets from auto-elevated LVS/recognition layers. Documented as a rendering rule, not a coord-frame band-aid. |
| `e8063cb` | chiplet-studio | Substrate procedural injection at z=-50 µm | Aesthetic choice (real wafer is 750 µm). Document in chiplet-studio's stackup yaml comment. Not coord-frame related. |
| `5512eb0` | hyp_to_gds.py | `cleanup_orphan_top_cells` (orphan flip-chip template prune) | Orthogonal to coord frames; protects against the Metal_Test orphan top cell issue. |

Post-removal sanity check:
```bash
git grep -nE 'useGdsOriginAsAnchor|PCB bbox center|stale convention'
# Expected: zero hits in non-doc files (this doc and CHANGELOGs are OK).
# Note: 're-anchor' deliberately not included — that is the
# legitimate term for the §8 'kept' finalizer logic in hyp_to_gds.py.
```

---

## 9. Future Consolidation (Out of Scope)

User-requested follow-up (separate session, NOT in scope here):

KiCad's "Export Chiplet" GUI action should integrate the
Hyperlynx + GDS pipeline so a single button produces the canonical
`.chiplet` without a separate `hyp_to_gds.py` invocation. All
information `hyp_to_gds.py` consumes is already in the Hyperlynx
file produced by KiCad.

This is a refactor, not a redesign. Scope:
1. KiCad fork's "Export Chiplet" action also emits the .hyp file.
2. The action invokes (or bundles) `hyp_to_gds.py
   --update-chiplet-file` to finalize.
3. The intermediate `_metadata.finalize_required: true` marker
   becomes unnecessary.

Tracked here so it is not lost. To be picked up after the execution
session lands and the contract is verified.

---

## 10. Execution Checklist (Linear, Gated)

The execution session works through these gates **in order**. Do
not advance to gate N+1 until gate N is committed and verified.

### Gate 1 — Schema doc + types

- [ ] This document published at `chiplet-studio/docs/coord_frame_contract.md`
- [ ] `Anchor` enum added (location: `Anchor.h` or inline in
      `Component.h`, decide here)
- [ ] `Component::anchor()`/`set_anchor()` API
- [ ] `IOPad.h` doc comment updated to cite this doc
- [ ] `ChipletFormat::parse_component()` parses `anchor:` field
- [ ] `ChipletFormat::save()` writes `anchor:` field
- [ ] `ChipletFormat::load()` rejects `_metadata.finalize_required: true`
- [ ] Existing chiplet-studio tests still pass

### Gate 2 — Reader updates

- [ ] `LayerMeshBuilder` constructor takes `Anchor` instead of
      `useGdsOriginAsAnchor` bool
- [ ] `LayerMeshBuilder::build` centering logic gated on `Anchor`
- [ ] `AssemblyView.cpp:1484` uses `comp.anchor()` (replace heuristic)
- [ ] `Assembly::calculate_component_z` fallback to
      `interposer.thickness` for missing connection / null
      connection_stack
- [ ] Existing tests still pass

### Gate 3 — Writer updates

KiCad approach: **Option (a) — intermediate output**. Locked.

- [ ] KiCad emits `_metadata.finalize_required: true` block at the
      top of the YAML output
- [ ] KiCad emits `anchor:` per component (`bbox_center` for
      interposer, `gds_origin` for dies)
- [ ] `hyp_to_gds.py::update_chiplet_file` re-anchors `io_pads` to
      GDS-bbox-corner
- [ ] `hyp_to_gds.py::update_chiplet_file` emits `anchor:` per
      component (preserves the values KiCad already wrote)
- [ ] `hyp_to_gds.py::update_chiplet_file` strips the `_metadata`
      block on output (the canonical .chiplet has no
      `finalize_required` marker)
- [ ] Drop band-aid comments in `hyp_to_gds.py`; replace with
      citation to this doc

### Gate 4 — Demo regeneration

- [ ] Regenerate `interposer_wire_bonding_demo.chiplet` end-to-end
- [ ] Visual check in chiplet-studio: U1 pads contact cu-pillar tips
      with no offset
- [ ] Visual check in KLayout: `complete.gds` assembly correct

### Gate 5 — Verification fixtures + tests

- [ ] `coord_contract_synth.chiplet` fixture (§7.1)
- [ ] `test_coord_frame_contract.cpp` (§7.1, §7.2)
- [ ] `check_complete_gds_alignment.py` script (§7.3)
- [ ] All three pass

### Gate 6 — Band-aid disposition

The pre-execution §8 table marked four items as "delete". Gate 6
audited each one against Option (a) and updated the disposition.

- [x] `useGdsOriginAsAnchor` (`bbc9c60`, chiplet-studio): swept in
      Gate 2 (`56033db`). Audit grep at Gate 6 returns zero residual
      references — no further action.
- [x] `hyp_to_gds.py 6537e38` step 1 + step 2: **re-categorized
      from "delete" to "keep"**. Under Option (a) (§4.1), KiCad emits
      PCB-bbox-corner intermediate; the finalizer (`update_chiplet_file`)
      owns the GDS bbox and performs the canonical conversion.
      Removing step 1/2 would leak intermediate-frame positions to
      the canonical file. See updated §8.
- [x] `kicad e901d35` PCB-bbox-center interposer position: reverted.
      KiCad now emits `interposer.position = (0, 0, 0)` as an
      intermediate placeholder; the finalizer writes the canonical
      value.
- [x] `git grep` sanity for `useGdsOriginAsAnchor`,
      `PCB bbox center`, `stale convention`: zero hits in code paths.
- [x] Gate 5 regression net green: `CoordFrameContract*` 10/10 PASS;
      `check_complete_gds_alignment.py` dx=0.000 µm exit 0.

### Gate 7 — Final regression

- [ ] Full chiplet-studio test suite green
- [ ] Demo visually correct in chiplet-studio + KLayout
- [ ] One commit per gate, not bundled
- [ ] Update `chiplet-studio/CHANGELOG.md` and
      `kicad/CHANGELOG.md` to mark the systemic alignment issue
      as resolved
- [ ] Resume the paused TODOs (Layers panel, auto-Detailed for
      flip-chip, Python bindings)

---

## Appendix A — Glossary of Coordinate Frames

The 9 frames in the toolchain prior to this contract:

| # | Frame | Origin | Used by |
|---|---|---|---|
| 1 | KiCad PCB internal nm | KiCad's signed int IU (nm) | KiCad core; not exposed to .chiplet |
| 2 | PCB-bbox-corner | Lower-left of `BoardEdgesBoundingBox` (or fallback) | KiCad `export_chiplet.cpp` for dies and io_pads |
| 3 | PCB-bbox-center | (PCB_w/2, PCB_h/2) | KiCad `export_chiplet.cpp` for interposer (post-`e901d35`) |
| 4 | HYP absolute | Hyperlynx file native (meters, KiCad y-down convention pre-conversion) | `hyp_to_gds.py` input parsing |
| 5 | GDS absolute | GDS file native (µm, y-up cartesian) | KLayout, hyp_to_gds.py for cell instance placement |
| 6 | **GDS-bbox-corner** ← canonical | Lower-left of interposer GDS bbox | **THIS CONTRACT.** Also `update_chiplet_file` for dies post-`6537e38`. |
| 7 | GDS-bbox-center | (GDS_w/2, GDS_h/2) | `update_chiplet_file` for interposer (band-aid `6537e38` step 1) |
| 8 | Interposer-local-corner | The frame components mean to be in (per `IOPad.h:27-31` comment) | The schema's intent. Now formalized as = #6. |
| 9 | Chiplet-studio 3D world | OpenGL world (mm, y-up, z-out-of-screen) | `AssemblyView` final placement |

After this contract: only #6 (canonical) and #9 (3D world) are
visible from the schema's perspective. The rest are internal to
specific tools and never appear in `.chiplet` files.

---

## Appendix B — Audit Findings (2026-05-07)

Phase A audit run by 3 Explore agents on the wire-bond demo state.
Summary of writer × reader matrix at the time of contract drafting:

### Writers

| Component | `export_chiplet.cpp` | `hyp_to_gds.py update_chiplet_file` |
|---|---|---|
| interposer | center @ PCB-bbox-corner (line 263-265) | center @ GDS-bbox-corner (line 1576-1577, override) |
| die | center @ PCB-bbox-corner (line 377-378) | center @ GDS-bbox-corner (line 1700-1701, re-anchor) |
| io_pad | center @ PCB-bbox-corner (line 307-308) | **HYP-absolute, no re-anchor** (line 1594-1596 pass-through) |

### Readers (chiplet-studio)

| Component | ChipletFormat | LayerMeshBuilder | AssemblyView |
|---|---|---|---|
| die | verbatim, no transform | mesh @ GDS (0,0) when `useGdsOriginAsAnchor=true` | line 1484 hard-codes Die → true |
| interposer | verbatim, no transform | mesh @ bbox center | line 1484 hard-codes Interposer → false |
| io_pad | verbatim, stored | NOT RENDERED | metadata only |

### Confirmed locked

- `gds_to_kicad`: footprint anchor at GDS (0,0) per
  `gds_to_kicad.py:273-279, 289-295`. `--flip-chip` mirrors X only.
- Z-mounting formula at `Assembly.cpp:261-319` is structurally
  correct; only the missing-connection edge case needs the fallback
  fix in §5.5.

### Documented intent vs reality

- `IOPad.h:27-31` comment says "interposer-global coordinates" —
  matches this contract.
- Writer emits HYP-absolute. Bug, not schema ambiguity.

---

## Appendix C — References

- `chiplet-studio/docs/CHIPLET_FORMAT_SPEC.md` — general schema
  (companion document; this doc adds frame and anchor semantics).
- `chiplet-studio/CHANGELOG.md` "2026-05-07 — Systemic alignment
  problem" — full incident history that drove this contract.
- `kicad/CHANGELOG.md` "Pending: systemic alignment work" —
  KiCad-side band-aid context.
- Commits referenced:
  - chiplet-studio `bbc9c60`, `d166da9`, `e903b16`, `e8063cb`
  - hyp_to_gds.py `5512eb0`, `6537e38`
  - KiCad `e901d35`
- Phase A audit reports (in-memory; not persisted as separate
  files): writer matrix, reader matrix, Z-mounting + io_pads +
  gds_to_kicad — captured in §4, §5, Appendix B.

---

## Version History

| Version | Date | Author | Changes |
|---|---|---|---|
| 1.0 | 2026-05-07 | Mauricio Montañares | Initial contract. Adopted GDS-bbox-corner as canonical frame; explicit `anchor:` field per component; Z-mounting formal definition; verification fixtures spec. |
