// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * ChipletFormat.cpp - Implementation of .chiplet YAML parser/writer
 */

#include "ChipletFormat.h"
#include "core/Technology.h"
#include "core/ConnectionStack.h"
#include "core/IOPad.h"
#include "core/flow/FlowConfig.h"
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

// Helper functions for parsing 3D structures from YAML
namespace {

Position3D parsePosition3D(const YAML::Node& node)
{
    Position3D pos;
    if (node["x"]) pos.x = node["x"].as<double>();
    if (node["y"]) pos.y = node["y"].as<double>();
    if (node["z"]) pos.z = node["z"].as<double>();
    return pos;
}

Rotation3D parseRotation3D(const YAML::Node& node)
{
    Rotation3D rot;
    if (node["z"]) rot.z = node["z"].as<double>();
    return rot;
}

Dimensions3D parseDimensions3D(const YAML::Node& node)
{
    Dimensions3D dims;
    if (node["width"]) dims.width = node["width"].as<double>();
    if (node["height"]) dims.height = node["height"].as<double>();
    if (node["thickness"]) dims.thickness = node["thickness"].as<double>();
    return dims;
}

} // anonymous namespace

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

ChipletFormat::ChipletFormat() = default;
ChipletFormat::~ChipletFormat() = default;

std::unique_ptr<Assembly> ChipletFormat::load(const string_type& path)
{
    // Store base path for relative path resolution
    std::filesystem::path filePath(path);
    m_basePath = filePath.parent_path().string();
    if (m_basePath.empty()) {
        m_basePath = ".";
    }

    // Load YAML file
    YAML::Node root;
    try {
        root = YAML::LoadFile(path);
    } catch (const YAML::Exception& e) {
        throw ChipletFormatException(e.what(), 0, "YAML");
    }

    // Check format version
    if (!root["format_version"]) {
        throw ChipletFormatException("Missing required field", 0, "format_version");
    }

    // Refuse to load intermediate output (KiCad export marks .chiplet
    // files as intermediate when they live in PCB-bbox-corner instead
    // of the canonical GDS-bbox-corner frame). The finalizer is
    // hyp_to_gds.py --update-chiplet-file, which strips this block.
    // See coord_frame_contract.md §4.1 (Option a) and §5.1.
    if (root["_metadata"]
        && root["_metadata"]["finalize_required"]
        && root["_metadata"]["finalize_required"].as<bool>(false)) {
        std::string finalizer = "hyp_to_gds.py --update-chiplet-file";
        if (root["_metadata"]["finalizer"]) {
            finalizer = root["_metadata"]["finalizer"].as<std::string>(finalizer);
        }
        throw ChipletFormatException(
            "this .chiplet is intermediate (PCB-bbox-corner frame); "
            "run " + finalizer + " to finalize",
            0,
            "_metadata.finalize_required");
    }

    auto assembly = std::make_unique<Assembly>();

    // Parse sections
    if (root["assembly"]) {
        parse_assembly_metadata(root["assembly"], *assembly);
    } else {
        throw ChipletFormatException("Missing required section", 0, "assembly");
    }

    if (root["technologies"]) {
        parse_technologies(root["technologies"], *assembly);
    }

    if (root["connection_stacks"]) {
        parse_connection_stacks(root["connection_stacks"], *assembly);
    }

    if (root["components"]) {
        parse_components(root["components"], *assembly);
    }

    // Interconnect adapter (optional): selects the bumping method whose 3D
    // bodies are merged into the stackup during auto_calculate_z. Mirrors the
    // interposer adapter; absent = interposer-only (no interconnect bodies).
    if (root["interconnect"] && root["interconnect"]["adapter"]) {
        std::string adapter = root["interconnect"]["adapter"].as<std::string>();
        assembly->set_interconnect_adapter(adapter);

        // Optional PDK-backed identity of the method (interconnect PDK lyp +
        // provenance). Registered like any other technology so viewers list
        // the interconnect alongside the die/interposer PDKs instead of
        // folding it into the interposer. A same-id entry already declared
        // under technologies: wins over this derived subblock.
        if (root["interconnect"]["technology"] && !assembly->technology(adapter)) {
            assembly->add_technology(
                parse_technology_entry(adapter, root["interconnect"]["technology"]));
        }
    }

    // Auto-calculate z for components with connection stacks and z == 0.0
    auto_calculate_z(*assembly);

    if (root["interfaces"]) {
        parse_interfaces(root["interfaces"], *assembly);
    }

    if (root["netlist"]) {
        parse_netlist(root["netlist"], *assembly);
    }

    if (root["flow"]) {
        FlowConfig flowConfig;
        FlowDefinition def = flowConfig.parse_flow_definition(root["flow"], *assembly);
        if (def.working_directory.empty()) {
            def.working_directory = m_basePath;
        }
        assembly->set_flow_definition(std::move(def));
    }

    // design_rules, default_views - skipped for now (future extension)

    return assembly;
}

void ChipletFormat::parse_assembly_metadata(const YAML::Node& node, Assembly& assembly)
{
    if (node["name"]) {
        assembly.set_name(node["name"].as<std::string>());
    } else {
        throw ChipletFormatException("Missing required field", 0, "assembly.name");
    }

    if (node["description"]) {
        assembly.set_description(node["description"].as<std::string>());
    }

    if (node["author"]) {
        assembly.set_author(node["author"].as<std::string>());
    }

    if (node["created"]) {
        assembly.set_created(node["created"].as<std::string>());
    }

    if (node["modified"]) {
        assembly.set_modified(node["modified"].as<std::string>());
    }

    if (node["units"]) {
        assembly.set_units(node["units"].as<std::string>());
    }

    if (node["assembly_gds"]) {
        assembly.set_assembly_gds(resolve_path(node["assembly_gds"].as<std::string>()));
    }

    if (node["io_technology"]) {
        assembly.set_io_technology(node["io_technology"].as<std::string>());
    }
}

void ChipletFormat::parse_technologies(const YAML::Node& node, Assembly& assembly)
{
    if (!node.IsMap()) {
        throw ChipletFormatException("Expected a map", 0, "technologies");
    }

    for (const auto& item : node) {
        std::string techId = item.first.as<std::string>();
        assembly.add_technology(parse_technology_entry(techId, item.second));
    }
}

std::unique_ptr<Technology> ChipletFormat::parse_technology_entry(
    const string_type& techId, const YAML::Node& techNode)
{
    auto tech = std::make_unique<Technology>(techId);

    if (techNode["description"]) {
        tech->set_description(techNode["description"].as<std::string>());
    }

    if (techNode["layer_properties"]) {
        std::string lpPath = techNode["layer_properties"].as<std::string>();
        std::string resolvedLypPath = resolve_path(lpPath);
        tech->set_layer_properties_path(resolvedLypPath);

        // Auto-load techfile based on layer_properties path
        // If layer_properties is "pdks/ihp-sg13g2/sg13g2.lyp"
        // Try loading "pdks/ihp-sg13g2/techfile/sg13g2.txt"
        std::filesystem::path lypPath(resolvedLypPath);
        std::string stem = lypPath.stem().string();  // "sg13g2"
        std::filesystem::path parent = lypPath.parent_path();  // "pdks/ihp-sg13g2"
        std::filesystem::path techfile = parent / "techfile" / (stem + ".txt");

        if (std::filesystem::exists(techfile)) {
            tech->load_process_def(techfile.string());
        }
    }

    if (techNode["dbu"]) {
        tech->set_dbu(techNode["dbu"].as<double>());
    }

    // stackup is skipped for now (future extension)

    return tech;
}

void ChipletFormat::parse_components(const YAML::Node& node, Assembly& assembly)
{
    if (!node.IsSequence()) {
        throw ChipletFormatException("Expected a sequence", 0, "components");
    }

    // Track components that did not declare `anchor:` so we can emit a
    // single file-level warning instead of one per component.
    // Per coord_frame_contract.md §2.2, the reader defaults to
    // bbox_center on absence and must warn (helps catch legacy files
    // before they cause silent geometry mismatches).
    std::vector<std::string> missingAnchor;

    for (const auto& compNode : node) {
        parse_component(compNode, assembly);
        const auto& comp = assembly.components().back();
        if (!comp->anchor_declared()) {
            missingAnchor.push_back(comp->id());
        }
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
}

void ChipletFormat::parse_component(const YAML::Node& node, Assembly& assembly)
{
    // Required fields
    if (!node["id"]) {
        throw ChipletFormatException("Missing required field", 0, "component.id");
    }
    std::string id = node["id"].as<std::string>();

    if (!node["type"]) {
        throw ChipletFormatException("Missing required field for " + id, 0, "component.type");
    }
    ComponentType type = string_to_component_type(node["type"].as<std::string>());

    auto component = std::make_unique<Component>(id, type);

    // Technology reference
    if (node["technology"]) {
        component->set_technology(node["technology"].as<std::string>());
    }

    // Connection stack reference
    if (node["connection"]) {
        component->set_connection(node["connection"].as<std::string>());
    }

    // Layout file
    if (node["layout"]) {
        std::string layoutPath = node["layout"].as<std::string>();
        component->set_layout_path(resolve_path(layoutPath));
    }

    // Cells - support both new 'cells' array and legacy 'top_cell' string
    if (node["cells"]) {
        // New format: cells is an array
        if (node["cells"].IsSequence()) {
            std::vector<std::string> cells;
            for (const auto& cellNode : node["cells"]) {
                cells.push_back(cellNode.as<std::string>());
            }
            component->set_cells(cells);
        } else {
            // Single cell as string (alternate format)
            component->set_top_cell(node["cells"].as<std::string>());
        }
    } else if (node["top_cell"]) {
        // Legacy format: single top_cell string -> convert to cells[0]
        component->set_top_cell(node["top_cell"].as<std::string>());
    }

    // Position
    if (node["position"]) {
        component->set_position(parsePosition3D(node["position"]));
    }

    // Rotation
    if (node["rotation"]) {
        component->set_rotation(parseRotation3D(node["rotation"]));
    }

    // Orientation (face-up or flip-chip). Render mode stays at the
    // constructor default (Transparent for dies, Solid for substrates)
    // regardless of orientation: a Transparent overview opens faster
    // and lets the user see the whole assembly at a glance. Users
    // promote individual components to Detailed via the Hierarchy
    // panel when they need to inspect cu-pillar contact / GDS
    // tessellation.
    if (node["orientation"]) {
        std::string orient = node["orientation"].as<std::string>("face_up");
        if (orient == "flip_chip" || orient == "face_down")
            component->set_orientation(Orientation::FaceDown);
    }

    // Anchor convention (see coord_frame_contract.md §2). Drives mesh
    // centering downstream. When absent, default to BboxCenter (the
    // pre-contract behavior for interposers); the file-level warning
    // is emitted by parse_components.
    if (node["anchor"]) {
        std::string anchorStr = node["anchor"].as<std::string>();
        auto parsed = string_to_anchor(anchorStr);
        if (parsed.has_value()) {
            component->set_anchor(parsed.value());
            component->set_anchor_declared(true);
        } else {
            qWarning("[chiplet] component '%s': unknown anchor value '%s' "
                     "(expected gds_origin or bbox_center); defaulting to "
                     "bbox_center",
                     id.c_str(), anchorStr.c_str());
            // Treat as undeclared so the file-level summary warns too.
        }
    }

    // Heuristic guard against HYP-absolute or other foreign-frame leaks
    // (per contract §5: warn loudly when |position.x| or |position.y|
    // exceeds 1e5 µm — the wire-bond demo io_pads bug surfaced exactly
    // there).
    const auto& posCheck = component->position();
    constexpr double kPositionWarnThreshold_um = 1.0e5;
    if (std::fabs(posCheck.x) > kPositionWarnThreshold_um
        || std::fabs(posCheck.y) > kPositionWarnThreshold_um) {
        qWarning("[chiplet] component '%s': position (%.3f, %.3f) µm "
                 "exceeds %.0e µm — likely a foreign-frame leak (e.g. "
                 "HYP-absolute). See coord_frame_contract.md §1.",
                 id.c_str(), posCheck.x, posCheck.y,
                 kPositionWarnThreshold_um);
    }

    // Dimensions
    if (node["dimensions"]) {
        component->set_dimensions(parseDimensions3D(node["dimensions"]));
    }

    // Array configuration (for DieArray type)
    if (node["array"]) {
        const YAML::Node& arrayNode = node["array"];
        ComponentArray arr;

        if (arrayNode["pattern"]) {
            arr.pattern = arrayNode["pattern"].as<std::string>();
        }

        if (arrayNode["count"]) {
            if (arrayNode["count"]["x"]) arr.countX = arrayNode["count"]["x"].as<int>();
            if (arrayNode["count"]["y"]) arr.countY = arrayNode["count"]["y"].as<int>();
        }

        if (arrayNode["pitch"]) {
            if (arrayNode["pitch"]["x"]) arr.pitchX = arrayNode["pitch"]["x"].as<double>();
            if (arrayNode["pitch"]["y"]) arr.pitchY = arrayNode["pitch"]["y"].as<double>();
        }

        if (arrayNode["start_position"]) {
            arr.startPosition = parsePosition3D(arrayNode["start_position"]);
        }

        component->set_array(arr);
    }

    // Custom metadata
    if (node["metadata"]) {
        const YAML::Node& metaNode = node["metadata"];
        if (metaNode.IsMap()) {
            for (const auto& item : metaNode) {
                component->set_metadata(
                    item.first.as<std::string>(),
                    item.second.as<std::string>()
                );
            }
        }
    }

    // External I/O pads (e.g. wire-bond pads on the interposer).
    // Optional and additive: existing files without io_pads are unaffected.
    if (node["io_pads"] && node["io_pads"].IsSequence()) {
        // Track io_pads whose declared position falls outside the
        // plausible range for the canonical frame — a heuristic to
        // catch HYP-absolute leaks (wire-bond demo bug, see contract
        // §6). One warning per parent component, listing pad ids.
        std::vector<std::string> oobPads;

        for (const auto& padNode : node["io_pads"]) {
            IOPad pad;
            if (padNode["id"]) {
                pad.set_id(padNode["id"].as<std::string>());
            }
            if (padNode["io_class"]) {
                pad.set_io_class(string_to_io_class(
                    padNode["io_class"].as<std::string>()));
            }
            if (padNode["net"]) {
                pad.set_net(padNode["net"].as<std::string>());
            }
            if (padNode["position"]) {
                IOPadPosition p;
                if (padNode["position"]["x"]) {
                    p.x = padNode["position"]["x"].as<double>();
                }
                if (padNode["position"]["y"]) {
                    p.y = padNode["position"]["y"].as<double>();
                }
                pad.set_position(p);
            }
            if (padNode["size"]) {
                IOPadSize sz;
                if (padNode["size"]["x"]) {
                    sz.x = padNode["size"]["x"].as<double>();
                }
                if (padNode["size"]["y"]) {
                    sz.y = padNode["size"]["y"].as<double>();
                }
                pad.set_size(sz);
            }
            if (padNode["layer"]) {
                pad.set_layer(padNode["layer"].as<std::string>());
            }

            constexpr double kPositionWarnThreshold_um = 1.0e5;
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
                     id.c_str(), oobPads.size(), idList.c_str());
        }
    }

    assembly.add_component(std::move(component));
}

void ChipletFormat::parse_connection_stacks(const YAML::Node& node, Assembly& assembly)
{
    if (!node.IsMap()) {
        throw ChipletFormatException("Expected a map", 0, "connection_stacks");
    }

    for (const auto& item : node) {
        ConnectionStack stack;
        stack.id = item.first.as<std::string>();
        const YAML::Node& stackNode = item.second;

        if (stackNode["description"]) {
            stack.description = stackNode["description"].as<std::string>();
        }

        if (stackNode["layers"] && stackNode["layers"].IsSequence()) {
            for (const auto& layerNode : stackNode["layers"]) {
                ConnectionStackLayer layer;
                if (layerNode["name"]) layer.name = layerNode["name"].as<std::string>();
                if (layerNode["material"]) layer.material = layerNode["material"].as<std::string>();
                if (layerNode["height"]) layer.height = layerNode["height"].as<double>();
                if (layerNode["diameter"]) layer.diameter = layerNode["diameter"].as<double>();
                stack.layers.push_back(layer);
            }
        }

        assembly.add_connection_stack(stack);
    }
}

void ChipletFormat::parse_interfaces(const YAML::Node& node, Assembly& assembly)
{
    if (!node.IsSequence()) {
        throw ChipletFormatException("Expected a sequence", 0, "interfaces");
    }

    for (const auto& ifaceNode : node) {
        if (!ifaceNode["id"]) {
            throw ChipletFormatException("Missing required field", 0, "interface.id");
        }
        if (!ifaceNode["type"]) {
            throw ChipletFormatException("Missing required field", 0, "interface.type");
        }

        std::string id = ifaceNode["id"].as<std::string>();
        InterfaceType type = interface_type_from_string(ifaceNode["type"].as<std::string>());

        auto iface = std::make_unique<Interface>(id, type);

        if (ifaceNode["from"]) {
            const YAML::Node& fromNode = ifaceNode["from"];
            InterfaceEndpoint ep;
            if (fromNode["component"]) ep.component = fromNode["component"].as<std::string>();
            if (fromNode["surface"]) ep.surface = fromNode["surface"].as<std::string>();
            if (fromNode["port_layer"]) ep.portLayer = fromNode["port_layer"].as<std::string>();
            iface->set_from(ep);
        }

        if (ifaceNode["to"]) {
            const YAML::Node& toNode = ifaceNode["to"];
            InterfaceEndpoint ep;
            if (toNode["component"]) ep.component = toNode["component"].as<std::string>();
            if (toNode["surface"]) ep.surface = toNode["surface"].as<std::string>();
            if (toNode["port_layer"]) ep.portLayer = toNode["port_layer"].as<std::string>();
            iface->set_to(ep);
        }

        if (ifaceNode["physical"]) {
            const YAML::Node& physNode = ifaceNode["physical"];
            InterfacePhysical phys;
            if (physNode["pitch"]) phys.pitch = physNode["pitch"].as<double>();
            if (physNode["diameter"]) phys.diameter = physNode["diameter"].as<double>();
            if (physNode["height"]) phys.height = physNode["height"].as<double>();
            iface->set_physical(phys);
        }

        assembly.add_interface(std::move(iface));
    }
}

void ChipletFormat::parse_netlist(const YAML::Node& node, Assembly& assembly)
{
    Netlist netlist;

    if (node["nets"] && node["nets"].IsSequence()) {
        for (const auto& netNode : node["nets"]) {
            if (!netNode["name"]) {
                throw ChipletFormatException("Missing required field", 0, "netlist.nets[].name");
            }

            std::string name = netNode["name"].as<std::string>();
            std::string classStr = netNode["class"] ? netNode["class"].as<std::string>() : "signal";
            NetClass nc = string_to_net_class(classStr);

            Net net(name, nc);

            if (netNode["external"]) {
                net.set_external(netNode["external"].as<bool>());
            }

            if (netNode["connections"] && netNode["connections"].IsSequence()) {
                for (const auto& connNode : netNode["connections"]) {
                    NetConnection conn;
                    if (connNode["component"]) {
                        conn.component = connNode["component"].as<std::string>();
                    }
                    if (connNode["pin"]) {
                        conn.pin = connNode["pin"].as<std::string>();
                    }
                    if (connNode["layer"]) {
                        conn.layer = connNode["layer"].as<std::string>();
                    }
                    net.add_connection(conn);
                }
            }

            netlist.add_net(std::move(net));
        }
    }

    if (node["external_netlist"]) {
        netlist.set_external_netlist_path(node["external_netlist"].as<std::string>());
    }

    assembly.set_netlist(std::move(netlist));
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
            << tech.layer_properties_path();
    }

    out << YAML::Key << "dbu" << YAML::Value << tech.dbu();
}

void ChipletFormat::save(const Assembly& assembly, const string_type& path)
{
    YAML::Emitter out;
    out << YAML::BeginMap;

    // Format version
    out << YAML::Key << "format_version" << YAML::Value << "1.0";

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
        out << YAML::Key << "assembly_gds" << YAML::Value << assembly.assembly_gds();
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
                out << YAML::Key << "layout" << YAML::Value << comp->layout_path();
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
// to the directory name walked for next to the .chiplet file plus the marker
// subpath that must exist under the root. Mirrors the Python reader
// (chiplet_kicad_plugin/hyp_to_gds.py) and the discovery convention in
// adk/docs/integration.md: environment -> sibling-checkout walk -> loud
// failure.
struct PathVarMarker {
    const char* name;
    const char* dirname;
    const char* marker; // subpath under the root that must exist
};

constexpr PathVarMarker kPathVarMarkers[] = {
    { "INTERPOSER_PDK_ROOT", "interposer", "libs.tech/klayout" },
    { "GDS_TO_KICAD_ROOT", "gds_to_kicad", "pdks" },
    { "ADK_ROOT", "adk", "klayout/drc" },
    { "INTERCONNECT_PDK_ROOT", "interconnect_pdk", "manifest" },
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
        if (env[0] != '\0'
            && (marker == nullptr
                || std::filesystem::is_directory(
                       std::filesystem::path(env) / marker->marker))) {
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
        std::filesystem::path cand = base / marker->dirname;
        if (std::filesystem::is_directory(cand / marker->marker)) {
            return cand.string();
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
