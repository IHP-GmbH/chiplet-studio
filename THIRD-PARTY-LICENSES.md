# Third-Party Licenses

Chiplet Studio is distributed under **GPL-3.0-or-later** (see `LICENSE`).
Copyright (C) 2026 IHP GmbH.

It links and/or bundles the third-party components listed below. The project
license is GPL-3.0-or-later because Chiplet Studio links **KLayout**, which is
GPL-3.0-or-later: the combined work is a derivative work and must be conveyed
under GPL-3.0-or-later. Every other component is compatible with that license.

When you redistribute Chiplet Studio (source or binary, including the portable
bundle in `dist/`), you must keep these notices and provide the corresponding
source for the copyleft components (KLayout, libgdsto3d, Qt, libstdc++).

## Linked into the Chiplet Studio binary

| Component | License (SPDX) | Linkage | Source / license text |
|-----------|----------------|---------|-----------------------|
| KLayout | `GPL-3.0-or-later` | dynamic (.so) | submodule `extern/klayout`, license at `extern/klayout/LICENSE` |
| GDS3D `libgdsto3d` (built as `chiplet_gds3d`) | `LGPL-2.1-or-later` | static | submodule `extern/GDS3D/libgdsto3d`, per-file headers |
| GDS3D `math/` (Paul Baker) | `BSD-3-Clause` | static | `extern/GDS3D/math/License.txt` |
| Clipper (Angus Johnson) | `BSL-1.0` | static | header in `extern/GDS3D/libgdsto3d/clipper/clipper.cpp` |
| `chiplet_format_io` (chiplet-spec reference reader/writer) | `Apache-2.0` | static | vendored in `src/formats/chiplet_format_io/`, per-file SPDX headers; see `VENDORED.md` |
| Qt 6 | `LGPL-3.0-only` | dynamic (.so) | https://doc.qt.io/qt-6/lgpl.html |
| yaml-cpp | `MIT` | dynamic | https://github.com/jbeder/yaml-cpp |
| nlohmann/json | `MIT` | header-only | https://github.com/nlohmann/json |
| pybind11 | `BSD-3-Clause` | header-only | https://github.com/pybind/pybind11 |
| CPython (`libpython3`) | `Python-2.0` | dynamic (embedded) | https://docs.python.org/3/license.html |

Notes:
- **KLayout** is the copyleft driver. It carries no linking exception, so the
  whole combined binary is GPL-3.0-or-later.
- **GDS3D**: the top-level `extern/GDS3D/LICENSE.txt` is GPLv2 and covers the
  GDS3D *application*. The *library* sources we actually compile
  (`libgdsto3d/*.cpp`) are headed **LGPL-2.1-or-later**; the per-file headers
  govern. LGPL-2.1-or-later is GPL-3.0-compatible.
- **Qt** is used under LGPL-3.0 and linked dynamically; the portable bundle ships
  Qt as shared libraries, satisfying the LGPL relink requirement.
- **`chiplet_format_io`** is a verbatim copy of the `.chiplet` reference
  reader/writer from `IHP-GmbH/chiplet-spec` (`reference/cpp/`), compiled straight
  into the binary; `src/formats/ChipletFormat.cpp` delegates parsing and
  validation to it. It is permissive Apache-2.0, which is GPL-3.0-compatible, so
  it adds no corresponding-source obligation beyond the existing copyleft set. Do
  not edit the vendored files in place; fix bugs upstream and re-vendor (see
  `src/formats/chiplet_format_io/VENDORED.md`).

## Bundled additionally in the portable distribution

`scripts/build-portable.sh` ships a software-OpenGL stack and runtime libraries
so the app runs without Docker or system dependencies. The closure pulls in the
libraries below (exact set depends on the build host). All are GPL-3.0-compatible.

| Component | License (SPDX) |
|-----------|----------------|
| libstdc++ / libgcc_s | `GPL-3.0-or-later WITH GCC-exception-3.1` |
| Mesa (llvmpipe, `swrast_dri`, `libGLX_mesa`, `libglapi`) | `MIT` |
| LLVM (`libLLVM`) | `Apache-2.0 WITH LLVM-exception` |
| glvnd (`libGL`, `libGLX`, `libGLdispatch`, `libOpenGL`) | `MIT` |
| libdrm | `MIT` |
| zlib (`libz`) | `Zlib` |
| libpng | `Libpng` |
| FreeType (`libfreetype`) | `FTL` (used as the FreeType License, not GPLv2) |
| HarfBuzz (`libharfbuzz`) | `MIT` |
| Fontconfig (`libfontconfig`) | `MIT` |
| ICU (`libicu*`, if present) | `ICU` |
| DejaVu fonts | `Bitstream-Vera` + public domain |

## Compatibility summary

All linked and bundled components are compatible with **GPL-3.0-or-later**, which
is therefore both the required and the sufficient license for the combined work.
A purely permissive (MIT/BSD) or proprietary release is not possible while
Chiplet Studio links KLayout.
