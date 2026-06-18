# .chiplet Format Specification

The canonical specification now lives in the permissive spec repository:

**[IHP-GmbH/chiplet-spec](https://github.com/IHP-GmbH/chiplet-spec)** — see
`docs/CHIPLET_FORMAT_SPEC.md` (Apache-2.0).

That repository is the single source of truth for the `.chiplet` interchange
format — the specification, the JSON schemas, examples, and the dependency-clean
reference reader/writer libraries (`chiplet-format-io`, Python and C++). The
format is intentionally licensed permissively so anyone may implement it under
any license. This file is kept only as a pointer so existing links keep
resolving; Chiplet Studio's reader (`src/formats/ChipletFormat.*`) consumes the
vendored copy of that reference library under `src/formats/chiplet_format_io/`.
