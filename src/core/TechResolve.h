// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * TechResolve.h - Which layer sheet and which stackup a technology is drawn with
 *
 * One answer for both views. Each function says which rung of its ladder won
 * and, when a declared file was not used, why, so the views can put that on
 * screen instead of falling back silently.
 */

#ifndef CHIPLET_CORE_TECHRESOLVE_H
#define CHIPLET_CORE_TECHRESOLVE_H

#include "LayerStackup.h"

#include <cstddef>
#include <string>
#include <vector>

namespace chiplet {

class Assembly;
class Component;
class Technology;

enum class SheetSource {
    Declared,  // the technology's own layer_properties
    BuiltIn,   // Studio's bundled sheet for the technology id
    None,      // neither: KLayout default styling
};

struct TechSheet {
    std::string techId;
    std::string path;      // the .lyp to load; empty for SheetSource::None
    SheetSource source = SheetSource::None;
    std::string problem;   // why a declared sheet was not used; empty otherwise
};

// The 2D sheet: the declared one, else the bundled one, else none.
// `tech` may be null (a component naming an undeclared technology).
TechSheet resolveTechnologySheet(const Technology* tech, const std::string& techId);

enum class StackupSource {
    Declared,    // the technology's own stackup
    BuiltIn,     // Studio's bundled stackup for the technology id
    NotModeled,  // neither: the caller derives one (from the .lyp, or generic)
};

struct TechStackup {
    LayerStackup stackup;  // filled for Declared and BuiltIn
    StackupSource source = StackupSource::NotModeled;
    std::string path;      // the file read, for Declared and BuiltIn
    std::string problem;   // why a declared stackup was not used; empty otherwise
};

// The modelled stackup: the declared one, else the bundled one. Interconnect
// fragments are not merged here; that is a 3D seating concern.
TechStackup resolveTechnologyStackup(const Technology* tech, const std::string& techId);

// Where dies mount on an interposer: the attachment_surface_z the file
// declares. A file with none has no mount plane, and Studio, as a viewer,
// seats on dimensions.thickness instead and says so (placementNotes). No
// stackup or PDK value stands in for the file's plane.
double interposerMountPlane(const Component& interposer);

// Merge the interconnect bodies for `keys` into an interposer's stackup, the
// one merge seating and the 3D view both make. The fragments rebase on
// interposerMountPlane, never on the stackup's own plane. A fragment whose
// heights are not relative to the attachment surface is left out (its key
// goes to leftOut when given), since it carries one interposer's plane inside
// it. Returns the
// number of body layers merged.
std::size_t mergeBodiesOnInterposer(LayerStackup& stackup, const Component& interposer,
                                    const std::vector<std::string>& keys,
                                    std::vector<std::string>* leftOut = nullptr);

// The stackup 3D draws a technology with, before any per-component change.
struct DrawnStackup {
    LayerStackup stackup;
    TechStackup resolved;          // the file rung's answer, for the notes
    bool modeled = false;          // a stackup file was used
    std::size_t bodiesMerged = 0;  // interconnect body layers merged into it
};

// The stackup file, else one derived from the technology's .lyp, else the
// generic interposer stack. On the interposer's technology the interconnect
// bodies are merged with mergeBodiesOnInterposer, as seating merges them, so a
// die's bumps are drawn on the plane it seats on. `assembly` and `lyp` may be
// null.
DrawnStackup drawnStackup(const Assembly* assembly, const std::string& techId,
                          const LayerPropertiesFile* lyp);

// Every (layer, datatype) the stackup marks `purpose: filler`.
std::vector<LayerKey> fillerKeys(const LayerStackup& stackup);

// One line per declared file that is not in use, naming the fallback each view
// uses instead. Empty when every declared file resolved. `sheetFromDeclared`
// tells the stackup note whether 3D can still derive heights from the sheet.
std::string sheetNote(const TechSheet& sheet);
std::string stackupNote(const std::string& techId, const TechStackup& stackup,
                        bool sheetFromDeclared);

// What 3D calls the stackup a technology is drawn with: the file name, or the
// derived/generic fallback when no file was used.
std::string stackupLabel(const TechStackup& stackup, bool fromSheet);

// The GDS layers of one component that its stackup places nowhere, so 3D
// draws nothing for them. Lists up to `kNoteKeyLimit` keys, then a count.
// Empty when `keys` is empty.
inline constexpr std::size_t kNoteKeyLimit = 8;
std::string unmodeledNote(const std::string& componentId,
                          const std::vector<LayerKey>& keys,
                          const std::string& stackupName);

// What 3D draws of one layer's GDS shapes, and what stopped the rest. Each
// count is the shapes that limit took, zero when it took none.
struct ShapeCount {
    LayerKey key;
    std::string name;
    bool borrowed = false;       // no stackup entry of its own: drawn at `name`'s height
    bool fill = false;           // its own entry is fill; Standard never draws its mesh
    std::size_t inGds = 0;
    std::size_t drawn = 0;
    std::size_t underArea = 0;   // skipped: under areaFloor
    double areaFloor = 0.0;      // um²
    std::size_t pastRead = 0;    // not read: past readLimit per layer
    std::size_t readLimit = 0;
    std::size_t viaSkipped = 0;  // the whole layer: a via layer over viaLimit cuts
    std::size_t viaLimit = 0;
    std::size_t pastMesh = 0;    // not built: past meshLimit per layer
    std::size_t meshLimit = 0;
};

// One line with both numbers and each limit that took shapes, for a layer 3D
// draws fewer shapes of than the GDS holds. Empty when it draws them all. A
// borrowed layer is named by its own key and the height it is drawn at.
std::string shapeCountNote(const std::string& componentId, const ShapeCount& count);

// The same two numbers for the layer's row, "3D shows 10,000 of 749,218
// shapes". Empty when 3D draws them all.
std::string shapeCountLabel(const ShapeCount& count);

// The notes above for every technology the assembly declares, in declaration
// order.
std::vector<std::string> technologyNotes(const Assembly& assembly);

// What a viewer should know about where dies sit: an interposer with no
// attachment_surface_z (its dies seat on a guessed plane, named with
// sourceName, the file's name, when given), a fragment left out for absolute
// heights, a stackup that puts the plane elsewhere than the file, and a bumped
// die whose file gives it a z other than where its bumps end. Studio draws a
// die where the file places it, so that gap is visible; the note says how big
// it is and why.
std::vector<std::string> placementNotes(const Assembly& assembly,
                                        const std::string& sourceName = {});

// The first of those on its own: the note that the dies sit on a guessed
// plane, or "" when the mount interposer declares one or no die has a
// connection. The window opens the notes for it, and the reader's load
// warning is this text too.
std::string mountPlaneGuessNote(const Assembly& assembly, const std::string& sourceName = {});

} // namespace chiplet

#endif // CHIPLET_CORE_TECHRESOLVE_H
