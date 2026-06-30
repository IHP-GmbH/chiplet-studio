<!-- SPDX-FileCopyrightText: 2026 IHP GmbH -->
<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# PDK Stackup and Layer Guide

How Chiplet Studio decides, for every layer it draws in 3D, its **number**,
**name**, **z position**, **thickness**, **stacking order** and **color**, and
where each of those values comes from. Read this before adding a new PDK or
debugging a layer that renders at the wrong height or in the wrong color.

This document reflects the model after the layer-source cleanup (golden-gated):
one source per concern, no redundant fallbacks.

## The four data sources

Each PDK property is owned by exactly one kind of file. Nothing is duplicated
across sources anymore.

| Source | File / location | Owns | Consumed by |
|---|---|---|---|
| **GDS layout** | the design `.gds` (per technology) | layer **numbers** (layer/datatype) + the actual **geometry** | `GDSLayerExtractor` |
| **Stackup YAML** | `configs/stackups/<pdk>.yaml`, or an explicit `technologies.<id>.stackup` in the `.chiplet` | per-layer **z** (`z_bottom`) + **thickness** + **name**, hence the vertical **stacking order** | `LayerStackup::loadFromBlenderGDS`, resolved in `AssemblyView::buildLayerGeometry` |
| **.lyp** | `pdks/<pdk>/<file>.lyp`, or an explicit `technologies.<id>.layer_properties` | per-layer **fill color** (and the 2D layer table) | `LayerMeshBuilder::build` (the single color source), `KLayout2DView` |
| **Generic blackbox scheme** | `configs/stackups/colors/generic/blackbox.yaml` | the blue-body / yellow-pad colors for a **black-box die** that ships no `.lyp` | `LayerMeshBuilder::build` (loaded only when there is no `.lyp`) |

Key point: **stacking order is derived entirely from `z`** (`z_bottom`).
`LayerStackup::sortedLayers` sorts by `z_bottom` and `LayerMeshBuilder::build`
re-sorts by `z_bottom`; there is no explicit order index. The YAML's `index:`
key is the **GDS layer number**, not a stacking position (a common misread).

## The stackup priority ladder

`AssemblyView::buildLayerGeometry` resolves one stackup per technology, the
first rung that produces layers wins:

| Rung | Source | When it fires |
|---|---|---|
| **-1** | explicit `technologies.<id>.stackup` YAML in the `.chiplet` | the technology declares its own stackup path; wins over the built-in lookup so an unsupported PDK can ship its own |
| **0** | bundled `configs/stackups/<pdk>.yaml` via `BlenderGDSConfigs::stackupPath(techId)` | the techId matches a known PDK (see below); this is the path every supported PDK takes |
| **1** | `.lyp`-derived (`LayerStackup::fromLayerProperties`, default thickness) | no YAML resolved but a `.lyp` is present; sensible per-layer z for a `.lyp`-only PDK |
| **2** | generic interposer (`Stackups::createInterposer`) | neither a YAML nor a `.lyp`; a plain top-metal stack so geometry still renders |

The former techfile rung (`Technology::createStackup`) and the hardcoded
name-match rung (`Stackups::createSG13G2` and an interposer by substring) were
**removed**: for every bundled PDK the YAML at rung 0 already owned the order,
so they were dead overlap. `tests/test_stackup_order_golden.cpp` freezes the
resolved order of all five bundled PDKs and is the guard that this ladder keeps
producing the exact same layers, z, thickness and names.

After the stackup is chosen, the interconnect PDK's 3D bodies (cu-pillar /
solder-bump, vendor microbumps) are merged in for the interposer's technology
only; see `docs/interconnect_render_contract.md`.

## The color system

Color has a single source: the **`.lyp`**. `LayerMeshBuilder::build` walks this
chain per layer:

| Priority | Source | When |
|---|---|---|
| 0 | color scheme by layer name | only the generic blackbox scheme is ever passed now, and only for a no-`.lyp` die (keyed on the `outline` / `pad` roles) |
| 1 | **`.lyp` fill color** | the default for every supported PDK |
| 2a / 2b | hardcoded blue body / yellow pad | a black-box die when `blackbox.yaml` is absent |
| 3 | deterministic hashed HSV (from layer/datatype) | a real stackup layer with no `.lyp` and no scheme entry |

The per-PDK "realistic" / "fancy" / "marketing" color-scheme YAMLs and the
`colorSchemePath` resolver were deleted; the `.lyp` is now the realistic color.
`metallic` / `roughness` in the old schemes were always dead: `component.frag`
is plain Phong with a single `objectColor` uniform and never uploaded them.

## Where the bundled data lives

```
configs/stackups/
  ihp-sg13g2.yaml  ihp-sg13cmos5l.yaml  sky130.yaml  gf180mcu.yaml  intm4tm2.yaml
  colors/generic/blackbox.yaml      # the no-.lyp role scheme (the only scheme kept)
pdks/
  ihp-sg13g2/sg13g2.lyp             # the three PDKs that ship a bundled .lyp
  sky130/sky130.lyp
  interposer/interposer.lyp         # intm4tm2
```

All five bundled PDKs have a stackup YAML; only three ship a `.lyp`
(**ihp-sg13cmos5l and gf180mcu do not**, see the last footgun). `pdks/` is a
sibling of the configs dir in both the dev tree and the installed
`share/chiplet-studio` layout.

### Stackup YAML schema

Top-level keys are layer names; each maps to:

```yaml
TopMetal2:
  index: 134     # GDS layer number
  type: 0        # GDS datatype
  z: 8.6         # z_bottom in micrometers
  height: 3.0    # thickness in micrometers
```

Optional top-level scalars: `attachment_surface_z` (the surface an interposer
offers to interconnect bodies) and `z_reference`; see the IntM4TM2 stackup and
`docs/interconnect_render_contract.md`.

## Recipe: add an external / unsupported PDK

**Preferred (no code change).** Declare the stackup and `.lyp` directly on the
technology in the `.chiplet`. The explicit `stackup` takes rung -1, so the
techId does **not** need to match any built-in name:

```yaml
technologies:
  my_pdk:
    layout: ${MY_PDK_ROOT}/design/top.gds
    layer_properties: ${MY_PDK_ROOT}/tech/my_pdk.lyp   # colors + names + 2D
    stackup: ${MY_PDK_ROOT}/tech/my_pdk_stackup.yaml   # z / height / order
```

Both paths resolve through the same `${VAR}` / relative chain as every other
path in the format. With both present the PDK renders with real heights and
real colors and needs no recompile.

**First-class (bundle it).** To make a PDK resolve by name like the built-in
ones:

1. Add `configs/stackups/<pdk>.yaml`.
2. Add a substring branch to `BlenderGDSConfigs::stackupPath` in
   `src/core/LayerStackup.cpp`.
3. Optionally ship `pdks/<pdk>/<pdk>.lyp` and add a branch to
   `pdkLayerPropertiesPath` (else layers fall to hashed colors).
4. Add a `StackupOrderGolden` case in `tests/test_stackup_order_golden.cpp` and
   capture the golden (`UPDATE_STACKUP_GOLDEN=1 ./chiplet_tests
   --gtest_filter='StackupOrderGolden.*'`), then review the diff.

## Footguns

- **`stackupPath` / `pdkLayerPropertiesPath` are hardcoded substring matches**
  over a fixed set of names (sg13g2, sg13cmos5l, sky130, gf180, intm4tm2). A
  techId that contains none of them silently resolves to `""` and falls through
  to the `.lyp`-derived or generic rung. Use the explicit `technologies.<id>.stackup`
  (rung -1) for anything outside that set.
- **The `ihp` catch-all** in `stackupPath` maps *any* techId containing "ihp"
  (with no more specific match) to `ihp-sg13g2.yaml`. A different IHP PDK named
  loosely will quietly get the SG13G2 stackup.
- **No custom color directory.** Color comes only from the `.lyp` (or the
  generic blackbox roles). There is no per-project or per-PDK color override
  path; recolor by editing the `.lyp`.
- **No `.lyp` means hashed colors.** A PDK with a real stackup YAML but no
  `.lyp` (today: gf180mcu, ihp-sg13cmos5l) renders its real layers with
  deterministic hashed HSV colors (chain priority 3), not realistic ones.
  Neither is in any bundled demo; ship the PDK's real `.lyp` to fix it.
