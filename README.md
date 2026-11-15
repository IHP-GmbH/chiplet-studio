# Chiplet Studio

Chiplet Studio is the interactive viewer of the IHP Open ADK. It opens a
`.chiplet` assembly file, shows the assembly in 3D, and lets you drill down
into each component's GDS or OASIS layout in a 2D KLayout view. It is alpha
software: its menus and its Python API can still change.

## Use

Chiplet Studio ships in the ADK-Tools image. With that image built, start a
shell in it with `run.sh` from your ADK-Tools clone, then open an assembly:

```bash
chiplet-studio path/to/assembly.chiplet
```

A `.chiplet` file's `flow:` block names programs that the Flow Pipeline panel
runs when you start them. Open only files you trust.

## Documentation

The full documentation is at
https://ihp-open-adk-docs.readthedocs.io/en/latest/tools/04_chiplet_studio.html

## Release

Released as part of the IHP Open ADK. The release page lists the commit of every
repository in each release:
https://ihp-open-adk-docs.readthedocs.io/en/latest/install/00_release.html

## License

GPL-3.0-or-later. See `LICENSE`, and `THIRD-PARTY-LICENSES.md` for the linked
components.
