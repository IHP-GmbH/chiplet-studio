# pipeline_smoke.chiplet

Unedited output of the KiCad plugin's full export (the writers, then
`hyp_to_gds.py --update-chiplet-file`) for the wire-bond demo board, identified
by sha256. Do not edit it here. `ChipletFormatIOPads.LoadRealHypToGdsOutput`
(`tests/test_chiplet_format_io_pads.cpp`) checks this hash first, as
`kPipelineSmokeSha`.

    sha256  26fbd6060f44fe99d8ce8c5170e6c1315517582fde127050a86eb19a44b82a7a

To refresh it, export the board again with the current plugin, copy the output
here, and change the sha256 in both places. The test's other expectations (pad
count, ids, nets, pinned positions, plane and die seats) were read from this
export and must be read again from the new one.

- Board: `interposer_wire_bonding_demo/interposer_wire_bonding_demo.kicad_pcb`
  in the kicad_designs repository, sha256
  `dd78890dc62b128c362d358dfceb3e619931b3bb965b302b19e57fe62d8574bf`.
- Plugin: chiplet_kicad_plugin `70fce9c`.
- Tools: the adk-tools runtime image (tag `runtime-fd13081`), digest
  `sha256:7a9e1759aee9d458e9daf0fa7043ee8fa18273bf589a7533cbc7dd8f04ac6ce2`.
  The attach plane, 10.8103 um, is the interposer PDK's
  `beol.attachment_surface_z_um` in that image.
- Command, in the image:
  `python3 plugin/plugins/chiplet_export/tests/regenerate_wirebond_demo.py --board <board> --output-dir out --allow-drc-fail`.
  The flag only lets the export finish when the assembly DRC fails; here the
  DRC passed and the run exited 0.

The file's paths are relative or `${...}` variables, so it names no host.
