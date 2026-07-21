// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * ChipletFormat.cpp - Implementation of .chiplet YAML parser/writer
 *
 * The .chiplet *parsing* is delegated to the vendored, Apache-2.0
 * chiplet_format_io reference library (dependency-clean: yaml-cpp only, no Qt
 * or KLayout). This host maps the library's plain ChipletDocument into the
 * studio's core/Assembly and keeps the studio-specific concerns the reference
 * library deliberately stays out of: path resolution, qWarning diagnostics, z
 * auto-calculation, techfile auto-load and flow parsing. The *writer* (save) is
 * unchanged and still emits via yaml-cpp directly.
 */

#include "ChipletFormat.h"
#include "core/Technology.h"
#include "core/ConnectionStack.h"
#include "core/IOPad.h"
#include "core/flow/FlowConfig.h"
#include <chiplet_format_io/chiplet_format_io.hpp>
#include <yaml-cpp/yaml.h>
#include <QDebug>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <filesystem>

namespace chiplet {

// ChipletFormatException implementation

ChipletFormatException::ChipletFormatException(const string_type& msg,
                                               size_t line,
                                               const string_type& context)
    : m_line(line)
    , m_context(context)
{
    m_message = context.empty() ? msg : (context + ": " + msg);
    if (line > 0) {
        m_message += " (line " + std::to_string(line) + ")";
    }
}

const char* ChipletFormatException::what() const noexcept
{
    return m_message.c_str();
}

// Type conversion functions
ComponentType string_to_component_type(const std::string& s)
{
    if (s == "die") return ComponentType::Die;
    if (s == "die_array") return ComponentType::DieArray;
    if (s == "interposer") return ComponentType::Interposer;
    if (s == "substrate") return ComponentType::Substrate;
    return ComponentType::Die;  // Default
}

std::string component_type_to_string(ComponentType t)
{
    switch (t) {
        case ComponentType::Die: return "die";
        case ComponentType::DieArray: return "die_array";
        case ComponentType::Interposer: return "interposer";
        case ComponentType::Substrate: return "substrate";
    }
    return "die";
}

InterfaceType interface_type_from_string(const std::string& s)
{
    if (s == "micro_bump") return InterfaceType::MicroBump;
    if (s == "copper_pillar") return InterfaceType::CopperPillar;
    if (s == "tsv") return InterfaceType::TSV;
    if (s == "wire_bond") return InterfaceType::WireBond;
    throw ChipletFormatException("Unknown interface type: " + s, 0, "interface.type");
}

std::string interface_type_to_string(InterfaceType t)
{
    switch (t) {
        case InterfaceType::MicroBump: return "micro_bump";
        case InterfaceType::CopperPillar: return "copper_pillar";
        case InterfaceType::TSV: return "tsv";
        case InterfaceType::WireBond: return "wire_bond";
    }
    return "micro_bump";
}

namespace {

// The .chiplet format revision this reader/writer implements. load()
// rejects any other value; bump together with docs/CHIPLET_FORMAT_SPEC.md
// and the Python writer (chiplet_kicad_plugin/writers/chiplet_writer.py).
constexpr const char* kSupportedFormatVersion = "1.0";

} // namespace

ChipletFormat::ChipletFormat() = default;
ChipletFormat::~ChipletFormat() = default;

std::unique_ptr<Assembly> ChipletFormat::load(const string_type& path)
{
    // Store base path for relative path resolution.
    std::filesystem::path filePath(path);
    m_basePath = filePath.parent_path().string();
    if (m_basePath.empty()) {
        m_basePath = ".";
    }

    // Parse + validate the document with the vendored Apache-2.0 reference
    // library. It owns the format itself (YAML grammar, the format_version
    // gate, the intermediate-file guard, required-field and interface-type
    // validation) and returns a dependency-clean ChipletDocument. Translate its
    // error type into this reader's public exception so callers' contracts hold.
    namespace cfio = chiplet_format_io;
    cfio::ChipletDocument doc;
    try {
        doc = cfio::load(path);  // allow_intermediate=false, validate=true
    } catch (const cfio::ChipletFormatError& e) {
        throw ChipletFormatException(e.what());
    }

    auto assembly = std::make_unique<Assembly>();

    // --- Assembly metadata ---
    assembly->set_name(doc.assembly.name);
    if (!doc.assembly.description.empty())
        assembly->set_description(doc.assembly.description);
    if (!doc.assembly.author.empty())
        assembly->set_author(doc.assembly.author);
    if (!doc.assembly.created.empty())
        assembly->set_created(doc.assembly.created);
    if (!doc.assembly.modified.empty())
        assembly->set_modified(doc.assembly.modified);
    if (!doc.assembly.units.empty())
        assembly->set_units(doc.assembly.units);
    if (!doc.assembly.assembly_gds.empty()) {
        assembly->set_assembly_gds(resolve_path(doc.assembly.assembly_gds));
        // Keep the verbatim string so a round-trip save does not bake in the
        // resolved absolute path (destroying ${VAR}/relative portability).
        assembly->set_assembly_gds_source(doc.assembly.assembly_gds);
    }
    if (!doc.assembly.io_technology.empty())
        assembly->set_io_technology(doc.assembly.io_technology);

    // --- Technologies (insertion order preserved) ---
    for (const auto& tech : doc.technologies) {
        assembly->add_technology(build_technology(tech));
    }

    // --- Connection stacks ---
    for (const auto& s : doc.connection_stacks) {
        ConnectionStack stack;
        stack.id = s.id;
        stack.description = s.description;
        for (const auto& l : s.layers) {
            ConnectionStackLayer layer;
            layer.name = l.name;
            layer.material = l.material;
            layer.height = l.height;
            layer.diameter = l.diameter;
            stack.layers.push_back(layer);
        }
        assembly->add_connection_stack(stack);
    }

    // --- Components ---
    // Per coord_frame_contract.md §2.2 the reader defaults a missing `anchor:`
    // to bbox_center and emits a single file-level warning (helps catch legacy
    // files before they cause silent geometry mismatches).
    constexpr double kPositionWarnThreshold_um = 1.0e5;
    std::vector<std::string> missingAnchor;

    for (const auto& c : doc.components) {
        auto component = std::make_unique<Component>(
            c.id, string_to_component_type(c.type));

        if (!c.technology.empty()) component->set_technology(c.technology);
        if (!c.connection.empty()) component->set_connection(c.connection);
        if (!c.layout.empty()) {
            component->set_layout_path(resolve_path(c.layout));
            component->set_layout_path_source(c.layout);
        }

        // cells[] vs legacy top_cell: a single cell is stored via set_top_cell
        // (cells[0]) for backward compatibility, multiple via set_cells.
        if (c.cells.size() == 1) {
            component->set_top_cell(c.cells.front());
        } else if (c.cells.size() > 1) {
            component->set_cells(c.cells);
        }

        {
            Position3D pos;
            pos.x = c.position.x;
            pos.y = c.position.y;
            pos.z = c.position.z;
            component->set_position(pos);
        }

        if (c.rotation.z != 0.0) {
            Rotation3D rot;
            rot.z = c.rotation.z;
            component->set_rotation(rot);
        }

        // Orientation (face-up default; flip_chip/face_down -> FaceDown). Render
        // mode stays at the constructor default regardless of orientation.
        if (c.orientation == "flip_chip" || c.orientation == "face_down") {
            component->set_orientation(Orientation::FaceDown);
        }

        // Anchor convention (coord_frame_contract.md §2). Present-and-valid sets
        // the anchor and marks it declared; present-but-unknown warns and is
        // treated as undeclared (so the file-level summary warns too); absent
        // leaves the BboxCenter default, undeclared.
        if (c.anchor.has_value()) {
            auto parsed = string_to_anchor(c.anchor.value());
            if (parsed.has_value()) {
                component->set_anchor(parsed.value());
                component->set_anchor_declared(true);
            } else {
                qWarning("[chiplet] component '%s': unknown anchor value '%s' "
                         "(expected gds_origin or bbox_center); defaulting to "
                         "bbox_center",
                         c.id.c_str(), c.anchor.value().c_str());
            }
        }
        if (!component->anchor_declared()) {
            missingAnchor.push_back(c.id);
        }

        // Heuristic guard against HYP-absolute or other foreign-frame leaks
        // (contract §5): warn loudly when |position.x|/|position.y| exceed
        // 1e5 µm — the wire-bond demo io_pads bug surfaced exactly there.
        const auto& posCheck = component->position();
        if (std::fabs(posCheck.x) > kPositionWarnThreshold_um
            || std::fabs(posCheck.y) > kPositionWarnThreshold_um) {
            qWarning("[chiplet] component '%s': position (%.3f, %.3f) µm "
                     "exceeds %.0e µm — likely a foreign-frame leak (e.g. "
                     "HYP-absolute). See coord_frame_contract.md §1.",
                     c.id.c_str(), posCheck.x, posCheck.y,
                     kPositionWarnThreshold_um);
        }

        {
            Dimensions3D dims;
            dims.width = c.dimensions.width;
            dims.height = c.dimensions.height;
            dims.thickness = c.dimensions.thickness;
            component->set_dimensions(dims);
        }

        // Interposer die-attachment surface (component-level, optional).
        // Absent => nullopt; Assembly falls back to dimensions.thickness.
        component->set_attachment_surface_z(c.attachment_surface_z);

        if (c.array.has_value()) {
            const auto& la = c.array.value();
            ComponentArray arr;
            arr.pattern = la.pattern;
            arr.countX = la.count_x;
            arr.countY = la.count_y;
            arr.pitchX = la.pitch_x;
            arr.pitchY = la.pitch_y;
            arr.startPosition.x = la.start_position.x;
            arr.startPosition.y = la.start_position.y;
            arr.startPosition.z = la.start_position.z;
            component->set_array(arr);
        }

        for (const auto& kv : c.metadata) {
            component->set_metadata(kv.first, kv.second);
        }

        // External I/O pads (e.g. wire-bond pads on the interposer).
        std::vector<std::string> oobPads;
        for (const auto& lp : c.io_pads) {
            IOPad pad;
            if (!lp.id.empty()) pad.set_id(lp.id);
            if (!lp.io_class.empty()) {
                // string_to_io_class throws std::runtime_error on an unknown
                // value; translate it so load() honors its documented
                // ChipletFormatException-only contract (external consumers,
                // including adk-tools, catch on that type).
                try {
                    pad.set_io_class(string_to_io_class(lp.io_class));
                } catch (const std::exception& e) {
                    throw ChipletFormatException(
                        std::string("invalid io_class '") + lp.io_class + "'", 0,
                        "component '" + c.id + "' io_pad '" + lp.id + "'");
                }
            }
            if (!lp.net.empty()) pad.set_net(lp.net);
            {
                IOPadPosition p;
                p.x = lp.pos_x;
                p.y = lp.pos_y;
                pad.set_position(p);
            }
            {
                IOPadSize sz;
                sz.x = lp.size_x;
                sz.y = lp.size_y;
                pad.set_size(sz);
            }
            if (!lp.layer.empty()) pad.set_layer(lp.layer);

            const auto& padPos = pad.position();
            if (std::fabs(padPos.x) > kPositionWarnThreshold_um
                || std::fabs(padPos.y) > kPositionWarnThreshold_um) {
                oobPads.push_back(pad.id());
            }

            component->add_io_pad(pad);
        }
        if (!oobPads.empty()) {
            std::string idList;
            for (size_t i = 0; i < oobPads.size(); ++i) {
                if (i > 0) idList += ", ";
                idList += oobPads[i];
            }
            qWarning("[chiplet] component '%s': %zu io_pad(s) with "
                     "position outside ±1e5 µm — likely HYP-absolute "
                     "leak. Affected pads: %s. See "
                     "coord_frame_contract.md section 6.",
                     c.id.c_str(), oobPads.size(), idList.c_str());
        }

        assembly->add_component(std::move(component));
    }

    if (!missingAnchor.empty()) {
        std::string idList;
        for (size_t i = 0; i < missingAnchor.size(); ++i) {
            if (i > 0) idList += ", ";
            idList += missingAnchor[i];
        }
        qWarning("[chiplet] %zu component(s) without explicit 'anchor' "
                 "field; defaulted to bbox_center: %s",
                 missingAnchor.size(), idList.c_str());
        qWarning("[chiplet] See chiplet-studio/docs/coord_frame_contract.md "
                 "section 2 for the anchor contract.");
    }

    // --- Interconnect adapter (optional) ---
    // Selects the bumping method whose 3D bodies are merged into the stackup
    // during auto_calculate_z. Its PDK-backed technology is registered like any
    // other technology so viewers list it alongside the die/interposer PDKs; a
    // same-id entry already declared under technologies: wins over it.
    if (doc.interconnect.has_value()) {
        assembly->set_interconnect_adapter(doc.interconnect->adapter);
        if (doc.interconnect->technology.has_value()
            && !assembly->technology(doc.interconnect->adapter)) {
            assembly->add_technology(
                build_technology(doc.interconnect->technology.value()));
        }
    }

    // Auto-calculate z for components with connection stacks and z == 0.0.
    auto_calculate_z(*assembly);

    // --- Interfaces ---
    for (const auto& i : doc.interfaces) {
        auto iface = std::make_unique<Interface>(
            i.id, interface_type_from_string(i.type));
        if (i.from.has_value()) {
            InterfaceEndpoint ep;
            ep.component = i.from->component;
            ep.surface = i.from->surface;
            ep.portLayer = i.from->port_layer;
            iface->set_from(ep);
        }
        if (i.to.has_value()) {
            InterfaceEndpoint ep;
            ep.component = i.to->component;
            ep.surface = i.to->surface;
            ep.portLayer = i.to->port_layer;
            iface->set_to(ep);
        }
        if (i.physical.has_value()) {
            InterfacePhysical phys;
            phys.pitch = i.physical->pitch;
            phys.diameter = i.physical->diameter;
            phys.height = i.physical->height;
            iface->set_physical(phys);
        }
        assembly->add_interface(std::move(iface));
    }

    // --- Netlist ---
    if (doc.netlist.present) {
        Netlist netlist;
        for (const auto& n : doc.netlist.nets) {
            Net net(n.name, string_to_net_class(n.net_class));
            net.set_external(n.external);
            for (const auto& conn : n.connections) {
                NetConnection nc;
                nc.component = conn.component;
                nc.pin = conn.pin;
                nc.layer = conn.layer;
                net.add_connection(nc);
            }
            netlist.add_net(std::move(net));
        }
        if (!doc.netlist.external_netlist.empty()) {
            netlist.set_external_netlist_path(doc.netlist.external_netlist);
        }
        assembly->set_netlist(std::move(netlist));
    }

    // --- Flow definition ---
    // The reference library preserves the flow block verbatim; FlowConfig (which
    // resolves ${...} variables against the assembly at parse time) stays the
    // host's concern.
    if (doc.has_flow && !doc.flow_yaml.empty()) {
        YAML::Node flowNode = YAML::Load(doc.flow_yaml);
        FlowConfig flowConfig;
        FlowDefinition def = flowConfig.parse_flow_definition(flowNode, *assembly);
        if (def.working_directory.empty()) {
            def.working_directory = m_basePath;
        }
        assembly->set_flow_definition(std::move(def));
    }

    // design_rules, default_views - skipped for now (future extension)

    return assembly;
}

std::unique_ptr<Technology> ChipletFormat::build_technology(
    const chiplet_format_io::Technology& tech)
{
    auto out = std::make_unique<Technology>(tech.id);

    if (!tech.description.empty()) {
        out->set_description(tech.description);
    }

    if (!tech.layer_properties.empty()) {
        std::string resolvedLypPath = resolve_path(tech.layer_properties);
        out->set_layer_properties_path(resolvedLypPath);
        out->set_layer_properties_source(tech.layer_properties);

        // Auto-load the GDS3D techfile that sits beside the .lyp:
        // "<dir>/<stem>.lyp" -> "<dir>/techfile/<stem>.txt".
        std::filesystem::path lypPath(resolvedLypPath);
        std::string stem = lypPath.stem().string();
        std::filesystem::path parent = lypPath.parent_path();
        std::filesystem::path techfile = parent / "techfile" / (stem + ".txt");
        if (std::filesystem::exists(techfile)) {
            out->load_process_def(techfile.string());
        }
    }

    // Explicit stackup YAML: resolve through the SAME ${VAR}/relative chain as
    // layer_properties (resolve_path => expand_path_vars -> absolute passthrough
    // -> m_basePath / p, i.e. relative to the .chiplet dir). Keep the verbatim
    // source so the writer round-trips the original string.
    if (!tech.stackup.empty()) {
        out->set_stackup_path(resolve_path(tech.stackup));
        out->set_stackup_source(tech.stackup);
    }

    if (tech.has_dbu) {
        out->set_dbu(tech.dbu);
    }

    return out;
}

void ChipletFormat::auto_calculate_z(Assembly& assembly)
{
    for (const auto& comp : assembly.components()) {
        if (comp->connection().empty()) continue;
        if (comp->position().z != 0.0) continue;

        double z = assembly.calculate_component_z(comp->id());
        if (z > 0.0) {
            Position3D pos = comp->position();
            pos.z = z;
            comp->set_position(pos);
        }
    }
}

// Shared field emission for a technology entry (used by the technologies:
// map and the interconnect: technology subblock).
static void emit_technology_fields(YAML::Emitter& out, const Technology& tech)
{
    if (!tech.description().empty()) {
        out << YAML::Key << "description" << YAML::Value << tech.description();
    }

    if (!tech.layer_properties_path().empty()) {
        out << YAML::Key << "layer_properties" << YAML::Value
            << (tech.layer_properties_source().empty()
                    ? tech.layer_properties_path()
                    : tech.layer_properties_source());
    }

    if (!tech.stackup_path().empty()) {
        out << YAML::Key << "stackup" << YAML::Value
            << (tech.stackup_source().empty()
                    ? tech.stackup_path()
                    : tech.stackup_source());
    }

    out << YAML::Key << "dbu" << YAML::Value << tech.dbu();
}

void ChipletFormat::save(const Assembly& assembly, const string_type& path)
{
    YAML::Emitter out;
    out << YAML::BeginMap;

    // Format version
    out << YAML::Key << "format_version" << YAML::Value << kSupportedFormatVersion;

    // Assembly metadata
    out << YAML::Key << "assembly" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "name" << YAML::Value << assembly.name();
    if (!assembly.description().empty()) {
        out << YAML::Key << "description" << YAML::Value << assembly.description();
    }
    if (!assembly.author().empty()) {
        out << YAML::Key << "author" << YAML::Value << assembly.author();
    }
    if (!assembly.created().empty()) {
        out << YAML::Key << "created" << YAML::Value << assembly.created();
    }
    if (!assembly.modified().empty()) {
        out << YAML::Key << "modified" << YAML::Value << assembly.modified();
    }
    out << YAML::Key << "units" << YAML::Value << assembly.units();
    if (!assembly.assembly_gds().empty()) {
        out << YAML::Key << "assembly_gds" << YAML::Value
            << (assembly.assembly_gds_source().empty()
                    ? assembly.assembly_gds()
                    : assembly.assembly_gds_source());
    }
    if (!assembly.io_technology().empty()) {
        out << YAML::Key << "io_technology" << YAML::Value << assembly.io_technology();
    }
    out << YAML::EndMap;

    // Technologies. The interconnect adapter's technology (if registered) is
    // emitted under the interconnect: block below -- its canonical home --
    // not in this map.
    const std::string& icAdapter = assembly.interconnect_adapter();
    bool haveComponentTech = false;
    for (const auto& tech : assembly.technologies()) {
        if (icAdapter.empty() || tech->id() != icAdapter) {
            haveComponentTech = true;
            break;
        }
    }
    if (haveComponentTech) {
        out << YAML::Key << "technologies" << YAML::Value << YAML::BeginMap;

        for (const auto& tech : assembly.technologies()) {
            if (!icAdapter.empty() && tech->id() == icAdapter) {
                continue;
            }
            out << YAML::Key << tech->id() << YAML::Value << YAML::BeginMap;
            emit_technology_fields(out, *tech);
            out << YAML::EndMap;
        }

        out << YAML::EndMap;
    }

    // Interconnect (assembly-level bumping method + its PDK-backed identity)
    if (!icAdapter.empty()) {
        out << YAML::Key << "interconnect" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "adapter" << YAML::Value << icAdapter;

        Technology* ictech = assembly.technology(icAdapter);
        if (ictech) {
            out << YAML::Key << "technology" << YAML::Value << YAML::BeginMap;
            emit_technology_fields(out, *ictech);
            out << YAML::EndMap;
        }

        out << YAML::EndMap;
    }

    // Connection stacks
    if (!assembly.connection_stacks().empty()) {
        out << YAML::Key << "connection_stacks" << YAML::Value << YAML::BeginMap;

        for (const auto& [stackId, stack] : assembly.connection_stacks()) {
            out << YAML::Key << stackId << YAML::Value << YAML::BeginMap;

            if (!stack.description.empty()) {
                out << YAML::Key << "description" << YAML::Value << stack.description;
            }

            if (!stack.layers.empty()) {
                out << YAML::Key << "layers" << YAML::Value << YAML::BeginSeq;
                for (const auto& layer : stack.layers) {
                    out << YAML::Flow << YAML::BeginMap;
                    out << YAML::Key << "name" << YAML::Value << layer.name;
                    out << YAML::Key << "material" << YAML::Value << layer.material;
                    out << YAML::Key << "height" << YAML::Value << layer.height;
                    out << YAML::Key << "diameter" << YAML::Value << layer.diameter;
                    out << YAML::EndMap;
                }
                out << YAML::EndSeq;
            }

            out << YAML::EndMap;
        }

        out << YAML::EndMap;
    }

    // Components
    if (!assembly.components().empty()) {
        out << YAML::Key << "components" << YAML::Value << YAML::BeginSeq;

        for (const auto& comp : assembly.components()) {
            out << YAML::BeginMap;
            out << YAML::Key << "id" << YAML::Value << comp->id();
            out << YAML::Key << "type" << YAML::Value << component_type_to_string(comp->type());

            // Anchor convention is part of the canonical contract and
            // must always be emitted explicitly so downstream readers
            // never have to guess (see coord_frame_contract.md §2,
            // §4 Writer Contract).
            out << YAML::Key << "anchor" << YAML::Value
                << anchor_to_string(comp->anchor());

            if (!comp->technology().empty()) {
                out << YAML::Key << "technology" << YAML::Value << comp->technology();
            }

            if (!comp->connection().empty()) {
                out << YAML::Key << "connection" << YAML::Value << comp->connection();
            }

            if (!comp->layout_path().empty()) {
                out << YAML::Key << "layout" << YAML::Value
                    << (comp->layout_path_source().empty()
                            ? comp->layout_path()
                            : comp->layout_path_source());
            }

            // Save cells - use 'cells' array for multi-cell, 'top_cell' for single (backward compat)
            const auto& cells = comp->cells();
            if (!cells.empty()) {
                if (cells.size() == 1) {
                    // Single cell: use legacy 'top_cell' for backward compatibility
                    out << YAML::Key << "top_cell" << YAML::Value << cells[0];
                } else {
                    // Multiple cells: use new 'cells' array
                    out << YAML::Key << "cells" << YAML::Value << YAML::Flow << YAML::BeginSeq;
                    for (const auto& cell : cells) {
                        out << cell;
                    }
                    out << YAML::EndSeq;
                }
            }

            // Position
            const auto& pos = comp->position();
            if (pos.x != 0 || pos.y != 0 || pos.z != 0) {
                out << YAML::Key << "position" << YAML::Value << YAML::Flow << YAML::BeginMap;
                out << YAML::Key << "x" << YAML::Value << pos.x;
                out << YAML::Key << "y" << YAML::Value << pos.y;
                out << YAML::Key << "z" << YAML::Value << pos.z;
                out << YAML::EndMap;
            }

            // Rotation
            const auto& rot = comp->rotation();
            if (rot.z != 0) {
                out << YAML::Key << "rotation" << YAML::Value << YAML::Flow << YAML::BeginMap;
                out << YAML::Key << "z" << YAML::Value << rot.z;
                out << YAML::EndMap;
            }

            // Orientation
            if (comp->orientation() == Orientation::FaceDown) {
                out << YAML::Key << "orientation" << YAML::Value << "flip_chip";
            }

            // Dimensions
            const auto& dims = comp->dimensions();
            if (dims.width != 0 || dims.height != 0 || dims.thickness != 0) {
                out << YAML::Key << "dimensions" << YAML::Value << YAML::Flow << YAML::BeginMap;
                out << YAML::Key << "width" << YAML::Value << dims.width;
                out << YAML::Key << "height" << YAML::Value << dims.height;
                out << YAML::Key << "thickness" << YAML::Value << dims.thickness;
                out << YAML::EndMap;
            }

            // Interposer die-attachment surface (component-level). Emitted only
            // when present so files without it round-trip unchanged.
            if (comp->attachment_surface_z()) {
                out << YAML::Key << "attachment_surface_z" << YAML::Value
                    << comp->attachment_surface_z().value();
            }

            // Array configuration
            if (comp->is_array()) {
                const auto& arr = comp->array().value();
                out << YAML::Key << "array" << YAML::Value << YAML::BeginMap;
                out << YAML::Key << "pattern" << YAML::Value << arr.pattern;
                out << YAML::Key << "count" << YAML::Value << YAML::Flow << YAML::BeginMap;
                out << YAML::Key << "x" << YAML::Value << arr.countX;
                out << YAML::Key << "y" << YAML::Value << arr.countY;
                out << YAML::EndMap;
                out << YAML::Key << "pitch" << YAML::Value << YAML::Flow << YAML::BeginMap;
                out << YAML::Key << "x" << YAML::Value << arr.pitchX;
                out << YAML::Key << "y" << YAML::Value << arr.pitchY;
                out << YAML::EndMap;
                out << YAML::Key << "start_position" << YAML::Value << YAML::Flow << YAML::BeginMap;
                out << YAML::Key << "x" << YAML::Value << arr.startPosition.x;
                out << YAML::Key << "y" << YAML::Value << arr.startPosition.y;
                out << YAML::Key << "z" << YAML::Value << arr.startPosition.z;
                out << YAML::EndMap;
                out << YAML::EndMap;
            }

            // Metadata
            const auto& meta = comp->all_metadata();
            if (!meta.empty()) {
                out << YAML::Key << "metadata" << YAML::Value << YAML::BeginMap;
                for (const auto& [key, value] : meta) {
                    out << YAML::Key << key << YAML::Value << value;
                }
                out << YAML::EndMap;
            }

            // I/O pads
            if (!comp->io_pads().empty()) {
                out << YAML::Key << "io_pads" << YAML::Value << YAML::BeginSeq;
                for (const auto& pad : comp->io_pads()) {
                    out << YAML::BeginMap;
                    out << YAML::Key << "id" << YAML::Value << pad.id();
                    out << YAML::Key << "io_class" << YAML::Value
                        << io_class_to_string(pad.io_class());
                    if (!pad.net().empty()) {
                        out << YAML::Key << "net" << YAML::Value << pad.net();
                    }
                    out << YAML::Key << "position" << YAML::Value
                        << YAML::Flow << YAML::BeginMap;
                    out << YAML::Key << "x" << YAML::Value << pad.position().x;
                    out << YAML::Key << "y" << YAML::Value << pad.position().y;
                    out << YAML::EndMap;
                    out << YAML::Key << "size" << YAML::Value
                        << YAML::Flow << YAML::BeginMap;
                    out << YAML::Key << "x" << YAML::Value << pad.size().x;
                    out << YAML::Key << "y" << YAML::Value << pad.size().y;
                    out << YAML::EndMap;
                    if (!pad.layer().empty()) {
                        out << YAML::Key << "layer" << YAML::Value << pad.layer();
                    }
                    out << YAML::EndMap;
                }
                out << YAML::EndSeq;
            }

            out << YAML::EndMap;
        }

        out << YAML::EndSeq;
    }

    // Interfaces
    if (!assembly.interfaces().empty()) {
        out << YAML::Key << "interfaces" << YAML::Value << YAML::BeginSeq;

        for (const auto& iface : assembly.interfaces()) {
            out << YAML::BeginMap;
            out << YAML::Key << "id" << YAML::Value << iface->id();
            out << YAML::Key << "type" << YAML::Value << interface_type_to_string(iface->type());

            // From endpoint
            const auto& from = iface->from();
            if (!from.component.empty()) {
                out << YAML::Key << "from" << YAML::Value << YAML::Flow << YAML::BeginMap;
                out << YAML::Key << "component" << YAML::Value << from.component;
                out << YAML::Key << "surface" << YAML::Value << from.surface;
                out << YAML::Key << "port_layer" << YAML::Value << from.portLayer;
                out << YAML::EndMap;
            }

            // To endpoint
            const auto& to = iface->to();
            if (!to.component.empty()) {
                out << YAML::Key << "to" << YAML::Value << YAML::Flow << YAML::BeginMap;
                out << YAML::Key << "component" << YAML::Value << to.component;
                out << YAML::Key << "surface" << YAML::Value << to.surface;
                out << YAML::Key << "port_layer" << YAML::Value << to.portLayer;
                out << YAML::EndMap;
            }

            // Physical parameters
            const auto& phys = iface->physical();
            if (phys.pitch != 0 || phys.diameter != 0 || phys.height != 0) {
                out << YAML::Key << "physical" << YAML::Value << YAML::Flow << YAML::BeginMap;
                out << YAML::Key << "pitch" << YAML::Value << phys.pitch;
                out << YAML::Key << "diameter" << YAML::Value << phys.diameter;
                out << YAML::Key << "height" << YAML::Value << phys.height;
                out << YAML::EndMap;
            }

            out << YAML::EndMap;
        }

        out << YAML::EndSeq;
    }

    // Netlist
    if (!assembly.netlist().empty()) {
        out << YAML::Key << "netlist" << YAML::Value << YAML::BeginMap;

        const auto& nl = assembly.netlist();
        if (nl.net_count() > 0) {
            out << YAML::Key << "nets" << YAML::Value << YAML::BeginSeq;

            for (const auto& net : nl.nets()) {
                out << YAML::BeginMap;
                out << YAML::Key << "name" << YAML::Value << net.name();
                out << YAML::Key << "class" << YAML::Value << net_class_to_string(net.net_class());

                if (net.external()) {
                    out << YAML::Key << "external" << YAML::Value << net.external();
                }

                if (net.connection_count() > 0) {
                    out << YAML::Key << "connections" << YAML::Value << YAML::BeginSeq;
                    for (const auto& conn : net.connections()) {
                        out << YAML::Flow << YAML::BeginMap;
                        out << YAML::Key << "component" << YAML::Value << conn.component;
                        out << YAML::Key << "pin" << YAML::Value << conn.pin;
                        if (!conn.layer.empty()) {
                            out << YAML::Key << "layer" << YAML::Value << conn.layer;
                        }
                        out << YAML::EndMap;
                    }
                    out << YAML::EndSeq;
                }

                out << YAML::EndMap;
            }

            out << YAML::EndSeq;
        }

        if (!nl.external_netlist_path().empty()) {
            out << YAML::Key << "external_netlist" << YAML::Value << nl.external_netlist_path();
        }

        out << YAML::EndMap;
    }

    // Flow definition
    if (assembly.has_flow()) {
        FlowConfig flowConfig;
        flowConfig.write_flow_definition(assembly.flow_definition(), out);
    }

    out << YAML::EndMap;

    // Write to file
    std::ofstream file(path);
    if (!file.is_open()) {
        throw ChipletFormatException("Could not open file for writing: " + path, 0, "file");
    }

    file << out.c_str();
}

namespace {

// Ecosystem-root variables accepted inside .chiplet path entries. Each maps
// to the directory names walked for next to the .chiplet file (canonical
// name first, then the GitHub repo name a default clone produces) plus the
// marker subpath that must exist under the root. Mirrors the Python reader
// (chiplet_kicad_plugin/hyp_to_gds.py _PATH_VAR_MARKERS) and the discovery
// convention in adk/docs/integration.md: environment -> sibling-checkout
// walk -> loud failure.
struct PathVarMarker {
    const char* name;
    const char* dirnames[3]; // walk candidates, nullptr-terminated
    const char* marker;      // subpath under the root that must exist
};

constexpr PathVarMarker kPathVarMarkers[] = {
    { "INTERPOSER_PDK_ROOT", { "interposer", "OpenIntM4TM2" },
      "libs.tech/klayout" },
    { "GDS_TO_KICAD_ROOT", { "gds_to_kicad", "gds-to-kicad" }, "pdks" },
    { "ADK_ROOT", { "adk", "ADK" }, "klayout/drc" },
    { "INTERCONNECT_PDK_ROOT",
      { "interconnect_pdk", "IHP-Interconnect-IntM4TM2" }, "manifest" },
    // Base SG13G2 PDK (standard IHP convention: $PDK_ROOT/ihp-sg13g2/...).
    { "PDK_ROOT", { "IHP-Open-PDK" }, "ihp-sg13g2/libs.tech/klayout" },
};

std::string discover_path_var(const std::string& name,
                              const std::filesystem::path& startDir)
{
    const PathVarMarker* marker = nullptr;
    for (const auto& m : kPathVarMarkers) {
        if (name == m.name) {
            marker = &m;
            break;
        }
    }

    // A set-and-valid environment value wins; set-but-invalid (marker
    // subpath missing) falls through to the walk, mirroring the Python side.
    if (const char* env = std::getenv(name.c_str())) {
        // Use the error_code overload: the throwing one would let an ELOOP/EACCES
        // on a hostile env value escape load() as a filesystem_error instead of
        // the documented ChipletFormatException. On any OS error treat the env
        // candidate as invalid and fall through to the walk.
        std::error_code ec_env;
        if (env[0] != '\0'
            && (marker == nullptr
                || std::filesystem::is_directory(
                       std::filesystem::path(env) / marker->marker, ec_env))) {
            return env;
        }
    }

    if (marker == nullptr) {
        return {};
    }

    std::error_code ec;
    std::filesystem::path base = std::filesystem::absolute(startDir, ec);
    if (ec) {
        base = startDir;
    }
    while (!base.empty()) {
        for (const char* dirname : marker->dirnames) {
            if (dirname == nullptr) {
                break;
            }
            std::filesystem::path cand = base / dirname;
            std::error_code ec_cand;
            if (std::filesystem::is_directory(cand / marker->marker, ec_cand)) {
                return cand.string();
            }
        }
        std::filesystem::path parent = base.parent_path();
        if (parent == base) {
            break;
        }
        base = parent;
    }
    return {};
}

} // namespace

ChipletFormat::string_type ChipletFormat::expand_path_vars(const string_type& path) const
{
    if (path.find("${") == string_type::npos) {
        return path;
    }

    string_type out = path;
    size_t pos = 0;
    while ((pos = out.find("${", pos)) != string_type::npos) {
        size_t end = out.find('}', pos + 2);
        if (end == string_type::npos) {
            break; // unterminated reference: pass through untouched
        }
        std::string name = out.substr(pos + 2, end - pos - 2);
        std::string value = discover_path_var(name, m_basePath);
        if (value.empty()) {
            throw ChipletFormatException(
                "cannot resolve ${" + name + "} in path '" + path
                    + "'; set the " + name + " environment variable or keep "
                    "the checkout next to the .chiplet file (ecosystem "
                    "discovery convention, see adk/docs/integration.md)",
                0, "path");
        }
        out.replace(pos, end - pos + 1, value);
        pos += value.size();
    }
    return out;
}

ChipletFormat::string_type ChipletFormat::resolve_path(const string_type& relativePath) const
{
    if (relativePath.empty()) {
        return relativePath;
    }

    // Expand ${VAR} ecosystem-root references first (environment ->
    // sibling-checkout walk -> throw). Plain paths pass through untouched,
    // so absolute and relative inputs keep their normal semantics.
    string_type expanded = expand_path_vars(relativePath);

    // Check if already absolute
    std::filesystem::path p(expanded);
    if (p.is_absolute()) {
        return expanded;
    }

    // Resolve relative to base path
    std::filesystem::path resolved = std::filesystem::path(m_basePath) / p;
    return resolved.lexically_normal().string();
}

} // namespace chiplet
