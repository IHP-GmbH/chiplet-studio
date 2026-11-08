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
#include "core/DecimalText.h"
#include "core/IOPad.h"
#include "core/flow/FlowConfig.h"
#include "YamlScalarEmit.h"
#include "TopLevelKeyIntent.h"
#include <chiplet_format_io/chiplet_format_io.hpp>
#include <yaml-cpp/yaml.h>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <regex>
#include <filesystem>
#include <vector>
#include <cerrno>
#include <functional>
#include <iterator>
#include <set>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace chiplet {

// ChipletFormatException implementation

ChipletFormatException::ChipletFormatException(const string_type& msg,
                                               size_t line,
                                               const string_type& context,
                                               FailureKind kind)
    : m_line(line)
    , m_context(context)
    , m_kind(kind)
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

// interface_type_from_string / interface_type_to_string are gone. The mapping
// they held now lives with the value it describes, in core/Interface.h, as one
// table read in both directions. The pair here was two hand-written switches
// that could disagree, and the from-string half refused the whole DOCUMENT for
// a value it did not recognise, which is the opposite of the policy the
// format's own reference header declares for this field.

bool is_valid_adapter_id(const std::string& id)
{
    if (id.empty()) return false;

    // chiplet-system-spec definitions/interconnect/properties/adapter, both
    // halves. regex_match anchors both ends, so the pattern carries no end
    // anchor: std::regex is ECMAScript and has no \Z. See the header for why
    // the anchor is never ported verbatim between dialects.
    static const std::regex kIdPattern(R"([A-Za-z0-9_][A-Za-z0-9_.\-]*)");
    static const std::regex kDeckSuffix(R"(.*\.drc)");

    return std::regex_match(id, kIdPattern) && !std::regex_match(id, kDeckSuffix);
}

bool path_has_nul(const std::string& path)
{
    return path.find('\0') != std::string::npos;
}

namespace {

// The .chiplet format revision this reader/writer implements. load()
// rejects any other value; bump together with docs/CHIPLET_FORMAT_SPEC.md
// and the Python writer (chiplet_kicad_plugin/writers/chiplet_writer.py).
constexpr const char* kSupportedFormatVersion = "1.0";

// The two things wrong with a destination that are the caller's mistake rather
// than the filesystem's answer. Split out so admission can apply them before it
// asks the filesystem anything, without also resolving the path.
void reject_invalid_destination_path(const std::string& path)
{
    if (path.empty() || path_has_nul(path))
        throw ChipletFormatException("Destination path is empty or contains a NUL byte");
}

std::string canonical_destination(const std::string& path)
{
    reject_invalid_destination_path(path);
    std::error_code ec;
    // Both failures below are the filesystem declining to resolve a path, which
    // an unsearchable parent directory produces, and neither says anything
    // about the document. Same reasoning as the inspection failure in
    // admit_destination: the question the kind answers is what may be done
    // next, and what may be done next is try somewhere else. An empty or
    // NUL-bearing path above is a caller mistake and keeps the default.
    const auto absolute = std::filesystem::absolute(path, ec);
    if (ec) throw ChipletFormatException("Cannot establish absolute destination: " + path,
                                         0, "", FailureKind::DestinationIo);
    auto result = std::filesystem::weakly_canonical(absolute, ec);
    if (ec)
        throw ChipletFormatException("Cannot establish destination identity: " + path,
                                     0, "", FailureKind::DestinationIo);
    // An empty result with no error is not the filesystem declining; it is a
    // shape this code did not expect, so it keeps the default.
    if (result.empty())
        throw ChipletFormatException("Cannot establish destination identity: " + path);
    return result.string();
}

std::string read_binary(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) throw ChipletFormatException("Cannot read file: " + path);
    std::string text((std::istreambuf_iterator<char>(in)), {});
    if (in.bad()) throw ChipletFormatException("Incomplete file read: " + path);
    return text;
}

bool writer_owned(const std::string& key)
{
    static const std::set<std::string> keys = {
        "format_version", "assembly", "technologies", "interconnect",
        "connection_stacks", "components", "interfaces", "netlist", "flow"};
    return keys.count(key) != 0;
}

DocumentPersistence capture_context(const std::string& text, const std::string& path)
{
    DocumentPersistence state;
    state.source_path = path;
    state.source_bytes = text;
    const auto capture = chiplet_format_io::capture_top_level_blocks(text);
    state.capture_safe = capture.safe;
    if (!capture.safe) {
        state.capture_reason = capture.reason_code + ": " + capture.reason;
        if (capture.byte_offset)
            state.capture_reason += " at UTF-8 byte " + std::to_string(*capture.byte_offset);
        return state;
    }
    state.preamble = capture.preamble;
    for (const auto& block : capture.blocks)
        if (!writer_owned(block.key) || block.key == "flow")
            state.roots.push_back({block.key, block.raw});
    return state;
}

void admit_destination(const std::string& path, const SaveRequest& request)
{
    namespace fs = std::filesystem;
    // Order matters here and it is the whole point of this arrangement.
    //
    // Resolving the path follows symlinks, and a symlink that points at itself
    // resolves to ELOOP rather than to a path. Asking the resolver first would
    // therefore report a destination that is plainly TAKEN as a filesystem
    // problem, and a caller with a fallback would answer it by writing the
    // document somewhere else while the name the user gave sits occupied. That
    // is the exact failure these kinds exist to prevent, so occupancy is
    // decided from the leaf's own status, which follows nothing, before
    // anything resolves it.
    //
    // The invalid-path checks keep their place at the front: they are about the
    // string the caller passed, not about what is on disk.
    reject_invalid_destination_path(path);

    std::error_code ec;
    const auto status = fs::symlink_status(path, ec);
    if (ec && ec != std::errc::no_such_file_or_directory)
        // Not knowing what is at the name is a fact about this filesystem and
        // says nothing about the document: an unsearchable parent directory
        // lands here. A different destination answers it, so it is I/O and not
        // a refusal, even though nothing was written yet.
        throw ChipletFormatException("Cannot inspect destination: " + path,
                                     0, "", FailureKind::DestinationIo);

    // Occupied, with no receipt claiming it: settled without resolving anything,
    // so a cyclic or otherwise unresolvable link at the name is still occupied.
    if (fs::exists(status) && !request.expected)
        throw ChipletFormatException("Refusing to overwrite an unrelated destination: " + path,
                                     0, "", FailureKind::DestinationOccupied);

    // From here a receipt is in hand, or the name is free. Both need identity,
    // and a save carrying a receipt keeps exactly the semantics it had.
    const auto canonical = canonical_destination(path);
    if (!fs::exists(status)) {
        if (request.expected)
            throw ChipletFormatException("Refusing to save: recorded destination disappeared: " + path);
        return;
    }
    if (request.expected->canonical_path() != canonical)
        throw ChipletFormatException("Refusing to overwrite an unrelated destination: " + path,
                                     0, "", FailureKind::DestinationOccupied);
    if (!fs::is_regular_file(path, ec) || ec
        || read_binary(path) != request.expected->bytes())
        throw ChipletFormatException("Refusing to save: destination changed externally: " + path);
}

// A z the contract derives from the attach plane (a die's position.z, the
// interposer's attachment_surface_z) goes out in the shortest text that reads
// back as it: 54.8103, not the 54.810299999999998 of 17 significant digits.
// Plain, so every reader takes a number. Every other double keeps yaml-cpp's
// text.
YAML::Emitter& emitPlaneZ(YAML::Emitter& out, double z)
{
    if (!std::isfinite(z)) {
        return out << z;
    }
    return out << shortestText(z);
}

} // namespace

ChipletFormat::ChipletFormat() = default;
ChipletFormat::~ChipletFormat() = default;

const std::vector<ChipletFormat::string_type>& ChipletFormat::warnings() const
{
    return m_warnings;
}

std::unique_ptr<Assembly> ChipletFormat::load(const string_type& path)
{
    const auto identity = canonical_destination(path);
    const auto text = read_binary(path);
    auto assembly = load_buffer(text, path);
    auto state = assembly->document_persistence();
    state.source_path = std::filesystem::absolute(path).lexically_normal().string();
    state.primary = SaveReceipt{state.m_identity, identity, text};
    assembly->set_document_persistence(std::move(state));
    return assembly;
}

std::unique_ptr<Assembly> ChipletFormat::load_buffer(
    const string_type& text, const string_type& path, bool allow_intermediate,
    bool restore_current_state)
{
    // Store base path for relative path resolution.
    std::filesystem::path filePath(path);
    m_basePath = filePath.parent_path().string();
    if (m_basePath.empty()) {
        m_basePath = ".";
    }

    // Parse + validate the document with the vendored Apache-2.0 reference
    // library. It owns the format itself (YAML grammar, the top-level block
    // grammar and its line-break rules, the format_version gate, the
    // intermediate-file guard and required-field validation) and returns a
    // dependency-clean ChipletDocument. Translate its error type into this
    // reader's public exception so callers' contracts hold.
    //
    // What it no longer owns is the enum-like vocabularies. It carries an
    // unrecognised interface type as the string the document wrote and reports
    // it on its warn channel, which is the policy its own header has always
    // declared. This reader still refuses one below, at
    // interface_type_from_string; that half is not fixed here.
    namespace cfio = chiplet_format_io;

    // The reader's non-fatal notes, taken from on_warn and not from
    // doc.warnings, which carries the same text.
    //
    // The reason is not that the header calls on_warn the normative channel,
    // though it does. It is that `loads` builds its ChipletDocument as a local
    // and returns it by value, so the assignment below only happens when the
    // load SUCCEEDS. A document that warns about interface 3 and then throws on
    // interface 5 destroys that document with the note still inside it; the
    // callback has already fired. Reading the vector would lose exactly the
    // notes that accompany a failure, which are the ones worth having.
    //
    // The channel is deliberately undeduplicated, so the coalescing is this
    // host's job: forty unknown interfaces are one line here, not forty.
    m_warnings.clear();
    cfio::LoadOptions opts;
    opts.allow_intermediate = allow_intermediate;
    opts.on_warn = [this](const std::string& note) { m_warnings.push_back(note); };

    cfio::ChipletDocument doc;
    try {
        doc = cfio::loads(text, opts);
    } catch (const cfio::ChipletFormatError& e) {
        for (const auto& note : m_warnings) {
            qWarning("[chiplet] %s", note.c_str());
        }
        throw ChipletFormatException(e.what());
    }

    // One line per distinct note, with a count, rather than one line per event.
    // A channel that repeats itself is a channel people stop reading, and the
    // whole reason to surface these is that an unrecognised vocabulary member
    // is a fact about a document the user may want to act on.
    if (!m_warnings.empty()) {
        std::vector<std::pair<std::string, size_t>> distinct;
        for (const auto& note : m_warnings) {
            auto it = std::find_if(distinct.begin(), distinct.end(),
                                   [&](const auto& d) { return d.first == note; });
            if (it == distinct.end()) distinct.emplace_back(note, 1);
            else ++it->second;
        }
        for (const auto& d : distinct) {
            if (d.second == 1) {
                qWarning("[chiplet] %s", d.first.c_str());
            } else {
                qWarning("[chiplet] %s (x%zu)", d.first.c_str(), d.second);
            }
        }
    }

    auto assembly = std::make_unique<Assembly>();
    assembly->set_document_persistence(capture_context(text, path));

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

        // Orientation. The frame contract defines only face_up (default) and
        // flip_chip; face_down is a non-canonical alias kept working but warned,
        // and any other token warns and stays at the FaceUp default instead of
        // silently rendering un-mirrored. Render mode stays at the constructor
        // default regardless of orientation. Mirrors the unknown-anchor warning
        // below so a lenient viewer is forgiving but never silent.
        if (c.orientation == "flip_chip") {
            component->set_orientation(Orientation::FaceDown);
        } else if (c.orientation == "face_down") {
            qWarning("[chiplet] component '%s': orientation 'face_down' is not a "
                     "canonical token (use 'flip_chip'); treating as flip_chip",
                     c.id.c_str());
            component->set_orientation(Orientation::FaceDown);
        } else if (!c.orientation.empty() && c.orientation != "face_up") {
            qWarning("[chiplet] component '%s': unknown orientation '%s' "
                     "(expected face_up or flip_chip); treating as face_up",
                     c.id.c_str(), c.orientation.c_str());
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
        qWarning("[chiplet] See docs/coord_frame_contract.md section 2 in "
                 "https://github.com/IHP-GmbH/chiplet-system-spec for the "
                 "anchor contract.");
    }

    // --- Interconnect adapter (optional) ---
    // Selects the bumping method whose 3D bodies are merged into the stackup
    // during auto_calculate_z. Its PDK-backed technology is registered like any
    // other technology so viewers list it alongside the die/interposer PDKs; a
    // same-id entry already declared under technologies: wins over it.
    if (doc.interconnect.has_value()) {
        // Gate the id before anything concatenates it into a path. The
        // vendored reader accepts any string (it validates structure, not
        // registry semantics) and cannot be changed here, so the host owns
        // this check. Fail closed and drop the adapter rather than throwing:
        // a bad adapter costs the interconnect axis, not the whole document,
        // and refusing to open a file the schema considers structurally valid
        // would be the reader overreaching.
        if (!is_valid_adapter_id(doc.interconnect->adapter)) {
            qWarning("[chiplet] interconnect.adapter '%s' is not a valid "
                     "adapter id and was IGNORED. An adapter is a registry id "
                     "resolved by the ADK, never a path and never a .drc deck "
                     "name. Interconnect geometry and the interconnect DRC "
                     "axis are off for this document.",
                     doc.interconnect->adapter.c_str());
        } else {
            assembly->set_interconnect_adapter(doc.interconnect->adapter);
            if (doc.interconnect->technology.has_value()
                && !assembly->technology(doc.interconnect->adapter)) {
                assembly->add_technology(
                    build_technology(doc.interconnect->technology.value()));
            }
        }
    }

    // Auto-calculate z for components with connection stacks and z == 0.0.
    // A checkpoint records the CURRENT state, including an intentionally zero z.
    // Normal file opening retains the existing automatic-placement convention.
    if (!restore_current_state) auto_calculate_z(*assembly);

    // --- Interfaces ---
    for (const auto& i : doc.interfaces) {
        // The declared string, carried verbatim whether or not this studio maps
        // it. Refusing here would refuse the whole document over a field no
        // decision in this program reads, and the vendored reader already
        // reports the unmapped value on its warn channel.
        auto iface = std::make_unique<Interface>(
            i.id, InterfaceTypeValue(i.type));
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
    //
    // `flow_yaml` is the SOURCE SLICE, and the slice starts at the `flow:` key
    // line: loading it yields a one-key map, not the block's contents. That is
    // deliberate upstream (it is what lets a host re-emit the block byte for
    // byte instead of re-serialising it, which would re-quote scalars and drop
    // comments), and it is a change of MEANING rather than of type, so the
    // indexing step below is the whole difference between parsing the flow and
    // silently deciding the document has none. Nothing about the old shape
    // failed loudly: parse_flow_definition finds no `steps`, returns an empty
    // FlowDefinition, has_flow() goes false, and the next save writes the user's
    // flow block out of existence.
    //
    // The branch keys on flow_source, which STATES which of the three cases
    // this document is in, and not on an empty flow_yaml, which is only a thing
    // that happens to be true in one of them. An empty slice is a proxy: it
    // cannot tell "the grammar could not delimit the block" from any other
    // reason the bytes might be missing, so a guard built on it prints a
    // specific diagnosis for a condition it never actually established.
    if (doc.flow_source == cfio::FlowSource::NotDelimitable) {
        // The reader met a flow block whose bytes the top-level block grammar
        // could not delimit, so nothing was captured. Reading on is what flow
        // rule 1 requires; what must not happen is a save that quietly drops
        // the block, so the assembly carries the fact to ChipletFormat::save.
        assembly->set_flow_block_uncaptured(true);
        qWarning("%s", ("Chiplet: '" + path + "' has a flow block this reader "
                        "could not delimit, so its contents were not loaded. "
                        "Saving is refused until the block is fixed in the file; "
                        "see the message from save() for what to change.").c_str());
    } else if (doc.has_flow && !doc.flow_yaml.empty()) {
        // Reading the flow must never cost the user the document.
        //
        // resolve_variables throws on a reference it cannot resolve, and a
        // dangling one is easy to arrive at honestly: delete a component the
        // flow names, save (the block is preserved byte for byte, so it still
        // names it), reopen. That used to throw out of load() and the file
        // could not be opened at all, in the editor that would let you fix it.
        // The studio had written a file it then refused to read, and the
        // save-time reader check cannot catch this one because the format
        // treats `flow:` as opaque bytes and never looks inside them.
        //
        // So: keep the bytes, drop only the parsed definition, and say why. The
        // block survives the next save untouched, because writeSourceBytes keys
        // on the bytes and not on has_flow, so nothing the author wrote is lost
        // and the reference can be fixed here or in a text editor. What the
        // user gives up meanwhile is running the flow, which was never going to
        // work with a reference that does not resolve.
        //
        // YAML::Load is inside the guard too: the block is opaque to the
        // reader, so nothing upstream has promised these bytes parse.
        FlowDefinition def;
        try {
            YAML::Node flowNode = YAML::Load(doc.flow_yaml)["flow"];
            FlowConfig flowConfig;
            def = flowConfig.parse_flow_definition(flowNode, *assembly);
        } catch (const std::exception& e) {
            def = FlowDefinition();
            m_warnings.push_back(
                std::string("the flow block could not be read (") + e.what() +
                "), so this document opened without it. The block itself is "
                "intact and saving keeps it exactly as it is; the flow cannot "
                "be run until the reference resolves.");
        }

        // working_directory stays exactly as the document declared it, empty
        // included. It used to be filled in here with this loader's own
        // absolute base path, which is a fact about the machine that opened the
        // file and not about the design; the effective directory is a RUNTIME
        // question, resolved by whoever runs the flow
        // (MainWindow::populateFlowEngine) against the file's own directory.

        // Definition and bytes together. The FlowDefinition is what this
        // program RUNS; the bytes are what it WRITES. They are not the same
        // job, and conflating them is how a round trip loses the half of the
        // block this program does not model. This is the one place that may
        // attach bytes, because this is the one place that has them.
        assembly->set_flow_definition_from_source(std::move(def), doc.flow_yaml);
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

    // layer_properties and stackup only change how the design is drawn, so a
    // path that does not resolve is not a reason to refuse the document. The
    // path stays empty, the verbatim text is kept for the writer, and the
    // reason goes to the views, which fall back and say which fallback.
    if (!tech.layer_properties.empty()) {
        out->set_layer_properties_source(tech.layer_properties);
        std::string why;
        std::string resolvedLypPath = resolve_viewing_path(tech.layer_properties, why);
        out->set_layer_properties_problem(why);
        out->set_layer_properties_path(resolvedLypPath);
    }
    if (!out->layer_properties_path().empty()) {
        const std::string& resolvedLypPath = out->layer_properties_path();

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

    // Explicit stackup YAML: the same ${VAR}/relative chain and the same
    // note-instead-of-refusal as layer_properties.
    if (!tech.stackup.empty()) {
        out->set_stackup_source(tech.stackup);
        std::string why;
        out->set_stackup_path(resolve_viewing_path(tech.stackup, why));
        out->set_stackup_problem(why);
    }

    if (tech.has_dbu) {
        out->set_dbu(tech.dbu);
    }

    return out;
}

void ChipletFormat::auto_calculate_z(Assembly& assembly)
{
    for (auto& comp : assembly.components()) {
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

// qs()/QuotedScalar and the separator-escaping rule now live in
// formats/YamlScalarEmit.h, shared with the flow-block writer so the two
// halves of one document cannot drift apart.

// Shared field emission for a technology entry (used by the technologies:
// map and the interconnect: technology subblock).
static void emit_technology_fields(YAML::Emitter& out, const Technology& tech)
{
    if (!tech.description().empty()) {
        out << YAML::Key << "description" << YAML::Value << qs(tech.description());
    }

    // Keyed on the source as well as the path: a declared path that did not
    // resolve on this host has no path, and must still save back as written.
    if (!tech.layer_properties_source().empty() || !tech.layer_properties_path().empty()) {
        out << YAML::Key << "layer_properties" << YAML::Value
            << qs(tech.layer_properties_source().empty()
                      ? tech.layer_properties_path()
                      : tech.layer_properties_source());
    }

    if (!tech.stackup_source().empty() || !tech.stackup_path().empty()) {
        out << YAML::Key << "stackup" << YAML::Value
            << qs(tech.stackup_source().empty()
                      ? tech.stackup_path()
                      : tech.stackup_source());
    }

    out << YAML::Key << "dbu" << YAML::Value << tech.dbu();
}

namespace {

// Replace `path` with `content` atomically, or leave the existing file
// untouched and throw.
//
// The previous implementation was `std::ofstream file(path); file << text;`
// with no check after the write. That is destructive in the most misleading
// way available: opening truncates the user's file immediately, and if the
// write then fails -- full disk, quota, an ENOSPC that only surfaces at close
// -- the destructor swallows the error, save() returns normally, and the GUI
// reports a successful save over a file that is now empty or half-written.
// The user's only copy of the design is gone and nothing said so. Demonstrated
// against /dev/full and under `ulimit -f`.
//
// The write-temp-then-rename shape fixes that because rename(2) is atomic: an
// observer sees either the whole old file or the whole new one, never a
// truncated one, and a failure anywhere before the rename leaves the original
// exactly as it was.
//
// Details that are load-bearing, not ceremony:
//   * The temp file goes in the TARGET's directory. rename(2) is only atomic
//     within one filesystem, so a temp in /tmp would turn into a copy, which
//     is the truncating behaviour again with extra steps.
//   * fsync(fd) before the rename. Without it the rename can reach disk while
//     the data has not, and a crash leaves a file that is atomically the new
//     name over the old (or empty) content.
//   * fsync on the parent DIRECTORY after the rename, which is what makes the
//     rename itself durable.
//   * mkstemp creates 0600. Left alone, every saved file would silently become
//     private to its owner, so mode is copied from the file being replaced, or
//     derived from the umask the way ofstream would have created it.
//   * A symlinked target is resolved first. ofstream follows symlinks and
//     rename does not, so without this a save through a symlink would replace
//     the link with a regular file.
//
// That last rule is right for the document the user asked to save and WRONG for
// a file we choose the name of ourselves. Following a link means the bytes land
// wherever the link points, which is fine when the user aimed it at their own
// document and unacceptable when the name was picked by this program: the
// rescue sidecar's own name, chosen as path + ".rejected", could be a link
// aimed at the very document the refusal promises is untouched, and then the
// rescue destroys it. So the policy is a parameter and the caller states it.
enum class SymlinkPolicy {
    Follow,   // the user named this path; write through a link as ofstream would
    Replace,  // this program named this path; a link here is replaced, never followed
};

void write_file_atomically(const std::string& path, const std::string& content,
                           SymlinkPolicy policy = SymlinkPolicy::Follow,
                           const std::function<void(const std::string&)>& before_replace = {},
                           bool require_absent = false)
{
    namespace fs = std::filesystem;

    fs::path target(path);
    std::error_code sec;
    const bool pathIsSymlink = fs::is_symlink(target, sec);
    if (policy == SymlinkPolicy::Follow && pathIsSymlink) {
        // Follow a symlinked target so we replace what ofstream would have
        // written through, not the link itself.
        fs::path resolved = fs::weakly_canonical(target, sec);
        if (!sec && !resolved.empty()) {
            target = resolved;
        }
    }

    fs::path dir = target.parent_path();
    if (dir.empty()) {
        dir = fs::path(".");
    }

    // Mode to land on: what the replaced file had, else what a fresh
    // ofstream would have created (0666 masked by the process umask).
    // Under Replace, a link at this name is about to become a regular file, so
    // there is no existing regular file to copy a mode from; stat() would follow
    // the link and hand us the mode of an unrelated file.
    struct ::stat st{};
    const bool targetExisted =
        (policy == SymlinkPolicy::Replace && pathIsSymlink)
            ? false
            : (::stat(target.c_str(), &st) == 0);
    mode_t mode;
    if (targetExisted) {
        mode = st.st_mode & 07777;
    } else {
        const mode_t um = ::umask(0);
        ::umask(um);
        mode = 0666 & ~um;
    }

    std::string tmpl = (dir / (".chiplet-save-XXXXXX")).string();
    std::vector<char> tmpBuf(tmpl.begin(), tmpl.end());
    tmpBuf.push_back('\0');

    const int fd = ::mkstemp(tmpBuf.data());
    if (fd < 0) {
        // Say why a directory permission is the obstacle when the file itself
        // looks writable: the user sees a .chiplet they can edit and would
        // otherwise hunt for the wrong permission.
        throw ChipletFormatException(
            std::string("Could not save '") + path +
                "': saving writes a temporary file in '" + dir.string() +
                "' and then renames it into place, so that directory must be "
                "writable (" + std::strerror(errno) + ")",
            0, "file", FailureKind::DestinationIo);
    }
    const std::string tmpPath(tmpBuf.data());

    // Every failure past this point must remove the temp file and leave the
    // original alone.
    auto fail = [&](const std::string& what, int err) {
        ::close(fd);
        ::unlink(tmpPath.c_str());
        throw ChipletFormatException(
            what + " '" + path + "': " + std::strerror(err), 0, "file",
            FailureKind::DestinationIo);
    };

    const char* data = content.data();
    size_t remaining = content.size();
    while (remaining > 0) {
        const ssize_t n = ::write(fd, data, remaining);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            fail("Could not write", errno);
        }
        data += n;
        remaining -= static_cast<size_t>(n);
    }

    if (::fsync(fd) != 0) {
        fail("Could not flush", errno);
    }
    if (::fchmod(fd, mode) != 0) {
        fail("Could not set permissions on", errno);
    }
    // Keep the group of the file being replaced (shared project directories
    // rely on it). Best effort and deliberately not fatal: refusing the save
    // because a chown was denied would be worse than saving with the caller's
    // own group, and on most systems a non-root user cannot change group at
    // all. Reported once so a surprising group is explainable, never thrown.
    if (targetExisted && ::fchown(fd, static_cast<uid_t>(-1), st.st_gid) != 0) {
        qWarning("[chiplet] saving '%s': could not keep the previous group (%s); "
                 "the file is written with the caller's group instead.",
                 path.c_str(), std::strerror(errno));
    }
    if (::close(fd) != 0) {
        const int err = errno;
        ::unlink(tmpPath.c_str());
        throw ChipletFormatException(
            std::string("Could not close the temporary file for '") + path +
                "': " + std::strerror(err),
            0, "file", FailureKind::DestinationIo);
    }

    try {
        if (before_replace) before_replace(target.string());
    } catch (...) {
        ::unlink(tmpPath.c_str());
        throw;
    }
    // A new destination has no replacement authority. Exclusive publication
    // protects a concurrently created file even after the final admission check.
    const int published = require_absent
        ? ::link(tmpPath.c_str(), target.c_str())
        : ::rename(tmpPath.c_str(), target.c_str());
    if (published != 0) {
        const int err = errno;
        ::unlink(tmpPath.c_str());
        // EEXIST is the one answer here that is not about this filesystem being
        // unable to take the file. Something is in the way, which is the same
        // fact admit_destination reports, discovered later by the one operation
        // that cannot be raced. Answering it with a fallback would write the
        // document into a private directory while never asking the user for the
        // different name that is the actual remedy, and would do it silently
        // because the check that should have caught it is the check that just
        // did. Every other errno means the destination could not take the file,
        // and a different destination may.
        throw ChipletFormatException(
            std::string(require_absent ? "Could not create a new destination without overwriting '"
                                       : "Could not replace '") + path + "': " + std::strerror(err),
            0, "file",
            err == EEXIST ? FailureKind::DestinationOccupied : FailureKind::DestinationIo);
    }
    if (require_absent) ::unlink(tmpPath.c_str());

    // The content is in place and correct from here on. A failure to fsync the
    // directory only means the rename may not survive a power cut, so it is
    // reported and not thrown: telling the caller the save failed after it
    // succeeded is the same class of lie this function exists to remove.
    const int dirFd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY);
    if (dirFd < 0) {
        qWarning("[chiplet] saved '%s' but could not open its directory to "
                 "flush the rename (%s); the file is correct but may not "
                 "survive a power loss.",
                 path.c_str(), std::strerror(errno));
        return;
    }
    // EINVAL here is the normal answer on filesystems that do not support
    // fsync on a directory fd, not a failure. Warning about it would train
    // users to ignore this message on exactly the systems where a real I/O
    // error still needs to be read.
    if (::fsync(dirFd) != 0 && errno != EINVAL) {
        qWarning("[chiplet] saved '%s' but could not flush its directory (%s); "
                 "the file is correct but may not survive a power loss.",
                 path.c_str(), std::strerror(errno));
    }
    ::close(dirFd);
}

}  // namespace

std::string ChipletFormat::serialize(const Assembly& assembly, const string_type& path,
    RejectedOutput rejected_output)
{
    const auto& persistence = assembly.document_persistence();
    if (!persistence.capture_safe)
        throw ChipletFormatException("Refusing to save: source preservation is unsafe ("
            + persistence.capture_reason + "). Your edits remain open; the destination is unchanged.");
    if (!persistence.source_bytes.empty()) {
        const auto original = capture_context(persistence.source_bytes, persistence.source_path);
        if (!original.capture_safe || original.preamble != persistence.preamble
            || original.roots.size() != persistence.roots.size())
            throw ChipletFormatException("Source preservation context no longer matches original bytes");
        for (std::size_t i = 0; i < original.roots.size(); ++i)
            if (original.roots[i].key != persistence.roots[i].key
                || original.roots[i].raw != persistence.roots[i].raw)
                throw ChipletFormatException("Source preservation context changed a captured block");
    }
    YAML::Emitter out;
    out << YAML::BeginMap;

    // The top-level key sequence this function INTENDS to write, recorded as
    // each key is emitted rather than read back out of the result. Reading it
    // back would make the comparison at the end of this function agree with
    // itself on every document. Emitting through `topKey` puts the key literal
    // in one expression that both records and writes, so the two cannot drift;
    // a future top-level `out << YAML::Key << "x"` that skips it produces a key
    // in the split that the intent lacks, and the save refuses. Every way of
    // getting this wrong fails closed.
    std::vector<std::string> intended;
    const auto topKey = [&out, &intended](const char* name) -> YAML::Emitter& {
        intended.emplace_back(name);
        out << YAML::Key << name;
        return out;
    };

    // Format version
    topKey("format_version") << YAML::Value << qs(kSupportedFormatVersion);

    // Assembly metadata
    topKey("assembly") << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "name" << YAML::Value << qs(assembly.name());
    if (!assembly.description().empty()) {
        out << YAML::Key << "description" << YAML::Value << qs(assembly.description());
    }
    if (!assembly.author().empty()) {
        out << YAML::Key << "author" << YAML::Value << qs(assembly.author());
    }
    if (!assembly.created().empty()) {
        out << YAML::Key << "created" << YAML::Value << qs(assembly.created());
    }
    if (!assembly.modified().empty()) {
        out << YAML::Key << "modified" << YAML::Value << qs(assembly.modified());
    }
    out << YAML::Key << "units" << YAML::Value << qs(assembly.units());
    if (!assembly.assembly_gds().empty()) {
        out << YAML::Key << "assembly_gds" << YAML::Value
            << qs(assembly.assembly_gds_source().empty()
                      ? assembly.assembly_gds()
                      : assembly.assembly_gds_source());
    }
    if (!assembly.io_technology().empty()) {
        out << YAML::Key << "io_technology" << YAML::Value << qs(assembly.io_technology());
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
        topKey("technologies") << YAML::Value << YAML::BeginMap;

        for (const auto& tech : assembly.technologies()) {
            if (!icAdapter.empty() && tech->id() == icAdapter) {
                continue;
            }
            out << YAML::Key << qs(tech->id()) << YAML::Value << YAML::BeginMap;
            emit_technology_fields(out, *tech);
            out << YAML::EndMap;
        }

        out << YAML::EndMap;
    }

    // Interconnect (assembly-level bumping method + its PDK-backed identity)
    if (!icAdapter.empty()) {
        topKey("interconnect") << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "adapter" << YAML::Value << qs(icAdapter);

        const Technology* ictech = assembly.technology(icAdapter);
        if (ictech) {
            out << YAML::Key << "technology" << YAML::Value << YAML::BeginMap;
            emit_technology_fields(out, *ictech);
            out << YAML::EndMap;
        }

        out << YAML::EndMap;
    }

    // Connection stacks
    if (!assembly.connection_stacks().empty()) {
        topKey("connection_stacks") << YAML::Value << YAML::BeginMap;

        for (const auto& [stackId, stack] : assembly.connection_stacks()) {
            out << YAML::Key << qs(stackId) << YAML::Value << YAML::BeginMap;

            if (!stack.description.empty()) {
                out << YAML::Key << "description" << YAML::Value << qs(stack.description);
            }

            if (!stack.layers.empty()) {
                out << YAML::Key << "layers" << YAML::Value << YAML::BeginSeq;
                for (const auto& layer : stack.layers) {
                    out << YAML::Flow << YAML::BeginMap;
                    out << YAML::Key << "name" << YAML::Value << qs(layer.name);
                    out << YAML::Key << "material" << YAML::Value << qs(layer.material);
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
        topKey("components") << YAML::Value << YAML::BeginSeq;

        for (const auto& comp : assembly.components()) {
            out << YAML::BeginMap;
            out << YAML::Key << "id" << YAML::Value << qs(comp->id());
            out << YAML::Key << "type" << YAML::Value << qs(component_type_to_string(comp->type()));

            // Anchor convention is part of the canonical contract and
            // must always be emitted explicitly so downstream readers
            // never have to guess (see coord_frame_contract.md §2,
            // §4 Writer Contract).
            out << YAML::Key << "anchor" << YAML::Value
                << qs(anchor_to_string(comp->anchor()));

            if (!comp->technology().empty()) {
                out << YAML::Key << "technology" << YAML::Value << qs(comp->technology());
            }

            if (!comp->connection().empty()) {
                out << YAML::Key << "connection" << YAML::Value << qs(comp->connection());
            }

            if (!comp->layout_path().empty()) {
                out << YAML::Key << "layout" << YAML::Value
                    << qs(comp->layout_path_source().empty()
                              ? comp->layout_path()
                              : comp->layout_path_source());
            }

            // Save cells - use 'cells' array for multi-cell, 'top_cell' for single (backward compat)
            const auto& cells = comp->cells();
            if (!cells.empty()) {
                if (cells.size() == 1) {
                    // Single cell: use legacy 'top_cell' for backward compatibility
                    out << YAML::Key << "top_cell" << YAML::Value << qs(cells[0]);
                } else {
                    // Multiple cells: use new 'cells' array
                    out << YAML::Key << "cells" << YAML::Value << YAML::Flow << YAML::BeginSeq;
                    for (const auto& cell : cells) {
                        out << qs(cell);
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
                out << YAML::Key << "z" << YAML::Value;
                emitPlaneZ(out, pos.z);
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
                out << YAML::Key << "orientation" << YAML::Value << qs("flip_chip");
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
                out << YAML::Key << "attachment_surface_z" << YAML::Value;
                emitPlaneZ(out, comp->attachment_surface_z().value());
            }

            // Array configuration
            if (comp->is_array()) {
                const auto& arr = comp->array().value();
                out << YAML::Key << "array" << YAML::Value << YAML::BeginMap;
                out << YAML::Key << "pattern" << YAML::Value << qs(arr.pattern);
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
                    out << YAML::Key << qs(key) << YAML::Value << qs(value);
                }
                out << YAML::EndMap;
            }

            // I/O pads
            if (!comp->io_pads().empty()) {
                out << YAML::Key << "io_pads" << YAML::Value << YAML::BeginSeq;
                for (const auto& pad : comp->io_pads()) {
                    out << YAML::BeginMap;
                    out << YAML::Key << "id" << YAML::Value << qs(pad.id());
                    // Only what the author declared. io_class and layer are
                    // optional in the schema, and emitting a value for a pad
                    // that omitted one turns "unspecified" into "declared":
                    // validation rule 8 is vacuous without an io_class and
                    // skips a pad whose layer is not the interface's port
                    // layer, so a stamped wire_bond/TopMetal2 can make the rule
                    // bind on a document its author never wrote that way.
                    if (pad.io_class()) {
                        out << YAML::Key << "io_class" << YAML::Value
                            << qs(io_class_to_string(*pad.io_class()));
                    }
                    if (!pad.net().empty()) {
                        out << YAML::Key << "net" << YAML::Value << qs(pad.net());
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
                    if (pad.layer()) {
                        out << YAML::Key << "layer" << YAML::Value
                            << qs(*pad.layer());
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
        topKey("interfaces") << YAML::Value << YAML::BeginSeq;

        for (const auto& iface : assembly.interfaces()) {
            out << YAML::BeginMap;
            out << YAML::Key << "id" << YAML::Value << qs(iface->id());
            // The declared string, not a re-derivation from the mapping: a
            // document that said solder_bump says solder_bump afterwards, and
            // one that said something this studio does not know keeps saying it.
            out << YAML::Key << "type" << YAML::Value << qs(iface->type().declared());

            // From endpoint
            const auto& from = iface->from();
            if (!from.component.empty()) {
                out << YAML::Key << "from" << YAML::Value << YAML::Flow << YAML::BeginMap;
                out << YAML::Key << "component" << YAML::Value << qs(from.component);
                out << YAML::Key << "surface" << YAML::Value << qs(from.surface);
                out << YAML::Key << "port_layer" << YAML::Value << qs(from.portLayer);
                out << YAML::EndMap;
            }

            // To endpoint
            const auto& to = iface->to();
            if (!to.component.empty()) {
                out << YAML::Key << "to" << YAML::Value << YAML::Flow << YAML::BeginMap;
                out << YAML::Key << "component" << YAML::Value << qs(to.component);
                out << YAML::Key << "surface" << YAML::Value << qs(to.surface);
                out << YAML::Key << "port_layer" << YAML::Value << qs(to.portLayer);
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
        topKey("netlist") << YAML::Value << YAML::BeginMap;

        const auto& nl = assembly.netlist();
        if (nl.net_count() > 0) {
            out << YAML::Key << "nets" << YAML::Value << YAML::BeginSeq;

            for (const auto& net : nl.nets()) {
                out << YAML::BeginMap;
                out << YAML::Key << "name" << YAML::Value << qs(net.name());
                out << YAML::Key << "class" << YAML::Value << qs(net_class_to_string(net.net_class()));

                if (net.external()) {
                    out << YAML::Key << "external" << YAML::Value << net.external();
                }

                if (net.connection_count() > 0) {
                    out << YAML::Key << "connections" << YAML::Value << YAML::BeginSeq;
                    for (const auto& conn : net.connections()) {
                        out << YAML::Flow << YAML::BeginMap;
                        out << YAML::Key << "component" << YAML::Value << qs(conn.component);
                        out << YAML::Key << "pin" << YAML::Value << qs(conn.pin);
                        if (!conn.layer.empty()) {
                            out << YAML::Key << "layer" << YAML::Value << qs(conn.layer);
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
            out << YAML::Key << "external_netlist" << YAML::Value << qs(nl.external_netlist_path());
        }

        out << YAML::EndMap;
    }

    // Flow definition
    //
    // Refuse rather than drop. This assembly came from a document whose flow
    // block the reader could not delimit, so its contents were never loaded and
    // there is nothing here to write. The two alternatives are both silent
    // lies: omit the block, which deletes the user's build configuration, or
    // invent one from an empty FlowDefinition, which replaces it with something
    // they did not write. The message names the two things that actually cause
    // this, because the fix is a one-character edit in a file the user owns and
    // an "unsupported document" would send them looking somewhere else.
    if (assembly.flow_block_uncaptured()) {
        throw ChipletFormatException(
            "Refusing to save: this document's flow block was never loaded, so "
            "saving would delete it. The block is not written as a bare `flow:` "
            "key line at column zero (a space before the colon is enough), or "
            "the file has a quoted key at column zero somewhere. Fix either in "
            "the .chiplet and reopen it.",
            0, "flow");
    }
    // A flow block that was READ is written back as the bytes it was read from,
    // and only one that this host authored is serialized from the model.
    //
    // Those are the two writable states the format defines, and the reason for
    // the split is that re-serialising a parsed block is lossy in ways nothing
    // reports. Measured on a document with a comment, an unmodelled `parallelism`
    // key and an unmodelled `retries` key: the comment goes, both keys go, and
    // `DEBUG: "1"` comes back as `DEBUG: 1`, which PyYAML then reads as the
    // integer 1 rather than the string the author wrote. The last one is not a
    // fidelity nicety, it is a value changing type on the way through.
    //
    // The bytes are appended after the emitter's document rather than pushed
    // through it, because that is what they are: a top-level block, captured
    // whole, that no emitter needs to understand. Anything else re-quotes them.
    const bool writeSourceBytes = !assembly.flow_source_text().empty();
    if (writeSourceBytes) {
        // These bytes are appended AFTER the emitter's document, so the emitter
        // has made no statement about them and this function cannot claim they
        // carry a `flow:` block just because that is where they came from.
        // Establish it with the format's own grammar first.
        //
        // Bytes the reader produced always satisfy this: flow_yaml is set only
        // from a block the splitter named `flow`, and any later key line would
        // have ended that block. But set_flow_definition_from_source is public
        // (core/Assembly.h) and takes any string, so the guarantee belongs to a
        // caller rather than to the type.
        //
        // This is not intent derived from output. It runs on an INPUT this
        // function did not author, before any output exists.
        std::string flowBytesProblem;
        try {
            const auto slice = chiplet_format_io::split_top_level_keys(
                assembly.flow_source_text());
            if (!slice.not_splittable.empty()) {
                flowBytesProblem = slice.not_splittable;
            } else if (slice.keys.empty()) {
                flowBytesProblem = "they begin no top-level block at all (the "
                                   "flow VALUE without its `flow:` key line is "
                                   "the pre-slice spelling this reader stopped "
                                   "producing)";
            } else if (slice.keys.front() != "flow") {
                flowBytesProblem = "they begin with `" + slice.keys.front() +
                                   ":`, not `flow:`";
            }
            // Deliberately NOT checking that this is the ONLY block here.
            // Extra blocks behind the flow slice are caught by the key-sequence
            // comparison at the end of this function, and that path is the only
            // one a test can drive from outside: every key this function emits
            // is a literal and every value goes out double-quoted, so nothing
            // else in the output can grow a key. Refusing here as well would
            // leave the comparison, and the call to it, with no witness.
        } catch (const chiplet_format_io::ChipletFormatError& e) {
            flowBytesProblem = e.what();
        }
        if (!flowBytesProblem.empty()) {
            // No sidecar. Nothing has been serialized, `path` is untouched and
            // the assembly is intact, so there is nothing to rescue; and the
            // fault is in what was handed to this assembly, not in the document
            // the user is editing.
            throw ChipletFormatException(
                std::string("Refusing to save: the flow bytes attached to this "
                            "assembly are not the flow block they are written "
                            "back as (") + flowBytesProblem + ").",
                0, "flow");
        }
    } else if (assembly.has_flow()) {
        // Authored in memory, so this host owns the bytes and may write them.
        //
        // Recorded, not emitted, because write_flow_definition emits the `flow`
        // key itself (core/flow/FlowConfig.cpp). That is a claim about another
        // translation unit, and it is CHECKED rather than trusted: if that
        // function ever stops emitting the key, the split will not contain it,
        // the sequences will differ and this save will refuse.
        intended.emplace_back("flow");
        FlowConfig flowConfig;
        flowConfig.write_flow_definition(assembly.flow_definition(), out);
    }

    out << YAML::EndMap;

    std::vector<PreservedRoot> carried;
    bool sourceFlowPlaced = false;
    for (const auto& block : persistence.roots) {
        if (block.key == "flow") {
            if (writeSourceBytes) {
                carried.push_back({"flow", assembly.flow_source_text()});
                sourceFlowPlaced = true;
            }
        } else {
            // Even a publicly supplied persistence record cannot claim an owned root.
            if (writer_owned(block.key))
                throw ChipletFormatException("Invalid opaque-root ownership: " + block.key);
            carried.push_back(block);
        }
    }
    if (writeSourceBytes && !sourceFlowPlaced)
        carried.insert(carried.begin(), {"flow", assembly.flow_source_text()});
    for (const auto& block : carried) intended.push_back(block.key);

    // Frozen before `text` exists, so nothing downstream can append to it from
    // what was produced.
    const std::vector<std::string> intendedKeys = std::move(intended);

    // Write to file. Atomic replace: the user's existing .chiplet survives
    // untouched if anything about this write fails (see
    // write_file_atomically). A plain ofstream truncates on open and reports
    // success even when the write never landed.
    if (!out.good()) {
        throw ChipletFormatException(
            std::string("Refusing to save: the emitter failed to serialize the "
                        "assembly (") + out.GetLastError() + ")",
            0, "file");
    }

    std::string text = persistence.preamble + std::string(out.c_str(), out.size());
    for (std::size_t i = 0; i < carried.size(); ++i) {
        if (!text.empty() && text.back() != '\n') text += '\n';
        text += carried[i].raw;
        if (i + 1 < carried.size() && (text.empty() || text.back() != '\n'))
            throw ChipletFormatException("Refusing to save: a carried block without a final "
                "newline cannot precede another block without changing its bytes");
    }

    // One refusal shape for both save-time checks below.
    //
    // Loss-free by construction, not by argument.
    //
    // Nothing has been written to `path` yet, so the previous file is
    // byte-identical and the in-memory assembly is untouched. That is not
    // enough on its own: a user whose only copy of an hour's work is in RAM
    // does not care that the old file survived. So the bytes go beside the
    // target before the refusal is raised.
    //
    // The name is chosen so that no *.chiplet glob matches it. Every sweep in
    // this ecosystem, adk-tools and interposer-pnr included, globs for
    // .chiplet, and a document a check has just refused is the last thing any
    // of them should pick up as input.
    //
    // Failing to write the sidecar changes the MESSAGE and never the refusal.
    // A check that downgrades itself to a warning when its rescue fails is not
    // a check.
    // Returns the exception rather than throwing it, so every call site reads
    // `throw rejection(...)` and the compiler can see that control leaves.
    const auto rejection = [&](const std::string& headline,
                               const std::string& detail) {
        const std::string sidecar = path + ".rejected";
        bool sidecarWritten = true;
        try {
            // Suppress is not "try and forgive a failure": it must not attempt
            // the write at all, or a caller that asked for no debris would
            // still create the file whenever the write happened to succeed,
            // which is every time it matters.
            if (rejected_output == RejectedOutput::Suppress)
                throw ChipletFormatException("Rejected output suppressed by request");
            if (path.empty()) throw ChipletFormatException("No recovery sidecar destination");
            // Replace, not Follow: this program chose this name, so a link
            // sitting on it must not aim the rescue at some other file, least
            // of all the document we just promised was untouched.
            write_file_atomically(sidecar, text, SymlinkPolicy::Replace);
        } catch (const std::exception&) {
            sidecarWritten = false;
        }
        std::string msg = "Refusing to save: " + headline + ", so '" + path +
                          "' is unchanged on disk and your work is still open.\n\n" +
                          detail + "\n\n";
        // Three outcomes, three sentences. Suppressed is not the same as
        // failed, and telling someone their text "could not be written" when
        // the caller asked for it not to be written sends them looking for a
        // disk problem that does not exist.
        if (sidecarWritten) {
            msg += "The rejected text was written to '" + sidecar +
                   "' so none of it is lost. That name is deliberate: nothing "
                   "in this ecosystem reads it as input.";
        } else if (rejected_output == RejectedOutput::Suppress) {
            msg += "No rejected-text file was written, by request of the caller: "
                   "this document was derived from a source you still have, so "
                   "there is nothing here that only existed in this session.";
        } else {
            msg += "The rejected text could not be written beside the target "
                   "either, so it exists only in this session. Do not close "
                   "this window before saving somewhere that works.";
        }
        return ChipletFormatException(msg, 0, "format");
    };

    // Does the document this is about to write carry the top-level keys it was
    // built from, in that order?
    //
    // This is a different question from the reader check below and it runs
    // first, because a mismatch here means THIS writer is broken, which is a
    // more urgent diagnosis than "the reader dislikes this document". A dropped
    // optional block also loads perfectly well, so the reader check would pass
    // and the finding would be lost.
    //
    // The comparison itself is in TopLevelKeyIntent.h, out of reach of the
    // emitter, so the expected sequence cannot quietly become a re-read of the
    // output. See the comment there.
    {
        const std::string mismatch =
            check_top_level_key_sequence(intendedKeys, text);
        if (!mismatch.empty()) {
            throw rejection("the document this would write does not carry "
                            "the top-level keys it was built from",
                            "The format's own grammar reads:\n  " + mismatch);
        }
    }

    const auto attested = chiplet_format_io::capture_top_level_blocks(text);
    if (!attested.safe || attested.preamble != persistence.preamble)
        throw rejection("the composed document cannot be safely captured",
                        attested.reason_code + ": " + attested.reason);
    for (const auto& block : carried) {
        const auto found = std::find_if(attested.blocks.begin(), attested.blocks.end(),
            [&](const auto& candidate) { return candidate.key == block.key; });
        if (found == attested.blocks.end() || found->raw != block.raw)
            throw rejection("a carried block changed bytes", block.key);
    }

    // Read back what is about to be written, through the reader itself, and
    // refuse rather than write a document that reader rejects.
    //
    // Why the reader and not a rule-by-rule check on the model: the reader
    // gains rules and this emitter never hears about it. That is not a
    // hypothetical, it is the shape of the re-vendor at 7caeaa9, where the
    // library learned validation rule 8 (a pad's io_class must allow its
    // interface's type on a shared port layer) and this file learned nothing. A
    // check written against today's rule list would go stale on exactly the
    // commits it exists to survive. Running the reader cannot go stale, because
    // it IS the thing it is checking against.
    //
    // It also reaches what no model-side check could. check_line_breaks, the
    // repeated-top-level-key scan and the concatenation of the appended flow
    // slice are properties of the TEXT, and the text is what this function
    // builds.
    //
    // Deliberately NOT sharing a LoadOptions object with load() above, even
    // though it would look tidier. The two ask different questions. load() asks
    // "can this host open this document" and refuses an intermediate; this asks
    // "is this a legal .chiplet", and an intermediate is one. The vendored
    // library draws the same line inside its own writer, which validates with
    // allow_intermediate = true and says so in a comment. One shared object
    // would make this host quietly disagree with the library it vendors.
    //
    // on_warn stays unset. The notes are about a document the user already has
    // open, load() reported them once, and repeating them on every five-minute
    // autosave is how a channel stops being read.
    {
        chiplet_format_io::LoadOptions checkOpts;
        checkOpts.validate = true;
        checkOpts.allow_intermediate = true;
        try {
            (void)chiplet_format_io::loads(text, checkOpts);
        } catch (const chiplet_format_io::ChipletFormatError& e) {
            // The message says what was rejected, not that the file "cannot be
            // reopened". This check is necessary for reopening, not sufficient:
            // a document full of ${VAR} paths passes it and can still fail on
            // open, because path resolution happens in this host and never in
            // the library. Claiming more than was tested is how an error
            // message sends someone to the wrong place.
            throw rejection(
                "the format's reader rejects the document this would write",
                std::string("The reader says:\n  ") + e.what());
        }
    }

    return text;
}

SaveReceipt ChipletFormat::save_snapshot(const Assembly& assembly,
    const string_type& path, const SaveRequest& request)
{
    if (request.expected && request.expected->m_identity != assembly.document_persistence().m_identity)
        throw ChipletFormatException("Refusing a save receipt issued for another document");
    // No mutable model reference enters serialization or admission.
    const auto text = serialize(assembly, path, request.rejected_output);
    admit_destination(path, request);
    const auto canonical = canonical_destination(path);
    write_file_atomically(path, text, SymlinkPolicy::Follow, [&](const std::string& actual_target) {
        if (canonical_destination(path) != canonical
            || canonical_destination(actual_target) != canonical)
            throw ChipletFormatException("Destination identity changed while saving: " + path);
        admit_destination(path, request);
    }, !request.expected.has_value());
    return SaveReceipt{assembly.document_persistence().m_identity, canonical, text};
}

SaveReceipt ChipletFormat::save(const Assembly& assembly, const string_type& path)
{
    SaveRequest request;
    const auto& state = assembly.document_persistence();
    if (state.primary && state.primary->canonical_path() == canonical_destination(path))
        request.expected = state.primary;
    return save_snapshot(assembly, path, request);
}

void ChipletFormat::save(Assembly& assembly, const string_type& path, SavePurpose purpose)
{
    auto state = assembly.document_persistence();
    const auto canonical = canonical_destination(path);
    const auto& previous = purpose == SavePurpose::Primary ? state.primary : state.autosave;
    SaveRequest request;
    if (previous && previous->canonical_path() == canonical) request.expected = previous;
    if (purpose == SavePurpose::Autosave && state.primary
        && state.primary->canonical_path() == canonical)
        throw ChipletFormatException("Autosave cannot replace the primary document");
    if (purpose == SavePurpose::Primary && !state.source_path.empty()
        && std::filesystem::path(canonical).parent_path()
            != std::filesystem::path(canonical_destination(state.source_path)).parent_path())
        qWarning("[chiplet] Save As preserves relative asset references without rebasing; "
                 "reopening from a different directory may resolve different assets");
    const auto receipt = save_snapshot(static_cast<const Assembly&>(assembly), path, request);
    if (purpose == SavePurpose::Primary) state.primary = receipt;
    else state.autosave = receipt;
    assembly.set_document_persistence(std::move(state));
}

namespace {
QString recovery_bytes(const std::string& bytes)
{
    return QString::fromLatin1(QByteArray(bytes.data(), static_cast<int>(bytes.size())).toBase64());
}

std::string recovery_bytes(const QJsonValue& value)
{
    if (!value.isString()) throw ChipletFormatException("Recovery byte field is not a string");
    const auto encoded = value.toString().toLatin1();
    const auto bytes = QByteArray::fromBase64(encoded);
    if (QString::fromLatin1(encoded) != value.toString() || bytes.toBase64() != encoded)
        throw ChipletFormatException("Recovery byte field is not canonical base64");
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

QJsonValue recovery_receipt(const std::optional<SaveReceipt>& receipt)
{
    if (!receipt) return QJsonValue(QJsonValue::Null);
    return QJsonObject{{"path", recovery_bytes(receipt->canonical_path())},
                       {"bytes", recovery_bytes(receipt->bytes())}};
}

std::optional<std::pair<std::string, std::string>> recovery_receipt(const QJsonValue& value)
{
    if (value.isNull()) return std::nullopt;
    if (!value.isObject()) throw ChipletFormatException("Recovery receipt is not an object");
    const auto object = value.toObject();
    if (object.size() != 2 || !object.contains("path") || !object.contains("bytes"))
        throw ChipletFormatException("Recovery receipt has unexpected fields");
    std::pair<std::string, std::string> receipt{
        recovery_bytes(object.value("path")), recovery_bytes(object.value("bytes"))};
    if (receipt.first.empty() || receipt.first.find('\0') != std::string::npos
        || !std::filesystem::path(receipt.first).is_absolute()
        || std::filesystem::path(receipt.first).lexically_normal().string() != receipt.first)
        throw ChipletFormatException("Recovery destination identity is not an absolute normalized path");
    // Re-attest baseline bytes without reading or granting authority over a disk target.
    chiplet_format_io::LoadOptions opts;
    opts.allow_intermediate = true;
    (void)chiplet_format_io::loads(receipt.second, opts);
    if (!chiplet_format_io::capture_top_level_blocks(receipt.second).safe)
        throw ChipletFormatException("Recovery destination baseline is not safely captured");
    return receipt;
}
} // namespace

std::string ChipletFormat::encode_recovery(const Assembly& assembly)
{
    const auto current = serialize(assembly, {});
    const auto& state = assembly.document_persistence();
    QJsonObject object{{"version", 1}, {"current", recovery_bytes(current)},
        {"source_path", recovery_bytes(state.source_path)},
        {"source_bytes", recovery_bytes(state.source_bytes)},
        {"primary", recovery_receipt(state.primary)},
        {"autosave", recovery_receipt(state.autosave)}};
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

std::unique_ptr<Assembly> ChipletFormat::decode_recovery(const std::string& payload)
{
    try {
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(
        QByteArray(payload.data(), static_cast<int>(payload.size())), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        throw ChipletFormatException("Recovery payload is not a complete JSON object");
    const auto object = document.object();
    // The encoder's one canonical JSON spelling also rejects duplicate keys,
    // which QJsonDocument alone would otherwise silently collapse.
    if (document.toJson(QJsonDocument::Compact) != QByteArray(payload.data(), static_cast<int>(payload.size())))
        throw ChipletFormatException("Recovery payload is not the canonical encoded checkpoint");
    if (object.size() != 6 || object.value("version") != QJsonValue(1)
        || !object.contains("primary") || !object.contains("autosave"))
        throw ChipletFormatException("Unsupported recovery payload version or fields");
    const auto current = recovery_bytes(object.value("current"));
    const auto sourcePath = recovery_bytes(object.value("source_path"));
    const auto sourceBytes = recovery_bytes(object.value("source_bytes"));
    if (sourcePath.find('\0') != std::string::npos
        || (!sourcePath.empty() && !std::filesystem::path(sourcePath).is_absolute()))
        throw ChipletFormatException("Recovery source context must be an absolute path");
    if (sourcePath.empty() != sourceBytes.empty())
        throw ChipletFormatException("Recovery source identity is incomplete");
    DocumentPersistence state;
    if (!sourceBytes.empty()) {
        state = capture_context(sourceBytes, sourcePath);
        chiplet_format_io::LoadOptions opts;
        opts.allow_intermediate = true;
        (void)chiplet_format_io::loads(sourceBytes, opts);
        if (!state.capture_safe)
            throw ChipletFormatException("Recovery original source cannot be preserved: " + state.capture_reason);
    }
    const auto restoreReceipt = [&](const QJsonValue& value) -> std::optional<SaveReceipt> {
        const auto raw = recovery_receipt(value);
        if (!raw) return std::nullopt;
        const auto baseline = capture_context(raw->second, raw->first);
        std::vector<PreservedRoot> expected, actual;
        for (const auto& block : state.roots)
            if (block.key != "flow") expected.push_back(block);
        for (const auto& block : baseline.roots)
            if (block.key != "flow") actual.push_back(block);
        if (!baseline.capture_safe || baseline.preamble != state.preamble || actual.size() != expected.size())
            throw ChipletFormatException("Recovery receipt does not preserve the original opaque context");
        for (std::size_t i = 0; i < expected.size(); ++i)
            if (expected[i].key != actual[i].key || expected[i].raw != actual[i].raw)
                throw ChipletFormatException("Recovery receipt changed original unsupported-root bytes");
        return SaveReceipt{state.m_identity, raw->first, raw->second};
    };
    state.primary = restoreReceipt(object.value("primary"));
    state.autosave = restoreReceipt(object.value("autosave"));
    if (state.primary && state.autosave
        && state.primary->canonical_path() == state.autosave->canonical_path())
        throw ChipletFormatException("Recovery primary and autosave identities collide");
    auto assembly = load_buffer(current, sourcePath.empty() ? "recovered.chiplet" : sourcePath, true, true);
    const auto& currentState = assembly->document_persistence();
    if (!currentState.capture_safe || currentState.preamble != state.preamble)
        throw ChipletFormatException("Recovered document lost the source preamble or safe capture");
    std::vector<PreservedRoot> originalOpaque, currentOpaque;
    for (const auto& block : state.roots)
        if (block.key != "flow") originalOpaque.push_back(block);
    for (const auto& block : currentState.roots)
        if (block.key != "flow") currentOpaque.push_back(block);
    if (originalOpaque.size() != currentOpaque.size())
        throw ChipletFormatException("Recovered document changed unsupported root inventory");
    for (std::size_t i = 0; i < originalOpaque.size(); ++i)
        if (originalOpaque[i].key != currentOpaque[i].key || originalOpaque[i].raw != currentOpaque[i].raw)
            throw ChipletFormatException("Recovered document changed opaque root bytes");
    // Current flow is restored by load_buffer. Original bytes are never promoted
    // to the edited document, nor edited bytes to a destination baseline.
    assembly->set_document_persistence(std::move(state));
    return assembly;
    } catch (const chiplet_format_io::ChipletFormatError& error) {
        throw ChipletFormatException(error.what(), 0, "recovery");
    }
}

namespace {

// Ecosystem-root variables accepted inside .chiplet path entries. Each maps
// to the directory names walked for next to the .chiplet file (canonical
// name first, then the GitHub repo name a default clone produces) plus the
// marker subpath that must exist under the root. Mirrors the Python reader
// (chiplet_kicad_plugin/hyp_to_gds.py _PATH_VAR_MARKERS). The discovery
// convention is environment -> sibling-checkout walk -> loud failure, and
// this table is its source of truth on this side: no document restates it,
// so a name absent from here is a name this host will not expand.
struct PathVarMarker {
    const char* name;
    const char* dirnames[3]; // walk candidates, nullptr-terminated; none = env only
    const char* marker;      // subpath under the root that must exist ("" = the root)
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
    // GF180MCU PDK, holding gf180mcuD/. "gf180mcu" is how the adk-tools image
    // names it beside Studio; wafer-space-gf180mcu is the upstream checkout.
    { "GF180MCU_PDK_ROOT", { "gf180mcu", "wafer-space-gf180mcu" },
      "gf180mcuD/libs.tech/klayout" },
    // A Chiplet Studio checkout or install, for its bundled stackups.
    { "CHIPLET_STUDIO_ROOT", { "chiplet-studio" }, "configs/stackups" },
    // A project's own technology files (<id>/layers.lyp, <id>/stackup.yaml).
    // Set by whoever runs the flow; there is no checkout to walk for, so the
    // environment is the only source and the marker is the folder itself.
    { "CHIPLET_FLOW_TECH", { nullptr }, "" },
};

// The table is the whole vocabulary. A name that is not in it has no marker to
// validate against, so there is no way to tell a root from any other string,
// and the only safe answer is to refuse it rather than substitute it.
const PathVarMarker* find_path_var_marker(const std::string& name)
{
    for (const auto& m : kPathVarMarkers) {
        if (name == m.name) {
            return &m;
        }
    }
    return nullptr;
}

// Resolve a KNOWN ecosystem root. Callers must have looked the name up in the
// table first; an unknown name never reaches here.
std::string discover_path_var(const PathVarMarker& marker,
                              const std::string& name,
                              const std::filesystem::path& startDir)
{
    // A set-and-valid environment value wins; set-but-invalid (marker
    // subpath missing) falls through to the walk, mirroring the Python side.
    //
    // The marker check is unconditional. It used to be skipped when the name
    // had no table entry, which meant the one case with nothing to validate
    // against was also the one case that was substituted unchecked: a name the
    // table never vetoed sailed through, while a name it did veto was refused.
    // Unknown names are now refused before this function is reached, so the
    // check has no way to be bypassed.
    if (const char* env = std::getenv(name.c_str())) {
        // Use the error_code overload: the throwing one would let an ELOOP/EACCES
        // on a hostile env value escape load() as a filesystem_error instead of
        // the documented ChipletFormatException. On any OS error treat the env
        // candidate as invalid and fall through to the walk.
        std::error_code ec_env;
        if (env[0] != '\0'
            && std::filesystem::is_directory(
                   std::filesystem::path(env) / marker.marker, ec_env)) {
            return env;
        }
    }

    const PathVarMarker* const markerPtr = &marker;
    if (markerPtr->dirnames[0] == nullptr) {
        return {};
    }

    std::error_code ec;
    std::filesystem::path base = std::filesystem::absolute(startDir, ec);
    if (ec) {
        base = startDir;
    }
    while (!base.empty()) {
        for (const char* dirname : markerPtr->dirnames) {
            if (dirname == nullptr) {
                break;
            }
            std::filesystem::path cand = base / dirname;
            std::error_code ec_cand;
            if (std::filesystem::is_directory(cand / markerPtr->marker, ec_cand)) {
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

// Why a ${VAR} reference did not expand. Kept as data so each caller words it
// for its own reader: load() refuses with the long text, and the two viewing
// fields turn the short text into an on-screen note.
struct PathVarFailure {
    enum class Kind { None, Unknown, Unresolved };
    Kind kind = Kind::None;
    std::string name;
    const PathVarMarker* marker = nullptr;
};

// Expand every ${VAR} in `path`. On the first reference that cannot expand,
// stop, fill `fail` and return an empty string.
std::string expand_path_vars_into(const std::string& path,
                                  const std::filesystem::path& base,
                                  PathVarFailure& fail)
{
    std::string out = path;
    size_t pos = 0;
    while ((pos = out.find("${", pos)) != std::string::npos) {
        size_t end = out.find('}', pos + 2);
        if (end == std::string::npos) {
            break; // unterminated reference: pass through untouched
        }
        std::string name = out.substr(pos + 2, end - pos - 2);
        const PathVarMarker* marker = find_path_var_marker(name);
        if (marker == nullptr) {
            fail = {PathVarFailure::Kind::Unknown, name, nullptr};
            return {};
        }
        std::string value = discover_path_var(*marker, name, base);
        if (value.empty()) {
            fail = {PathVarFailure::Kind::Unresolved, name, marker};
            return {};
        }
        out.replace(pos, end - pos + 1, value);
        pos += value.size();
    }
    return out;
}

// The one-line reason a viewing note gives. Says what the variable holds, not
// what to do about it; the note's second half names the fallback in use.
std::string short_reason(const PathVarFailure& fail)
{
    const std::string ref = "${" + fail.name + "}";
    if (fail.kind == PathVarFailure::Kind::Unknown) {
        return ref + " is not a path variable Studio knows";
    }
    const PathVarMarker& m = *fail.marker;
    const bool envOnly = m.dirnames[0] == nullptr;
    const char* env = std::getenv(fail.name.c_str());
    std::string why;
    if (env == nullptr || env[0] == '\0') {
        why = ref + " is not set";
    } else if (m.marker[0] == '\0') {
        why = ref + " is set to '" + env + "', which is not a folder";
    } else {
        why = ref + " is set to '" + env + "', which has no " + m.marker;
    }
    if (!envOnly) {
        why += std::string(", and no ") + m.dirnames[0] + " checkout was found above the file";
    }
    return why;
}

} // namespace

ChipletFormat::string_type ChipletFormat::resolve_viewing_path(
    const string_type& source, string_type& why) const
{
    why.clear();
    PathVarFailure fail;
    const std::string expanded = expand_path_vars_into(source, m_basePath, fail);
    if (fail.kind != PathVarFailure::Kind::None) {
        why = short_reason(fail);
        return {};
    }
    std::filesystem::path p(expanded);
    if (p.is_absolute()) {
        return expanded;
    }
    return (std::filesystem::path(m_basePath) / p).lexically_normal().string();
}

ChipletFormat::string_type ChipletFormat::expand_path_vars(const string_type& path) const
{
    if (path.find("${") == string_type::npos) {
        return path;
    }

    PathVarFailure fail;
    string_type out = expand_path_vars_into(path, m_basePath, fail);
    if (fail.kind != PathVarFailure::Kind::None) {
        const std::string& name = fail.name;

        // Fail closed on a name this host does not know.
        //
        // Expansion used to be MORE permissive with names nobody had vetoed
        // than with names that had been: a name absent from the table skipped
        // the marker check entirely and its environment value was substituted
        // raw, while a name in the table had to point at a directory
        // containing the marker. So ${ADK_ROOT} was validated and ${HOME}, or
        // any name a document cared to invent that happened to be set in the
        // environment, was not. The table is the vocabulary; a name outside it
        // has no marker to check against, so there is nothing that would make
        // substituting it safe.
        const PathVarMarker* marker = fail.marker;
        if (fail.kind == PathVarFailure::Kind::Unknown) {
            std::string known;
            for (const auto& m : kPathVarMarkers) {
                if (!known.empty()) known += ", ";
                known += m.name;
            }
            throw ChipletFormatException(
                "${" + name + "} in path '" + path + "' is not an ecosystem "
                "root this host knows, so it will not be substituted even if a "
                "variable of that name is set in the environment. The names "
                "that can appear here are: " + known + ".",
                0, "path");
        }

        if (marker->dirnames[0] == nullptr) {
            throw ChipletFormatException(
                "cannot resolve ${" + name + "} in path '" + path
                    + "'; set the " + name + " environment variable to a "
                    "directory",
                0, "path");
        }
        throw ChipletFormatException(
            "cannot resolve ${" + name + "} in path '" + path
                + "'; set the " + name + " environment variable to a "
                "directory containing " + marker->marker + ", or keep the "
                "checkout next to the .chiplet file",
            0, "path");
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
