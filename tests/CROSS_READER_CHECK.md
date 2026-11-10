# The cross-reader check for the .chiplet writer

`chiplet_cross_reader` (ctest) reads `.chiplet` files written by this studio
with PyYAML and checks what PyYAML makes of them. It is a regression guard
over the paths classified below. It is not a format conformance test, and it
should not be cited as one.

## What it checks

Two YAML implementations read a `.chiplet` written here. One is yaml-cpp, in
this studio. The other is PyYAML, used by the KiCad plugin, by
chiplet-system-spec's reference reader and by other tools downstream. They
disagree on what some unquoted scalars mean. For example, `id: 0755` is the
integer 493 under YAML 1.1 and the string `0755` under yaml-cpp. The writer
therefore quotes every field the schema declares as a string.

Every other test of that quoting runs under yaml-cpp, so it can only show that
this host reads back what this host wrote. This check covers the claim that
matters downstream: the other reader gets the same thing.

When the check was written, the whole `.chiplet` corpus in this repository was
saved through the writer and read with PyYAML 6.0.3, and there were no
discrepancies. That sweep was a one-time result, and this check does not rerun
it. It guards against regressions; it did not fix a bug.

## Which paths it covers

`tests/fixtures/cross_reader_classification.json` puts every addressable path
of `chiplet.schema.json` in one bucket. The paths come from walking the schema
and resolving every `$ref`, counting each route to a definition separately. A
definition reached from two places is two paths, because the writer emits it
from two call sites.

There are 89 addressable paths: 88 leaves, plus the second form of the
`components[].cells` oneOf, which is addressable both as a scalar and as a
sequence.

- **Emitted, 46.** String-typed, and the writer can produce them. Each one
  must have an expectation; the check fails if one is missing.
- **Canonicalized, 2.** `.components[].cells` and `.components[].cells[]`. A
  single cell is written as `top_cell`, so `cells` appears only for a
  component with two or more.
- **Not supported, 11.** `._metadata.finalizer`, `._metadata.frame`, the three
  `3dblox_ref` fields, the five `cdxml_ref` fields and `.interposer.adapter`.
  The writer cannot emit these, and an expectation naming one fails on
  purpose.
- **Non-string, 24.** Numbers and booleans. Quoting them would be wrong.
  Whether the two readers agree on integer versus float, and on precision, is
  a separate question that this check does not cover.
- **Numeric, 4.** `.components[].dimensions.width`, `.height`, `.thickness`
  and `.technologies.<id>.dbu`, the four non-string paths a GDS import emits.
  They are pinned by Python type and by value. The pins record what the writer
  does today, not what it should do. The schema declares `number` for all
  four, but yaml-cpp writes `2000.0` as `2000`, so PyYAML reads an int for
  three of them.
- **Undeclared value type, 1.** `.components[].metadata` allows any
  properties and declares no value type, so it has no path to address. The
  writer does quote its values. It is not covered.
- **Opaque, 1.** `flow`, which the format tells hosts not to interpret.

The check asserts that the buckets sum to `addressable_total` and that no path
is in two buckets.

That gives two coverage figures, and they are never added together: **48 of 48
reachable string paths**, and **4 of 28 numeric paths**.

## The real producer, once

Coverage runs on a purpose-built fixture, loaded and saved through the writer.
The check also runs the real producer once. `chiplet_studio.write_single_gds_import`
is the function the Import GDS action calls. Its document is read by PyYAML and
by this studio's own reader. The four numeric values, the anchor, the layout
path, the top cell and the technology are asserted on both sides.

- **No default is repeated in the binding.** The fixture's spec leaves out
  `thickness_um`, so the expected 200 comes from the C++ default
  (`ChipletSynth.h`). If that default changes, the check fails.
- **The write must succeed.** `Outcome::Written` is asserted before
  `result.path` is read. A fallback write also carries a path, so checking
  the path alone would pass on a failed write.
- **These assertions are kept apart.** They live under their own
  `import_producer` key, outside the coverage arithmetic, so the producer's
  output cannot stand in for a missing fixture expectation.

What this half does not see is the Import GDS dialog. It checks what the
writer does with a spec, not what the dialog passes to the writer.

## How it runs

PyYAML 6.0.3 lives in a venv in `docker/Dockerfile.build`
(`/opt/crossreader-venv`). It is never a runtime dependency of Studio and is
never installed globally. The ctest entry is registered unconditionally.
A missing interpreter or PyYAML fails the check instead of skipping it, so
the check cannot drop out of the gate unnoticed.
`tests/run_cross_reader_check.sh` is the entry point.

## On the version pinned

chiplet-system-spec's reference reader declares `PyYAML>=5.1`. Pinning 6.0.3
keeps the check reproducible, but it tests one point in a wider declared range.

## What it does not cover

- **Meaning.** It compares types and values. Two readers agreeing that a field
  is the string `"0755"` says nothing about whether they agree on what it
  denotes.
- **Most numeric paths.** It covers four numeric paths, and for those it pins
  the current disagreement rather than resolving it. A green check means
  PyYAML still reads an int for `thickness`, not that it should. Precision is
  not measured on any path.
- **Schema growth.** The classification is a file in this repository, not a
  live read of the schema. A string field added to the schema later is
  unexercised until someone adds it here, and nothing fails to say so. This is
  the weakest link in the check.
- **The plugin's writer, and `flow:`.** The plugin's writer is a separate
  byte-level contract and is not checked here. `flow:` is carried verbatim, so
  it cannot diverge while that remains true.
