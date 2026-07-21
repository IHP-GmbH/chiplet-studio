// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Component.h - Base class for assembly components (Die, Interposer, Substrate)
 */

#ifndef CHIPLET_CORE_COMPONENT_H
#define CHIPLET_CORE_COMPONENT_H

#include <string>
#include <vector>
#include <optional>
#include <map>
#include "IOPad.h"

namespace chiplet {

/**
 * 3D position in micrometers
 */
struct Position3D {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

/**
 * Rotation in degrees (around Z axis for now)
 */
struct Rotation3D {
    double z = 0.0;
};

/**
 * Dimensions in micrometers
 */
struct Dimensions3D {
    double width = 0.0;
    double height = 0.0;
    double thickness = 0.0;
};

/**
 * Per-component render mode for 3D visualization
 */
enum class RenderMode {
    Hidden,                 // Not rendered
    Wireframe,              // Bounding box edges only
    Transparent,            // Semi-transparent solid (alpha blending)
    Solid,                  // Opaque solid
    Detailed,               // Full GDS layer tessellation (with Si bulk)
    DetailedNoSubstrate     // Full GDS layer tessellation (no Si bulk slab)
};

/**
 * Die orientation (face-up for wirebond, face-down for flip-chip).
 * This is the internal render enum; the canonical .chiplet YAML token for
 * FaceDown is `flip_chip` (see coord_frame_contract.md 2.4), not `face_down`.
 */
enum class Orientation {
    FaceUp,    // Default: die face up (wirebond); YAML "face_up"
    FaceDown   // Flip-chip: die face down, mirror X; YAML "flip_chip"
};

/**
 * Mesh anchor convention. Determines how a component's local mesh is
 * built relative to its declared `position` in the canonical frame.
 *
 * See chiplet-studio/docs/coord_frame_contract.md §2 for the contract.
 *
 * - GdsOrigin: the component mesh is built around its own GDS (0,0).
 *   `position` is added to the GDS-origin without extra centering.
 *   Used by dies produced by gds_to_kicad (footprint anchor at GDS 0,0).
 * - BboxCenter: the component mesh is centered on its own GDS bbox.
 *   `position` places the bbox center. Used by interposers.
 */
enum class Anchor {
    GdsOrigin,
    BboxCenter
};

/**
 * Component types
 */
enum class ComponentType {
    Die,
    DieArray,
    Interposer,
    Substrate
};

/**
 * Array configuration for DieArray components
 */
struct ComponentArray {
    std::string pattern = "grid";  // "grid", "linear", "custom"
    int countX = 1;
    int countY = 1;
    double pitchX = 0.0;
    double pitchY = 0.0;
    Position3D startPosition;  // Position of first element
};

/**
 * Component is the base class for all assembly elements.
 */
class Component {
public:
    // Type definitions
    typedef std::string string_type;
    typedef Position3D position_type;
    typedef Rotation3D rotation_type;
    typedef Dimensions3D dimensions_type;
    typedef ComponentArray array_type;
    typedef std::map<std::string, std::string> metadata_type;

    // Constructors and destructor
    Component(const string_type& id, ComponentType type);
    virtual ~Component();

    // Getters - identity
    const string_type& id() const;
    ComponentType type() const;

    // Getters/Setters - display name (user-editable, defaults to id)
    void set_name(const string_type& name);
    const string_type& name() const;

    // Getters/Setters - technology
    void set_technology(const string_type& techId);
    const string_type& technology() const;

    // Getters/Setters - connection stack reference
    void set_connection(const string_type& connectionId);
    const string_type& connection() const;

    // Getters/Setters - layout
    void set_layout_path(const string_type& path);
    const string_type& layout_path() const;

    // Verbatim layout path as written in the .chiplet file (may contain ${VAR}
    // or a relative path). The reader records it so the writer can round-trip it
    // instead of baking in the resolved absolute path. Empty for in-memory
    // assemblies; callers should fall back to layout_path() then.
    void set_layout_path_source(const string_type& path);
    const string_type& layout_path_source() const;

    // Legacy top_cell (backward compatibility - returns cells[0])
    void set_top_cell(const string_type& cell);
    const string_type& top_cell() const;

    // Multi-cell support (for flat GDS files)
    void set_cells(const std::vector<string_type>& cells);
    const std::vector<string_type>& cells() const;
    void add_cell(const string_type& cell);
    void clear_cells();

    // Getters/Setters - geometry
    void set_position(const position_type& pos);
    const position_type& position() const;

    void set_rotation(const rotation_type& rot);
    const rotation_type& rotation() const;

    void set_dimensions(const dimensions_type& dims);
    const dimensions_type& dimensions() const;

    // Array configuration (for DieArray type)
    void set_array(const array_type& array);
    const std::optional<array_type>& array() const;
    bool is_array() const;

    // Custom metadata (vendor, part_number, etc.)
    void set_metadata(const string_type& key, const string_type& value);
    string_type metadata(const string_type& key) const;
    const metadata_type& all_metadata() const;

    // Render mode (per-component 3D visualization control)
    RenderMode render_mode() const;
    void set_render_mode(RenderMode mode);

    // Orientation (face-up or flip-chip face-down)
    Orientation orientation() const;
    void set_orientation(Orientation o);

    // Mesh anchor convention (see Anchor enum doc and contract §2).
    Anchor anchor() const;
    void set_anchor(Anchor a);

    // True when the anchor field was explicitly declared in the source
    // .chiplet file (vs defaulted by the reader). Used by tests and by
    // ChipletFormat to emit one warning per file when a writer omits
    // the field.
    bool anchor_declared() const;
    void set_anchor_declared(bool declared);

    // External I/O pads attached to this component (e.g. wire-bond
    // pads on the interposer).
    void add_io_pad(const IOPad& pad);
    void clear_io_pads();
    const std::vector<IOPad>& io_pads() const;
    size_t io_pad_count() const;

private:
    string_type m_id;
    string_type m_name;  // User-editable display name, defaults to id
    ComponentType m_type;
    string_type m_technology;
    string_type m_connection;
    string_type m_layoutPath;
    string_type m_layoutPathSource;  // verbatim ${VAR}/relative path from the file
    std::vector<string_type> m_cells;  // List of cells to visualize (replaces m_topCell)
    position_type m_position;
    rotation_type m_rotation;
    dimensions_type m_dimensions;
    std::optional<array_type> m_array;
    metadata_type m_metadata;
    RenderMode m_renderMode;
    Orientation m_orientation = Orientation::FaceUp;
    Anchor m_anchor = Anchor::BboxCenter;
    bool m_anchorDeclared = false;
    std::vector<IOPad> m_ioPads;

    // Static empty string for backward compatibility reference return
    static const string_type s_emptyString;
};

// Anchor string conversion helpers. Strings are snake_case
// ("gds_origin", "bbox_center") matching the schema.
//
// string_to_anchor returns std::nullopt for unknown values; callers
// must decide how to handle that (log + default for the parser, hard
// error for command-line tools, etc.).
std::string anchor_to_string(Anchor a);
std::optional<Anchor> string_to_anchor(const std::string& s);

} // namespace chiplet

#endif // CHIPLET_CORE_COMPONENT_H
