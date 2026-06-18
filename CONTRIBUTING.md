# Contributing to Chiplet Studio

Thanks for your interest in contributing.

## License of contributions

Chiplet Studio is licensed under **GPL-3.0-or-later** (see `LICENSE`). By
contributing, you agree that your contributions are licensed under the same
terms. This is required because the project links KLayout (GPL-3.0-or-later);
see `THIRD-PARTY-LICENSES.md`.

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

1. Fork the repo (or branch, if you have write access). Branch from `main`.
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
6. Follow `docs/CODE_STYLE.md`.
7. Open a pull request against `main` describing the change and how you tested it.

## Running the tests

`./scripts/build-docker.sh` only compiles; it does not run the tests. Run them
yourself before opening a PR.

### C++ suite (ctest)

The GoogleTest suite (`build/tests/chiplet_tests`, driven by ctest) needs the
KLayout shared libraries on the loader path and Qt's offscreen platform for
headless rendering. Run it inside the same Docker image used for the build:

```bash
docker run --rm \
    -v "$PWD:/workspace" -w /workspace/build \
    -u "$(id -u):$(id -g)" \
    -e QT_QPA_PLATFORM=offscreen \
    -e LD_LIBRARY_PATH=/workspace/extern/klayout/bin-release:/workspace/extern/klayout/bin-release/db_plugins \
    chiplet-studio-build \
    ctest --output-on-failure
```

A few GUI tests that need a physical display skip gracefully in this mode.
For a quick local run of the raw binary (still headless), `./scripts/test-headless.sh`
sets `QT_QPA_PLATFORM=offscreen` and execs `build/tests/chiplet_tests`; pass
GoogleTest filters through, for example
`./scripts/test-headless.sh --gtest_filter=KLayout*`.

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
`src/formats/chiplet_format_io/`, a verbatim copy of `IHP-GmbH/chiplet-spec`
(`reference/cpp/`, Apache-2.0). `src/formats/ChipletFormat.*` is a thin consumer
that maps the parsed document into the studio's `core/Assembly`.

Do not edit the vendored files in place. Fix format bugs upstream in
`chiplet-spec`, then re-vendor and bump the commit hash recorded in
`src/formats/chiplet_format_io/VENDORED.md`.

The format and its coordinate conventions are a contract surface consumed by
adk-tools (which embeds this repo as a submodule). The contract docs
(`docs/CHIPLET_FORMAT_SPEC.md`, `docs/coord_frame_contract.md`,
`docs/interconnect_render_contract.md`) are backed by tests
(`tests/test_coord_frame_contract.cpp`, `tests/test_chiplet_format*.cpp`). If a
change touches the format or coordinate behavior, update those docs and tests
together so the downstream contract stays accurate.

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
