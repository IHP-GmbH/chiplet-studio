# gf180mcu.lyp

Verbatim copy of `gf180mcuD/libs.tech/klayout/tech/gf180mcu.lyp` from
**wafer-space/gf180mcu**, commit `7636804` (the file last changed in `f779675`).
Apache-2.0, GlobalFoundries PDK Authors.

    sha256  4d0e42a66b16f598bc4c401c193e60b8aeabc45bba7ff96d2f93e84cf6acb31f

The sheet names each layer's fill `<layer>_Dummy` on datatype 4, in the
`<source>` text itself (`Metal1_Dummy 34/4@1`) with an empty `<name/>`.

`TechResolve.BundledGf180mcuMarksEveryFillLayerItsPdkSheetNames` asserts the
hash, reads the sheet with Studio's own parser, and checks that every
`_Dummy` layer it names is `purpose: filler` in the bundled
`configs/stackups/gf180mcu.yaml`, at its drawn layer's z and height, and that
nothing else is. Do not edit the copy here: re-copy it from the PDK and update
the hash.
