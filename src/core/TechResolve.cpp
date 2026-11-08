// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TechResolve.h"

#include "Assembly.h"
#include "CountText.h"
#include "Component.h"
#include "DecimalText.h"
#include "Technology.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <sstream>

namespace chiplet {

namespace {

bool isFile(const std::string& path)
{
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

std::string microns(double v)
{
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f", v);
    return buf;
}

std::string grouped(std::size_t n)
{
    return groupedCount(n);
}

std::string fileName(const std::string& path)
{
    return std::filesystem::path(path).filename().string();
}

// Why a declared path is not usable, or "" when it is. The loader has already
// recorded a ${VAR} that did not expand; a path that expanded can still name
// nothing.
std::string declaredProblem(const std::string& source, const std::string& path,
                            const std::string& loaderProblem)
{
    if (source.empty() && path.empty()) {
        return {};
    }
    if (!loaderProblem.empty()) {
        return loaderProblem;
    }
    if (!isFile(path)) {
        return "file not found: " + path;
    }
    return {};
}

} // namespace

TechSheet resolveTechnologySheet(const Technology* tech, const std::string& techId)
{
    TechSheet out;
    out.techId = techId;
    if (tech) {
        out.problem = declaredProblem(tech->layer_properties_source(),
                                      tech->layer_properties_path(),
                                      tech->layer_properties_problem());
        if (out.problem.empty() && !tech->layer_properties_path().empty()) {
            out.path = tech->layer_properties_path();
            out.source = SheetSource::Declared;
            return out;
        }
    }
    const std::string bundled = BlenderGDSConfigs::pdkLayerPropertiesPath(techId);
    if (!bundled.empty()) {
        out.path = bundled;
        out.source = SheetSource::BuiltIn;
    }
    return out;
}

TechStackup resolveTechnologyStackup(const Technology* tech, const std::string& techId)
{
    TechStackup out;
    if (tech) {
        out.problem = declaredProblem(tech->stackup_source(), tech->stackup_path(),
                                      tech->stackup_problem());
        if (out.problem.empty() && !tech->stackup_path().empty()) {
            if (out.stackup.loadFromBlenderGDS(tech->stackup_path())) {
                out.source = StackupSource::Declared;
                out.path = tech->stackup_path();
                return out;
            }
            out.stackup.clear();
            out.problem = "could not read " + tech->stackup_path();
        }
    }
    if (techId.empty()) {
        return out;
    }
    const std::string bundled = BlenderGDSConfigs::stackupPath(techId);
    if (!bundled.empty() && out.stackup.loadFromBlenderGDS(bundled)) {
        out.source = StackupSource::BuiltIn;
        out.path = bundled;
        return out;
    }
    out.stackup.clear();
    return out;
}

std::vector<LayerKey> fillerKeys(const LayerStackup& stackup)
{
    std::vector<LayerKey> keys;
    for (const auto& elev : stackup.sortedLayers()) {
        if (elev.isFiller()) {
            keys.push_back(elev.key());
        }
    }
    return keys;
}

std::string sheetNote(const TechSheet& sheet)
{
    if (sheet.problem.empty()) {
        return {};
    }
    std::string note = sheet.techId + " layer sheet not loaded: " + sheet.problem + ". ";
    // 3D colours come from the declared sheet only, so a failed declaration
    // always leaves 3D on generated colours whatever 2D found.
    if (sheet.source == SheetSource::BuiltIn) {
        note += "2D uses Studio's built-in sheet " + fileName(sheet.path)
              + "; 3D uses generated colours.";
    } else {
        note += "2D uses default styling; 3D uses generated colours.";
    }
    return note;
}

std::string stackupNote(const std::string& techId, const TechStackup& stackup,
                        bool sheetFromDeclared)
{
    if (stackup.problem.empty()) {
        return {};
    }
    std::string note = techId + " stackup not loaded: " + stackup.problem + ". ";
    if (stackup.source == StackupSource::BuiltIn) {
        note += "Using Studio's built-in stackup " + fileName(stackup.path) + ".";
    } else if (sheetFromDeclared) {
        note += "3D stacks the layer sheet's layers at a default thickness; fill is not "
                "recognised.";
    } else {
        note += "3D uses a generic interposer stackup; fill is not recognised.";
    }
    return note;
}

std::string stackupLabel(const TechStackup& stackup, bool fromSheet)
{
    if (stackup.source != StackupSource::NotModeled) {
        return fileName(stackup.path);
    }
    return fromSheet ? "layer-sheet" : "generic interposer";
}

std::string unmodeledNote(const std::string& componentId,
                          const std::vector<LayerKey>& keys,
                          const std::string& stackupName)
{
    if (keys.empty()) {
        return {};
    }
    std::string list;
    const std::size_t shown = std::min(keys.size(), kNoteKeyLimit);
    for (std::size_t i = 0; i < shown; ++i) {
        if (i) list += ", ";
        list += std::to_string(keys[i].layer) + "/" + std::to_string(keys[i].datatype);
    }
    if (keys.size() > shown) {
        list += " and " + std::to_string(keys.size() - shown) + " more";
    }
    return "3D: " + componentId + " has shapes on " + list + ", not in the "
         + stackupName + " stackup, not drawn.";
}

std::string shapeCountNote(const std::string& componentId, const ShapeCount& count)
{
    if (count.drawn >= count.inGds) {
        return {};
    }
    std::string note = "3D: " + componentId + " " + std::to_string(count.key.layer) + "/"
                     + std::to_string(count.key.datatype);
    if (count.borrowed) {
        note += " has no stackup entry; drawn at " + count.name + "'s height";
    } else if (!count.name.empty()) {
        note += " " + count.name;
    }
    note += ": " + grouped(count.drawn) + " of " + grouped(count.inGds) + " shapes shown";
    if (count.underArea) {
        std::ostringstream floor;
        floor << count.areaFloor;
        note += "; " + grouped(count.underArea) + " under " + floor.str() + " um² skipped";
    }
    if (count.pastRead) {
        note += "; " + grouped(count.pastRead) + " past the read limit of "
              + grouped(count.readLimit) + " per layer";
    }
    if (count.viaSkipped) {
        note += "; a via layer over " + grouped(count.viaLimit) + " cuts is not drawn";
    }
    if (count.pastMesh) {
        note += "; " + grouped(count.pastMesh) + " past the mesh limit of "
              + grouped(count.meshLimit) + " per layer";
    }
    return note + ".";
}

std::string shapeCountLabel(const ShapeCount& count)
{
    if (count.drawn >= count.inGds) {
        return {};
    }
    return "3D shows " + grouped(count.drawn) + " of " + grouped(count.inGds) + " shapes";
}

std::vector<std::string> technologyNotes(const Assembly& assembly)
{
    std::vector<std::string> notes;
    for (const auto& tech : assembly.technologies()) {
        const TechSheet sheet = resolveTechnologySheet(tech.get(), tech->id());
        if (std::string n = sheetNote(sheet); !n.empty()) {
            notes.push_back(std::move(n));
        }
        const TechStackup stackup = resolveTechnologyStackup(tech.get(), tech->id());
        if (std::string n = stackupNote(tech->id(), stackup,
                                        sheet.source == SheetSource::Declared);
            !n.empty()) {
            notes.push_back(std::move(n));
        }
    }
    return notes;
}

std::size_t mergeBodiesOnInterposer(LayerStackup& stackup, const Component& interposer,
                                    const std::vector<std::string>& keys)
{
    if (interposer.attachment_surface_z()) {
        stackup.setAttachmentSurfaceZ(*interposer.attachment_surface_z());
    }
    return stackup.mergeInterconnectFragments(keys);
}

DrawnStackup drawnStackup(const Assembly* assembly, const std::string& techId,
                          const LayerPropertiesFile* lyp)
{
    DrawnStackup d;

    // Priority 0: a stackup file, the technology's own else Studio's bundled
    // one for its id. The same resolver seats the dies
    // (Assembly::calculate_component_z) and writes the notes, so the three
    // cannot disagree on which file is in use.
    d.resolved = resolveTechnologyStackup(assembly ? assembly->technology(techId) : nullptr,
                                          techId);
    if (d.resolved.source != StackupSource::NotModeled) {
        d.stackup = d.resolved.stackup;
        d.modeled = true;
    } else if (lyp) {
        // Priority 1: derive a stackup from the technology's .lyp (default
        // thickness). A supported PDK never reaches this -- its bundled YAML
        // resolves at Priority 0 -- but it lets an unsupported PDK that ships
        // only a .lyp still render with sensible per-layer z.
        d.stackup = LayerStackup::fromLayerProperties(*lyp);
    } else {
        // Priority 2: generic last resort when there is neither a YAML stackup
        // nor a .lyp -- a plain top-metal interposer stack so geometry still
        // renders. (The former techfile and hardcoded-name rungs were removed:
        // the bundled YAMLs at Priority 0 always won, so they were dead. The
        // golden test test_stackup_order_golden.cpp guards that the surviving
        // ladder still yields the exact same order for every bundled PDK.)
        d.stackup = Stackups::createInterposer();
    }

    // Merge the interconnect PDK's 3D bodies (CuPillar/SnAgCap, or a vendor
    // microbump), which the interconnect PDK owns rather than the interposer
    // stackup, so the layer-render path knows their z and height. Additive and
    // idempotent (addLayer overwrites by key). The render merges the UNION of
    // the fragments of all methods the dies use (per-die connection ids are the
    // keys); the assembly-level adapter is the legacy fallback when no method
    // id resolves.
    //
    // ONLY for the interposer's technology: the bodies physically sit on the
    // interposer. Merging them into a die's stackup inflates totalHeight(),
    // which is the flip-chip beolTop -- every layer of a FaceDown die would
    // render shifted up by the whole body-stack height (die floating above its
    // pillars by ~44 um).
    if (!assembly || techId.empty()) return d;
    for (const auto& c : assembly->components()) {
        if (c->type() != ComponentType::Interposer || c->technology() != techId) continue;
        d.bodiesMerged = mergeBodiesOnInterposer(
            d.stackup, *c,
            LayerStackup::resolveInterconnectKeys(assembly->interconnect_method_ids(),
                                                  assembly->interconnect_adapter()));
        break;
    }
    return d;
}

std::vector<std::string> placementNotes(const Assembly& assembly)
{
    std::vector<std::string> notes;
    std::string stackupName = "interposer thickness";
    for (const auto& comp : assembly.components()) {
        if (comp->type() != ComponentType::Interposer || comp->technology().empty()) continue;
        const TechStackup s = resolveTechnologyStackup(
            assembly.technology(comp->technology()), comp->technology());
        if (s.source != StackupSource::NotModeled) {
            stackupName = fileName(s.path);
            // Seating and the render take the file's plane over the
            // stackup's, so a stackup that puts it elsewhere is said once.
            const std::optional<double>& plane = comp->attachment_surface_z();
            if (plane && s.stackup.hasAttachmentSurfaceZ()
                && *plane != s.stackup.attachmentSurfaceZ()) {
                notes.push_back(comp->id() + " declares its attach plane at "
                                + shortestText(*plane) + " um, but " + stackupName
                                + " puts it at "
                                + shortestText(s.stackup.attachmentSurfaceZ())
                                + " um. Dies seat on the file's plane.");
            }
        }
        break;
    }

    for (const auto& comp : assembly.components()) {
        if (comp->type() == ComponentType::Interposer || comp->connection().empty()) continue;
        const double z = comp->position().z;
        if (z == 0.0) continue;  // seated by the reader at open
        const double seat = assembly.calculate_component_z(comp->id());
        if (std::fabs(z - seat) < 0.005) continue;
        notes.push_back(comp->id() + " is placed at z " + microns(z) + " um, but its "
                        + comp->connection() + " bumps end at " + microns(seat)
                        + " um on " + stackupName + ". Drawn where the file places it.");
    }
    return notes;
}

} // namespace chiplet
