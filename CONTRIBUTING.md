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
2. Build and test in Docker (host libraries may differ from the container):
   ```bash
   ./scripts/build-docker.sh
   ```
3. Keep changes focused; one logical change per commit. Commit messages in
   English, clear and concise.
4. New source files under `src/` must start with the SPDX header:
   ```cpp
   // SPDX-FileCopyrightText: <year> <your name or IHP GmbH>
   // SPDX-License-Identifier: GPL-3.0-or-later
   ```
   Do not add headers to third-party code under `extern/`.
5. Follow `docs/CODE_STYLE.md`.
6. Open a pull request against `main` describing the change and how you tested it.

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
