# .chiplet Format Specification

The canonical specification now lives in the permissive spec repository:

**[IHP-GmbH/chiplet-spec](https://github.com/IHP-GmbH/chiplet-spec)** (Apache-2.0); see
`docs/CHIPLET_FORMAT_SPEC.md` there.

That repository is the single source of truth for the `.chiplet` interchange
format: the specification, the JSON schemas, examples, and the dependency-clean
reference reader/writer libraries (`chiplet-format-io`, Python and C++). The
format is intentionally licensed permissively so anyone may implement it under
any license.

This file is kept only as a pointer so existing links keep resolving. Chiplet
Studio's reader (`src/formats/ChipletFormat.*`) does not parse the format itself;
it delegates parsing and validation to a verbatim, Apache-2.0 vendored copy of
the C++ reference library under `src/formats/chiplet_format_io/` (see that
directory's `VENDORED.md` for the upstream commit and re-vendoring rules). The
host then maps the library's plain `ChipletDocument` into `core/Assembly` and
keeps the studio-specific concerns the reference library stays out of: path
resolution, diagnostics, z auto-calculation, techfile auto-load, and flow
parsing.
