// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * ChipletSynth.cpp - Implementation
 */

#include "ChipletSynth.h"

#include <cstdio>
#include <sstream>

namespace chiplet {

const char* const kCustomImportTechId = "custom_imported";

namespace {

// Format a double for YAML: fixed notation, trailing zeros trimmed, so 1234.0
// becomes "1234" and 1234.5 stays "1234.5" (never scientific notation, which
// some YAML readers mishandle).
std::string num(double v)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.4f", v);
    std::string s(buf);
    const std::string::size_type dot = s.find('.');
    if (dot != std::string::npos) {
        std::string::size_type last = s.find_last_not_of('0');
        if (last == dot) {
            --last;  // drop the now-bare decimal point too
        }
        s.erase(last + 1);
    }
    return s;
}

// Double-quoted YAML scalar with the two escapes a quoted scalar needs.
std::string yq(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (char c : s) {
        if (c == '\\' || c == '"') {
            out += '\\';
        }
        out += c;
    }
    out += '"';
    return out;
}

}  // namespace

std::string synthesizeSingleGdsChiplet(const SingleGdsImportSpec& spec)
{
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

    std::ostringstream o;
    o << "# Synthesized by Chiplet Studio (File > Import GDS).\n"
      << "# Wraps a single GDS for 3D viewing; edit freely or re-open directly.\n"
      << "\n"
      << "format_version: \"1.0\"\n"
      << "\n"
      << "assembly:\n"
      << "  name: " << yq(assemblyName) << "\n"
      << "  units: \"um\"\n"
      << "\n"
      << "technologies:\n"
      << "  " << techKey << ":\n";
    if (!spec.customLyp.empty()) {
        o << "    layer_properties: " << yq(spec.customLyp) << "\n";
    }
    if (!spec.customStackup.empty()) {
        o << "    stackup: " << yq(spec.customStackup) << "\n";
    }
    o << "    dbu: 0.001\n"
      << "\n"
      << "components:\n"
      << "  - id: " << componentId << "\n"
      << "    type: die\n"
      << "    technology: " << techKey << "\n"
      << "    layout: " << yq(spec.gdsPath) << "\n";
    if (!spec.topCell.empty()) {
        o << "    top_cell: " << yq(spec.topCell) << "\n";
    }
    o << "    dimensions: { width: " << num(width)
      << ", height: " << num(height)
      << ", thickness: " << num(thickness) << " }\n"
      // bbox_center, NOT the die convention gds_origin, and the difference is
      // half a die. The general rule (dies anchor on gds_origin) describes a
      // die placed in an assembly, where its GDS origin is the design
      // reference the position is measured from. This path is not that: it
      // wraps a single loose GDS for 3D viewing, the position is a hardcoded
      // zero, and the dimensions above ARE the GDS bounding box. A
      // bbox-sized box has to be centred on the bbox; anchoring it on the
      // GDS origin would shift every imported layout drawn in the first
      // quadrant by half its own size. Declared explicitly because the
      // format requires new files to state it and the reader's default for
      // an absent anchor is not guaranteed to stay bbox_center.
      << "    anchor: bbox_center\n"
      << "    position: { x: 0, y: 0, z: 0 }\n";

    return o.str();
}

}  // namespace chiplet
