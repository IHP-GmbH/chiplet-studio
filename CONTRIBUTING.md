# Contributing to Chiplet Studio

Thanks for your interest in contributing.

## License of contributions

Chiplet Studio is licensed under **GPL-3.0-or-later** (see `LICENSE`). By
contributing, you agree that your contributions are licensed under the same
terms. The project links KLayout (GPL) and Qt 6 (LGPL-3.0); see
`THIRD-PARTY-LICENSES.md`.

We use the **Developer Certificate of Origin (DCO)** rather than a CLA. There is
no copyright assignment: you keep the copyright on your work and license it to
the project under GPL-3.0-or-later.

## Sign your commits (DCO)

Every commit must carry a `Signed-off-by` line certifying the DCO below. Add it
automatically with:

```bash
git commit -s -m "your message"
```

which appends:

```
Signed-off-by: Your Name <your.email@example.com>
```

Use your real name and a reachable email. Commits without a sign-off cannot be
merged.

## Workflow

1. Fork the repo (or branch, if you have write access). Branch from `dev`, which
   is where work lands. `main` moves only at a release fold.
2. Build in Docker; host libraries may differ from the container:
   ```bash
   ./scripts/build-docker.sh
   ```
   This builds KLayout (if not already present) and Chiplet Studio. The binary
   lands at `build/chiplet-studio`.
3. Run the tests (see "Running the tests" below). New behavior needs a test;
   bug fixes should add a regression test.
4. Keep changes focused; one logical change per commit. Commit messages in
   English, clear and concise.
5. New source files under `src/` must start with the SPDX header:
   ```cpp
   // SPDX-FileCopyrightText: <year> <your name or IHP GmbH>
   // SPDX-License-Identifier: GPL-3.0-or-later
   ```
   Do not add headers to third-party code under `extern/`.
6. Match the surrounding code. There is no formatter config in this repo, so
   the local style of the file you are editing is the rule.
7. Open a pull request against `dev` describing the change and how you tested it.
   One check is required to merge, `ci-gate`. It collects the jobs in
   `.github/workflows/ci.yml` and finishes in about a minute. Those jobs check
   what a compiler cannot: that every source is named in a `CMakeLists`, and
   that the vendored `chiplet_format_io` is still the verbatim copy it declares.
   The build and the test suites are verified separately, on the machine that
   has the Docker cache for them, so a green `ci-gate` is necessary and not
   sufficient.

## Running the tests

`./scripts/build-docker.sh` only compiles; it does not run the tests. Run them
yourself before opening a PR.

### C++ suite (ctest)

Run it with `./scripts/run-gate.sh`. Do not hand-roll the `docker run`.

The suite has tests that decline to run when an input is missing rather than
fail, so a partial invocation still prints `100% tests passed` while quietly not
executing them. This project reported ten "known display-only skips" for months;
nine were nothing of the kind, and the two that certify save safety were among
them. The inputs, and what dropping each one silences:

- **A non-root uid.** The two `AtomicSave` tests that pin the atomic-save
  contract skip on `geteuid() == 0`, so a gate run as root never checks that a
  failed save leaves the original intact.
- **`QT_QPA_PLATFORM=xcb`, under `xvfb-run`.** `nativeViewExpected()` in
  `tests/test_klayout_2dview.cpp` returns false for `offscreen` and `minimal`,
  so exporting either makes the native-view test skip whatever Xvfb provides.
  This is not a physical-display requirement.
- **`WIREBOND_DEMO_CHIPLET`.** Six `CoordFrameContractWirebondDemo` tests and
  one `RenderModeTest`.
- **The interposer, interconnect and gds_to_kicad roots.** Stackup resolution,
  CuPillar and bump geometry, layer colours and the real-GDS load path.
  Unresolved roots mask each other: binding the first only moves the error to
  the next, so bind them together.

The script binds all of it and treats a skip as a failure unless you pass
`--allow-skips`.

Its verdict does not come from the summary line. `100% tests passed, 0 tests
failed out of N` is self-consistent for an empty suite, for one that registered
fewer tests than it should have, and for one where the case that mattered
skipped, because N is whatever ctest found. So the run produces two independent
artifacts, `ctest --show-only=json-v1` for what the build resolved and
`ctest --output-junit` for what ran, and `ci/reconcile_gate.py` requires exact
set identity between them, both non-empty. Set identity cannot see a test the
build never registered, so `ci/gate_required_tests.txt` names the cases that
must also be present and pass; add to it when you add a test whose silent
absence would be expensive.

The capture holds the runner's own bytes and their sha256, the docker argv, the
client exit and the container exit as separate facts, cleanup verification,
source inventories before and after, and product inventories before and after
the build. A capture is written once; the run aborts rather than overwrite one.
It also aborts on a dirty tree, on a build configured for a different source or
with Python OFF, and on a failure to hash the binaries it is about to run.

It builds and runs in the adk-tools `studio-builder` image, the environment the
product image is built in, and links and loads that image's own KLayout; the
run checks that this tree's KLayout headers are the image's and that the
cross-reader PyYAML is the pinned version. `./scripts/configure-gate.sh`
prepares `build/` in the same image. It is the only step that reaches the
network, and it fails unless every fetched dependency is at the commit its
`FetchContent_Declare` pins. The gate itself runs offline.

Point both at the image, the sibling repositories and the fixture:

```bash
export CHIPLET_GATE_IMAGE=adk-tools:studio-builder-<sha>
export CROSS_READER_PYTHON=/opt/adk-tools/venv/bin/python3
export CHIPLET_ADK_ROOT=..            # holds interposer/, interconnect_pdk/, gds_to_kicad/
export WIREBOND_DEMO_CHIPLET_HOST=/path/to/interposer_wire_bonding_demo.chiplet
./scripts/configure-gate.sh           # once, into an absent build/
./scripts/run-gate.sh
```

If an input is genuinely unavailable to you, `--allow-skips` runs anyway and
says in the log what was not bound. A green from that run is a narrower claim
than a green from a complete one; say which you ran when you report it.

For a quick local run of the raw binary while iterating,
`./scripts/test-headless.sh` execs `build/tests/chiplet_tests` and passes
GoogleTest filters through, for example
`./scripts/test-headless.sh --gtest_filter=KLayout*`. It sets
`QT_QPA_PLATFORM=offscreen`, so it is a development convenience and not the
gate.

### Documentation pages

`docs/` holds the pages the ADK documentation includes. The Python API
reference there is generated: after a build that changes the bindings in
`src/scripting/PyBindings.cpp`, regenerate it in the image the build used and
commit the result with the change:

```bash
python3 scripts/gen-api-reference.py build/python > docs/reference/python-api.md
```

### Contract checks without a build (pytest)

The checks that need no compiler, no Qt and no KLayout (the source list, the
demo launchers, the vendored-copy checker) also run as pytest cases from
`tests/contracts/`, for a host with no build:

```bash
python3 -m venv .venv && .venv/bin/pip install pytest
.venv/bin/python -m pytest
```

One case, marked `needs_sibling_sources`, compares the vendored reader with a
chiplet-system-spec checkout at `$HCDP_ROOT/chiplet-spec`. It skips when
`HCDP_ROOT` is unset or names no such checkout, and fails if the vendored
release is newer than the checkout's. It then looks the vendored files up as
blobs in the checkout and skips if they are not there, which is normal once the
pinned spec has moved past the vendored release. Deselect it with
`-m "not needs_sibling_sources"`.

### Python binding tests

The Python bindings are tested separately; they are **not** wired into ctest.
After a build, run them with `build/python` on `PYTHONPATH`:

```bash
./tests/run_python_tests.sh
```

These exercise the `chiplet_studio` module (assembly/component manipulation,
save/load roundtrip). Run them whenever you touch `src/scripting/PyBindings.cpp`.

## The `.chiplet` format and downstream contract

`.chiplet` parsing and validation live in the vendored reference library at
`src/formats/chiplet_format_io/`, a verbatim copy of
`IHP-GmbH/chiplet-system-spec` (`reference/cpp/`, Apache-2.0).
`src/formats/ChipletFormat.*` is a thin consumer that maps the parsed document
into the studio's `core/Assembly`.

Do not edit the vendored files in place. Fix format bugs upstream in
`chiplet-system-spec`, then re-vendor and update the reader release and the
content digest recorded in `src/formats/chiplet_format_io/VENDORED.md`.

CI runs `ci/check_vendored.py`, which checks the copy against the content
digest `VENDORED.md` records, with no upstream checkout:

```bash
python3 ci/check_vendored.py
```

It proves the copy matches the note, not that the note matches upstream. An
in-place edit fails it unless the digest line is rewritten too, so review a
digest change as a re-vendor: the new digest must come from the upstream
files. An upstream change does not turn this red; noticing that the copy has
aged is a separate, deliberate re-vendor.

The format and its coordinate conventions are a contract surface consumed by
adk-tools (which embeds this repo as a submodule). The two contract documents,
`docs/CHIPLET_FORMAT_SPEC.md` and `docs/coord_frame_contract.md`, live in
[IHP-GmbH/chiplet-system-spec](https://github.com/IHP-GmbH/chiplet-system-spec),
not here; this repository has no `docs/` directory. They are backed on this side
by `tests/test_coord_frame_contract.cpp` and `tests/test_chiplet_format*.cpp`.
If a change touches the format or coordinate behavior, the tests here and the
document there have to move together, or the downstream contract stops being
accurate.

## Developer Certificate of Origin 1.1

```
Developer Certificate of Origin
Version 1.1

Copyright (C) 2004, 2006 The Linux Foundation and its contributors.
1 Letterman Drive
Suite D4700
San Francisco, CA, 94129

Everyone is permitted to copy and distribute verbatim copies of this
license document, but changing it is not allowed.


Developer's Certificate of Origin 1.1

By making a contribution to this project, I certify that:

(a) The contribution was created in whole or in part by me and I
    have the right to submit it under the open source license
    indicated in the file; or

(b) The contribution is based upon previous work that, to the best
    of my knowledge, is covered under an appropriate open source
    license and I have the right under that license to submit that
    work with modifications, whether created in whole or in part
    by me, under the same open source license (unless I am
    permitted to submit under a different license), as indicated
    in the file; or

(c) The contribution was provided directly to me by some other
    person who certified (a), (b) or (c) and I have not modified
    it.

(d) I understand and agree that this project and the contribution
    are public and that a record of the contribution (including all
    personal information I submit with it, including my sign-off) is
    maintained indefinitely and may be redistributed consistent with
    this project or the open source license(s) involved.
```
