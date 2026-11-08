// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * ChipletSynth.cpp - Implementation
 */

#include "ChipletSynth.h"

#include "ChipletFormat.h"

#include "core/Assembly.h"
#include "core/Component.h"
#include "core/LayerStackup.h"
#include "core/Technology.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace chiplet {

const char* const kCustomImportTechId = "custom_imported";

namespace {

// The two things a spec can say that no document can carry. Both throw with
// FailureKind::Other, stated rather than defaulted, because the routing is the
// point: Other is the arm that answers Refused and never writes the same
// document somewhere else, and a reader of this file should be able to see that
// without opening ChipletFormat.h to find out what the default is.

// A dimension that is not a finite number. Refused, not substituted, and that
// is the whole distinction between this and the `<= 0` clause below it: zero
// means the importer had nothing to measure and a default is the honest answer,
// while a NaN or an infinity is a measurement that went wrong. Substituting
// 1000.0 for it records a size nobody measured, and letting an infinity through
// writes `width: .inf`, which the format states, both readers read back as
// infinity, and no mesh builder can use.
void rejectNonFiniteDimension(const char* field, double value)
{
    if (std::isfinite(value)) return;
    throw ChipletFormatException(
        std::string("Import spec ") + field + " is " + std::to_string(value) +
            ", which is not a finite size. Nothing was written. Give a finite "
            "value, or leave it at 0 to take the default.",
        0, "", FailureKind::Other);
}

// A NUL byte in a field that names a file. See path_has_nul in ChipletFormat.h
// for why this is worth refusing even though the format represents it
// faithfully: the document is fine and the path is not, because ::open stops at
// the NUL and opens something else.
//
// Only the three fields that are actually filesystem paths. topCell, techId,
// componentId and assemblyName are names the format carries, not paths anything
// opens, and refusing a byte in them would be a different rule with a different
// reason behind it.
void rejectNulInPath(const char* field, const std::string& value)
{
    if (!path_has_nul(value)) return;
    throw ChipletFormatException(
        std::string("Import spec ") + field + " contains a NUL byte, so it "
            "cannot name a file. Nothing was written.",
        0, "", FailureKind::Other);
}

}  // namespace

std::unique_ptr<Assembly> buildSingleGdsAssembly(const SingleGdsImportSpec& spec)
{
    // Admission first, and before the substitutions below, because a
    // substitution applied to a value that should have been refused is how the
    // refusal disappears: `NaN > 0.0` is false, so without this the width the
    // caller could not measure would quietly become 1000.0 and the document
    // would state it as a fact.
    rejectNonFiniteDimension("width", spec.widthUm);
    rejectNonFiniteDimension("height", spec.heightUm);
    rejectNonFiniteDimension("thickness", spec.thicknessUm);
    rejectNulInPath("gdsPath", spec.gdsPath);
    rejectNulInPath("customLyp", spec.customLyp);
    rejectNulInPath("customStackup", spec.customStackup);

    // Geometry: bbox drives width/height; keep everything strictly positive so
    // the mesh builder never falls back to symbolic box geometry.
    const double width = spec.widthUm > 0.0 ? spec.widthUm : 1000.0;
    const double height = spec.heightUm > 0.0 ? spec.heightUm : 1000.0;
    const double thickness = spec.thicknessUm > 0.0 ? spec.thicknessUm : 200.0;

    const std::string techKey =
        spec.techId.empty() ? std::string(kCustomImportTechId) : spec.techId;
    const std::string componentId =
        spec.componentId.empty() ? std::string("imported_die") : spec.componentId;
    const std::string assemblyName =
        spec.assemblyName.empty() ? std::string("Imported GDS") : spec.assemblyName;

    auto assembly = std::make_unique<Assembly>();
    assembly->set_name(assemblyName);
    assembly->set_units("um");

    auto tech = std::make_unique<Technology>(techKey);
    if (!spec.customLyp.empty()) {
        tech->set_layer_properties_path(spec.customLyp);
    }
    if (!spec.customStackup.empty()) {
        tech->set_stackup_path(spec.customStackup);
    }
    tech->set_dbu(0.001);
    assembly->add_technology(std::move(tech));

    // Die or Interposer by the technology, from the registry's role table and
    // nowhere else: the dialog's "Imports as" line reads the same table.
    auto component = std::make_unique<Component>(
        componentId, BlenderGDSConfigs::importedComponentType(spec.techId));
    component->set_technology(techKey);
    component->set_layout_path(spec.gdsPath);
    if (!spec.topCell.empty()) {
        component->set_top_cell(spec.topCell);
    }
    component->set_dimensions(Dimensions3D{width, height, thickness});

    // bbox_center, NOT the die convention gds_origin, and the difference is
    // half a die. The general rule (dies anchor on gds_origin) describes a
    // die placed in an assembly, where its GDS origin is the design
    // reference the position is measured from. This path is not that: it
    // wraps a single loose GDS for 3D viewing, the position is left at the
    // model's zero, and the dimensions above ARE the GDS bounding box. A
    // bbox-sized box has to be centred on the bbox; anchoring it on the
    // GDS origin would shift every imported layout drawn in the first
    // quadrant by half its own size.
    //
    // Set explicitly rather than left to the model's default, because the
    // reason above is a decision about this document and not an accident of
    // which value Component happens to start at. The writer emits the anchor
    // unconditionally, so it reaches the file either way.
    component->set_anchor(Anchor::BboxCenter);

    // position is deliberately not set. The model starts at {0,0,0}, the writer
    // suppresses an all-zero position, the reference writer omits it identically
    // and the reference reader only reads the key when it is present, so the
    // document round-trips to the same coordinates with or without it. This is
    // NOT the anchor case above: there the absent-key default is the reader's
    // choice and could change, here every party already agrees on zero.

    assembly->add_component(std::move(component));
    return assembly;
}

namespace {

// A private working directory, mode 0700, created by mkdtemp rather than by
// naming a path and hoping. The old import fallback built a name from a UUID and
// wrote it straight into a shared /tmp: unpredictable enough that nothing could
// pre-place a symlink on it, but with no mode of its own and no directory the
// import could call its own. mkdtemp gives both.
std::string makePrivateWorkDir(std::string& error)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path base = fs::temp_directory_path(ec);
    if (ec) {
        error = "no temporary directory is available: " + ec.message();
        return {};
    }
    std::string tmpl = (base / "chiplet-import-XXXXXX").string();
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    if (::mkdtemp(buf.data()) == nullptr) {
        error = "could not create a working directory under " + base.string();
        return {};
    }
    return std::string(buf.data());
}

// The fallback write, and the only place this program puts an import somewhere
// the user did not name. Reached only after a confirmed write failure at the
// destination they did name, never after a refusal.
ImportWriteResult writeToPrivateDirectory(const Assembly& assembly,
                                          const std::string& failedPath,
                                          const std::string& why)
{
    namespace fs = std::filesystem;
    ImportWriteResult result;

    std::string error;
    const std::string workDir = makePrivateWorkDir(error);
    if (workDir.empty()) {
        result.outcome = ImportWriteResult::Outcome::Failed;
        result.message = why + "\nThe fallback could not be prepared either: " + error;
        return result;
    }

    // A destination with no filename component would aim this at the working
    // directory itself.
    const std::string name = fs::path(failedPath).filename().string();
    const fs::path fallback =
        fs::path(workDir) / (name.empty() ? std::string("import.chiplet") : name);

    SaveRequest request;
    request.rejected_output = RejectedOutput::Suppress;
    try {
        ChipletFormat format;
        (void)format.save_snapshot(assembly, fallback.string(), request);
    } catch (const std::exception& e) {
        std::error_code ec;
        fs::remove_all(workDir, ec);
        result.outcome = ImportWriteResult::Outcome::Failed;
        result.message = why + "\nThe fallback could not be written either: " + e.what();
        return result;
    }

    result.outcome = ImportWriteResult::Outcome::WrittenToFallback;
    result.path = fallback.string();
    result.message = why;
    return result;
}

}  // namespace

ImportWriteResult writeSingleGdsImport(
    const SingleGdsImportSpec& spec,
    const std::string& preferredPath,
    const std::function<std::optional<std::string>(const std::string& occupied)>&
        chooseAnotherName)
{
    ImportWriteResult result;
    std::unique_ptr<Assembly> assembly;

    // One request, one write, at the destination the caller actually named.
    //
    // An earlier version wrote the document into a private directory first, to
    // find out whether it could be written at all without leaving a
    // <path>.rejected beside a GDS or inside a PDK checkout. That whole
    // rehearsal is now one field. Suppress says a refusal leaves no
    // rejected-text file, and a refusal is decided during serialization, before
    // anything is created at the destination, so a refused import leaves the
    // caller's directory exactly as it found it without a trial run. The
    // ordinary case costs one serialization and one write instead of two.
    SaveRequest request;
    request.rejected_output = RejectedOutput::Suppress;

    std::string outPath = preferredPath;
    for (;;) {
        try {
            // Built inside the try rather than before the loop, so a spec that
            // cannot describe a die reaches the same FailureKind switch as a
            // document the writer refuses. Both are the document being wrong,
            // both must answer Refused, and there is one place that decides
            // that. Building it before the try instead was the shape that let a
            // spec refusal escape this function entirely, past every outcome it
            // defines and past the caller's dialog.
            //
            // Built once. The loop re-enters only to change the DESTINATION,
            // and the spec does not change with it, so a second build would be
            // the same bytes at best and a second refusal of an already refused
            // spec at worst.
            if (!assembly) assembly = buildSingleGdsAssembly(spec);
            ChipletFormat format;
            (void)format.save_snapshot(*assembly, outPath, request);
            result.outcome = ImportWriteResult::Outcome::Written;
            result.path = outPath;
            return result;
        } catch (const ChipletFormatException& e) {
            // No default label on purpose: a fourth kind is a build failure
            // here rather than a silent choice, which is the point of a typed
            // kind over a tag that has to be recognised.
            switch (e.kind()) {
            case FailureKind::DestinationOccupied:
                // Freshness is answered by the write itself rather than by a
                // check taken before it, so there is no interval in which the
                // answer goes stale: an empty SaveRequest publishes with an
                // exclusive create. A name that is taken needs a different
                // NAME, so the caller is asked for one; writing the document
                // into some other directory would answer a question nobody
                // asked and would do it without telling anyone.
                if (const auto chosen = chooseAnotherName
                                            ? chooseAnotherName(outPath)
                                            : std::optional<std::string>{}) {
                    if (chosen->empty()) {
                        result.outcome = ImportWriteResult::Outcome::Cancelled;
                        return result;
                    }
                    outPath = *chosen;
                    continue;
                }
                result.outcome = ImportWriteResult::Outcome::Cancelled;
                return result;

            case FailureKind::DestinationIo:
                // This destination could not take the file. A different one
                // may, and the document is known good because serialization
                // and the reader check both passed before anything was
                // attempted on disk.
                return writeToPrivateDirectory(*assembly, outPath, e.what());

            case FailureKind::Other:
                // The document itself. Never answered by writing the same
                // document somewhere else, which is the whole reason the kinds
                // exist: a refusal copied into a second directory is a document
                // the format rejects, on disk, twice.
                result.outcome = ImportWriteResult::Outcome::Refused;
                result.message = e.what();
                return result;
            }
            // Unreachable while the switch above is exhaustive, and a rethrow
            // rather than a fall-through because falling through would retry
            // the same destination forever.
            throw;
        } catch (const std::exception& e) {
            result.outcome = ImportWriteResult::Outcome::Failed;
            result.message = e.what();
            return result;
        }
    }
}

}  // namespace chiplet
