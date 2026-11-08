// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * ChipletSynth.h - Build the in-memory assembly that wraps a single GDS.
 *
 * Used by File > Import GDS to open a standalone GDS for 3D viewing without
 * authoring a .chiplet by hand: the importer detects the GDS top cell + bbox,
 * fills the spec below, and this builds the Assembly the normal writer then
 * serializes. Qt-free on purpose so it is unit-testable without a GUI.
 *
 * It builds a MODEL and not text, and that is the point of the file. This used
 * to emit YAML directly through a private quoter that escaped a backslash and a
 * double quote and nothing else, which is a second YAML grammar living next to
 * the one in ChipletFormat.cpp. A path carrying a newline, legal on Linux and
 * reachable from a file dialog, was written raw inside a quoted scalar: the
 * document then read back with the newline folded to a space under PyYAML and
 * was NOT SPLITTABLE for the reference reader, and this was the only path in
 * the program that could write such a file, beside the GDS where every
 * *.chiplet glob finds it. Going through the model means one serializer, one
 * escaping rule, one admission check and one atomic destination for every
 * .chiplet this program writes.
 */

#ifndef CHIPLET_FORMATS_CHIPLET_SYNTH_H
#define CHIPLET_FORMATS_CHIPLET_SYNTH_H

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace chiplet {

class Assembly;

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
 * Build the assembly for the spec. Never returns null. Optional fields
 * (top_cell, layer_properties, stackup) are set only when non-empty, so the
 * writer's emit-only-what-was-declared rule keeps them out of the document.
 *
 * Whether a given string can be REPRESENTED in the format is still the writer's
 * question, decided once, in one place; nothing here second-guesses it. A path
 * carrying a newline, a quote or a `#` is data, and it reaches the document.
 *
 * Two things are refused here, and neither is a representability question,
 * which is why they are not the writer's to answer. A dimension that is not a
 * finite number is a measurement that went wrong, and the format states `.inf`
 * quite happily. A NUL inside gdsPath, customLyp or customStackup round-trips
 * through both readers exactly and still cannot name a file, because ::open
 * stops at the NUL and opens something else. Both throw ChipletFormatException
 * with FailureKind::Other, which writeSingleGdsImport turns into Refused.
 *
 * The refusal lives HERE rather than at writeSingleGdsImport's door on purpose.
 * This function is public and is called directly, by the import writer and by
 * tests, and a guard at the entry point would be invisible to every one of
 * those callers; the hostile-input tests in particular build and save without
 * going through the entry point at all. A check the test harness walks past is
 * a shape that keeps recurring.
 */
std::unique_ptr<Assembly> buildSingleGdsAssembly(const SingleGdsImportSpec& spec);

/**
 * What happened when an import was written.
 *
 * Refused and Failed are separate because they must not be answered the same
 * way. Refused means the document could not be written AT ALL, so retrying it
 * in another directory would refuse again; Failed means this destination could
 * not be written, which a different destination can fix. Conflating them is how
 * a refusal ends up copied into a second directory along with its rescue file.
 */
struct ImportWriteResult {
    enum class Outcome {
        Written,            ///< at `path`, the caller's preferred destination
        WrittenToFallback,  ///< at `path`, because the preferred one could not be written
        Refused,            ///< nothing written anywhere; `message` says why
        Cancelled,          ///< the caller declined a new name; nothing written
        Failed,             ///< could not even prepare the import; `message` says why
    };
    Outcome outcome = Outcome::Failed;
    std::string path;
    std::string message;
};

/**
 * Build the assembly for `spec` and write it as a .chiplet.
 *
 * The destination must be FRESH. `preferredPath` is used only if nothing exists
 * there, a dangling symlink included; otherwise `chooseAnotherName` is called
 * with the occupied path and may return a different path (checked the same way,
 * as often as it takes) or nothing, which cancels. This function offers no
 * overwrite: replacing a file the user did not name, and following a link to
 * somewhere else, are two different consent questions and neither is answered
 * by a yes or no on a path this program chose.
 *
 * A document that cannot be written at all is refused without leaving anything
 * in the caller's directory, rescue file included. Qt-free so the whole policy
 * is testable without a GUI; the caller supplies the dialog.
 *
 * Two callers. src/ui/MainWindow.cpp is the product one. The second is the
 * `write_single_gds_import` binding in src/scripting/PyBindings.cpp, which
 * exists so tests/cross_reader_check.py can read THIS function's output with a
 * YAML implementation other than the writer's own; a check that re-saved a
 * document instead would witness agreement about something resembling an
 * import. Changing this signature means changing that binding, and the binding
 * repeats no default of its own so a changed default here cannot leave the
 * check green on a value nothing produces.
 */
ImportWriteResult writeSingleGdsImport(
    const SingleGdsImportSpec& spec,
    const std::string& preferredPath,
    const std::function<std::optional<std::string>(const std::string& occupied)>&
        chooseAnotherName);

/// Technology id used for a custom/unsupported-PDK import.
extern const char* const kCustomImportTechId;

}  // namespace chiplet

#endif  // CHIPLET_FORMATS_CHIPLET_SYNTH_H
