# Interconnect rendering and die seating: data contract

How Chiplet Studio renders the 3D bodies of an interconnect method
(cu-pillars, solder bumps, vendor microbumps) and seats dies on them, and
the data contract this implies between the interposer PDK, the
interconnect PDK and this tool. Companion to `coord_frame_contract.md`
(coordinate frames) and the interconnect PDK's manifest (methods, layers,
DRC rules).

## Architecture

Three parties contribute, each through its own artifact:

| Party | Artifact | Declares |
|---|---|---|
| interposer PDK | technology stackup YAML | the substrate layers and (target state) the **attachment surface z** |
| interconnect PDK | `libs.tech/chiplet_studio/stackup_fragments/<adapter>.stackup.yaml` | the method's 3D **body layers** (GDS layer/datatype, heights) |
| `.chiplet` | `interconnect:` block | the active **adapter** + the method's PDK-backed technology identity |

At load time the studio registers the interconnect method as a Technology
(keyed by the adapter id) so it appears alongside the die/interposer PDKs
with its own layer properties and provenance.

At render and z-calculation time the method's stackup fragment is merged
into the **interposer technology's** stackup — and only that one. The
merge is additive and idempotent (`addLayer` overwrites by layer key).
Two consumers perform it and must stay in lockstep:

- `AssemblyView::buildLayerGeometry` — gives the body layers z/height in
  the interposer component's mesh.
- `Assembly::calculate_component_z` — locates the mounting surface (the
  z where a die's connection stack starts) by looking up the stack's
  first layer name in the merged stackup.

A die's seating height itself comes from the `.chiplet`'s inline
`connection_stacks` (per die), not from the fragment; the fragment is the
render-side and surface-lookup data.

## Conventions (load-bearing, keep them true)

**C1 — Body shapes live in the interposer's layout.** Generators place
the interconnect body polygons (e.g. 500/35, 501/35, or a vendor's
510/35, 511/35) inside the GDS that the interposer component references.
The studio renders them as layers of the interposer mesh whose elevations
come from the merged fragment. Any future generation flow must keep this
invariant or the bodies will not render.

**C2 — Fragment is keyed by adapter.** One fragment per interconnect
adapter; the assembly's `interconnect.adapter` selects it. Methods
sharing an adapter (e.g. cu-pillar options) share the fragment.

**C3 — Merge is scoped to the interposer technology.** Merging into any
other technology corrupts flip-chip rendering: a FaceDown die's stackup
`totalHeight()` is its z-inversion reference, and inflating it shifts
every die layer upward by the body-stack height.

## Known couplings and their target state

**L1 — Fragment z is absolute today (couples method data to one
interposer).** Fragments currently encode the body base at the IntM4TM2
attachment surface (z = 13.83 um). That mixes method-owned data (body
heights) with interposer-owned data (surface height), and it is
load-bearing in both consumers. Consequence: a second interposer PDK with
a different BEOL height would require one fragment per
(method x interposer) pair, or dies would seat at the wrong z.

*Target:* fragments declare `z_reference: attachment_surface` and use
z values relative to 0; the interposer stackup YAML declares
`attachment_surface_z` (for IntM4TM2: 13.83, the Passiv top); consumers
offset at merge time. Fragments without the marker keep the legacy
absolute interpretation (with a deprecation warning). Note that the
attachment surface is a declared value, not `max_z` of the stackup —
passivation geometry can exceed the real mounting surface.

**L2 — The adapter is an assembly-level singleton.** Each die carries its
own `connection:` (so per-die seating is already correct for mixed
stacks), but one `interconnect.adapter` selects one fragment and one
interconnect DRC deck for the whole assembly. An assembly mixing methods
from different interconnect vendors renders and checks only one of them
correctly.

*Target:* per-interface interconnect. Render-side, merging the union of
the fragments of all methods present is sound (body layer keys are
disjoint by construction — the manifest schema enforces unique
layer/datatype); format- and DRC-side this needs a schema extension and
per-method rule scoping.

**L3 — Bodies inherit the interposer's render identity.** Being part of
the interposer mesh, the bodies share its render mode and selection, and
their colors resolve through the interposer technology rather than the
interconnect PDK's layer properties.

*Target:* a dedicated mesh group built from the fragment's layer keys,
giving the method its own render mode, 3D selection and `interconnect.lyp`
colors. Until then, per-layer show/hide is available through the
hierarchy's interconnect row and its properties view.

## What is deliberately decoupled already

- The two DRC adapter axes (interposer = where attachment lands,
  interconnect = how/at what density) and the manifest as single source
  of truth for methods.
- Per-die inline connection stacks: die seating is correct per die and
  vendor-agnostic.
- Discovery: fragments and the interconnect PDK resolve by environment
  variable or sibling-checkout walk; no fixed-depth paths.
- The `.chiplet` identity: `interconnect.technology` round-trips and the
  method is a first-class Technology in the UI.
