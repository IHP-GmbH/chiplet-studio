// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * AssemblySheet.h - One 2D layer list for a layout that mixes technologies
 *
 * KLayout styles by layer number, not by cell, so one number gets one style
 * in a view. An assembly GDS carries the carrier (interposer and interconnect)
 * and the dies, and several of their numbers coincide. This decides, per GDS
 * layer, who owns it, whose sheet styles it, which group lists it and whether
 * it starts visible. It knows nothing about KLayout's view; KLayout2DView only
 * applies the result.
 */

#ifndef CHIPLET_VIEW2D_ASSEMBLYSHEET_H
#define CHIPLET_VIEW2D_ASSEMBLYSHEET_H

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace db {
class Layout;
}

namespace chiplet {

using LayerNum = std::pair<int, int>;  // GDS layer, datatype

// One GDS layer that has shapes, and on which side of the assembly.
struct LayerUse {
    LayerNum key;
    bool carrier = false;            // shapes outside every die cell
    std::set<std::string> dieTechs;  // technologies of the die cells holding shapes on it
};

// What one technology contributes: its sheet's entries in sheet order and the
// layers its stackup marks as fill.
struct SheetTech {
    std::vector<LayerNum> order;
    std::map<LayerNum, std::string> names;
    std::map<LayerNum, bool> visible;
    std::set<LayerNum> fill;
};

struct ComposedRow {
    LayerNum key;
    std::string styleTech;            // whose sheet styles the row; empty = default styling
    std::string name;                 // row text after "layer/datatype"
    std::vector<std::string> owners;  // technologies with shapes on the layer
    bool fill = false;
    bool visible = true;              // at open
};

struct ComposedGroup {
    std::string title;                // a technology id, "Shared", or "Unassigned"
    std::vector<ComposedRow> rows;
};

inline constexpr const char kSharedGroup[] = "Shared";
inline constexpr const char kUnassignedGroup[] = "Unassigned";

// carrierTechs: the interposer's technology, then the interconnect's (either
// may be absent). Carrier layers take the first of these whose sheet names
// them. A layer used on both sides, or by dies of two technologies, is one
// row in "Shared", styled by the carrier sheet (else the first owner's sheet
// that names it), and its name lists every owner. Groups come carrier first,
// then Shared, then the die technologies by id, then Unassigned (carrier
// shapes when no carrier technology is known). Rows go by layer number.
std::vector<ComposedGroup> composeAssemblySheet(
    const std::vector<LayerUse>& uses,
    const std::vector<std::string>& carrierTechs,
    const std::map<std::string, SheetTech>& sheets);

// A die cell is any cell named "<component id>_..." for a die component.
struct DiePrefix {
    std::string componentId;
    std::string techId;
};

struct Occupancy {
    std::vector<LayerUse> uses;
    std::vector<std::string> unmatchedDies;  // die component ids no cell starts with
};

// Everything a 2D load needs to know about the technologies involved.
struct SheetSpec {
    std::string techId;
    std::string lypPath;     // empty: no sheet, default styling
    std::set<LayerNum> fill; // the technology's stackup marks these `purpose: filler`
};

struct LoadSpec {
    std::vector<SheetSpec> sheets;
    std::vector<std::string> carrierTechs;
    std::vector<DiePrefix> dies;  // empty: the whole layout belongs to carrierTechs
    // The interposer's own GDS, where the carrier fill lives when the layout
    // shown leaves it out (an assembled GDS). Read on the first Show Fill.
    std::string carrierFillPath;
};

class Assembly;

// The assembly GDS: the interposer's and the interconnect's technologies are
// the carrier, and every other component with a technology is a die. With
// `carrierOnly` (the interposer's own GDS standing in for a missing assembly
// GDS) there are no die cells to look for.
LoadSpec assemblyLoadSpec(const Assembly& assembly, bool carrierOnly);

// One component's own GDS: all of it belongs to its technology.
LoadSpec componentLoadSpec(const Assembly& assembly, const std::string& componentId);

// The interposer's layout file when it exists and is not `shownPath` itself;
// empty otherwise. An assembled GDS may leave the carrier fill out, and this
// is where 2D reads it from.
std::string carrierFillSource(const Assembly& assembly, const std::string& shownPath);

#ifdef HAVE_KLAYOUT
// Walk the layout from `topCell`. A die cell's whole subtree counts for its
// technology (KLayout's per-layer bounding box, so one call per layer); every
// other cell reached without passing through a die cell counts for the
// carrier by its own shapes.
Occupancy measureOccupancy(const db::Layout& layout, unsigned int topCell,
                           const std::vector<DiePrefix>& dies);
#endif

} // namespace chiplet

#endif // CHIPLET_VIEW2D_ASSEMBLYSHEET_H
