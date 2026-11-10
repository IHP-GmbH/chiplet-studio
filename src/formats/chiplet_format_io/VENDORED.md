<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- SPDX-FileCopyrightText: 2026 IHP GmbH -->

# Vendored: chiplet_format_io (Apache-2.0)

Chiplet Studio is GPL-3.0-or-later (it links KLayout). To keep `.chiplet`
parsing in a permissively licensed layer that adk-tools and other downstream
tools can reuse, the format reader/writer lives in this vendored copy of the
C++ reference library rather than in a hand-written parser inside the GPL host.

This directory is a **verbatim copy** of the C++ reference reader/writer for the
`.chiplet` format:

- **Upstream:** `IHP-GmbH/chiplet-system-spec`, `reference/cpp/`
- **Reader release:** 1.13.1 (`READER_RELEASE`)
- **License:** Apache-2.0 (see the SPDX headers in each file)
- **Content digest:** `3a14837d4aa048fa42f834ffa788ca4047b96cdb27fc2233fac660ddba36c861`

The digest is sha256 over the `sha256sum` listing of the files listed under
"What is vendored", sorted by path. Anyone can recompute it, here or in a
chiplet-system-spec checkout, with no particular commit fetched:

```bash
cd src/formats/chiplet_format_io      # or reference/cpp in chiplet-system-spec
sha256sum include/chiplet_format_io/chiplet_format_io.hpp \
          src/chiplet_format_io.cpp | sha256sum
```

The record identifies the source by release and content, not by commit id. An
upstream commit whose `reference/cpp/` gives the same digest carries exactly
these bytes.

The reader code in these two files is release 1.6's. From 1.6.0 to 1.13.1,
upstream changed only the `READER_RELEASE` line and comments here: the two
reference implementations ship one release number, and the work in those
releases lies outside these files. Reader 1.6 added the parser-attested
`capture_top_level_blocks` API with exact UTF-8 source slices and all-or-nothing
refusal for unsupported capture forms. Existing loader and key-sequence API
signatures remain unchanged; this is not a file-format version bump.

The vendored copy may lag the chiplet-system-spec the ecosystem pins; it may
not lead it. `ci/check_vendored.py` holds the copy to the digest above with no
upstream at all, which proves the copy matches this note and nothing more: an
edit that also rewrites the digest passes it, so a digest change is what a
reviewer holds to a real re-vendor. The contract case in `tests/contracts/`
checks against the pinned export. It always checks that the vendored release
is not newer than the export's. It then looks the vendored files up as blobs in
the export; present, they are upstream bytes. Where the export does not hold
them it skips, and the digest check is the only one that ran. It does not walk
history, so it cannot show a copy taken from off the pinned line.

The C++ generated-mapping writer also retries quoted emission for carriage
returns, including CRLF inside scalar values, while preserving captured flow
bytes. This is not a generalized repair of the legacy hand-authored flow-value
serializer. Vendoring establishes source identity, not qualification of the
Studio Save/recovery integration or its GUI witness.

The reader release is recorded as PROVENANCE, so a human can diff two vendorings.
The only assertion on it is the order above: not newer than the pinned spec's.

What this copy's history establishes is narrower than "never check the version",
and worth stating exactly. The upstream commit "C++ flow_yaml is the source
slice, not a re-serialisation" changed what `ChipletDocument::flow_yaml` MEANS,
from the flow block's value to the source slice including the key line, with
`READER_RELEASE` at 1.1.0 both at that commit and at its parent. Every guard
that could have been written on the number would have been green through it: an
exact match, a minimum, a compatible range. So the conclusion is that a version
guard cannot be the guard against a change of meaning, because a version is the
producer's summary of its own behaviour and a summary can be wrong about the
thing it summarises.

That is not an argument against recording or checking the version at all. A
version check, a file registry and a byte-identity check each answer a question
behaviour tests do not: which upstream this is, whether the set of files is
complete, whether the copy was modified in place. `ci/check_vendored.py` exists
precisely to answer the last one. Those are complements. The rule is only that
none of them may stand in for asserting on what the reader DOES. That is what
the `VendoredReaderContract` tests in `tests/test_chiplet_format.cpp` are for;
they assert against `cfio` directly rather than through `ChipletFormat`, so a
compensating change in this host cannot keep them green.

The behaviours this host depends on are pinned directly instead, in
`tests/test_chiplet_format.cpp` under `VendoredReaderContract`. Those tests assert
against this library rather than through the host's consumer of it, so a
compensating change in the host cannot keep them green. Add one whenever this host
starts relying on a new property of this copy.

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

**Do not edit these files here.** Fix bugs upstream in `chiplet-system-spec`,
then re-vendor: copy *only* the two files above from the upstream
`reference/cpp/` tree and update the release and the content digest in this
note. Do not drag in the upstream
`CMakeLists.txt` or `tests/`; the studio supplies its own build and contract
tests.

The copy is byte-identical to upstream and is meant to stay that way. To check
it by hand, run the command above in a chiplet-system-spec checkout: the same
digest means the same bytes.
There are no local patches and no cherry-picks to re-fold.

Vendoring an Apache-2.0 library into this GPL-3.0-or-later project is
license-compatible; the copied files keep their own Apache-2.0 SPDX headers.

## History: why this copy had drifted

For a while this note described a deferred full re-sync, a
`attachment_surface_z` cherry-pick applied surgically on top of an older
upstream copy, and a handoff of the full re-vendor to another team. All of it
rested on the claim that chiplet-spec HEAD had *removed* `TechDef.stackup`,
which the host still reads (`ChipletFormat.cpp`, `Technology::stackup_path()`).

That claim was wrong. `stackup` was never in chiplet-spec, at any commit, on any
branch: it was added to this copy here, locally, by the File > Import GDS work,
and the deferral was built on the assumption that upstream had taken away
something it had never carried. The re-sync was blocked by a mistake rather than
by a real coupling.

`technologies.<id>.stackup` is now specified upstream (`CHIPLET_FORMAT_SPEC.md`,
Technologies Section) and implemented in the reference reader and writer with a
round-trip test, so this directory is a clean verbatim copy again and the
deferred work is discharged rather than handed on.
