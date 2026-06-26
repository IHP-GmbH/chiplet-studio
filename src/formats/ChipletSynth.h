// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * ChipletSynth.h - Synthesize a minimal .chiplet that wraps a single GDS.
 *
 * Used by File > Import GDS to open a standalone GDS for 3D viewing without
 * authoring a .chiplet by hand: the importer detects the GDS top cell + bbox,
 * fills the spec below, and writes the YAML this produces. The text is then
 * loaded through the normal ChipletFormat::load pipeline, so it is validated by
 * the same strict format_version gate as any other .chiplet. Qt-free on purpose
 * so it is unit-testable without a GUI.
 */

#ifndef CHIPLET_FORMATS_CHIPLET_SYNTH_H
#define CHIPLET_FORMATS_CHIPLET_SYNTH_H

#include <string>

namespace chiplet {

/**
 * Inputs for a single-die import. Paths should be absolute so the synthesized
 * .chiplet resolves them regardless of where it is written.
 */
struct SingleGdsImportSpec {
    std::string gdsPath;          // absolute GDS path -> components[0].layout
    std::string topCell;          // GDS top cell -> top_cell (may be empty)
    double widthUm = 0.0;         // die bbox width  (um); <=0 -> sensible default
    double heightUm = 0.0;        // die bbox height (um); <=0 -> sensible default
    double thicknessUm = 200.0;   // synthetic die thickness (um); no 2D source

    // Technology. A non-empty techId names a supported PDK (sg13g2, sg13cmos5l,
    // sky130, gf180, intm4tm2) whose stackup + colors the studio resolves from
    // the id alone. An empty techId means a custom/unsupported PDK: the emitted
    // technology is "custom_imported", carrying the user-supplied layer
    // properties (.lyp) and/or stackup YAML so the render path can use them.
    std::string techId;           // supported id, or "" for custom
    std::string customLyp;        // verbatim .lyp path (custom); may be empty
    std::string customStackup;    // verbatim stackup YAML path (custom); may be empty

    std::string assemblyName = "Imported GDS";
    std::string componentId = "imported_die";
};

/**
 * Build the .chiplet YAML text for the spec. The result is accepted by
 * ChipletFormat::load. Optional fields (top_cell, layer_properties, stackup)
 * are emitted only when non-empty.
 */
std::string synthesizeSingleGdsChiplet(const SingleGdsImportSpec& spec);

/// Technology id used for a custom/unsupported-PDK import.
extern const char* const kCustomImportTechId;

}  // namespace chiplet

#endif  // CHIPLET_FORMATS_CHIPLET_SYNTH_H
