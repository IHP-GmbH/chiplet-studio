// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Component.cpp - Implementation
 */

#include "Component.h"

namespace chiplet {

// Static empty string for backward compatibility reference return
const Component::string_type Component::s_emptyString;

Component::Component(const std::string& id, ComponentType type)
    : m_id(id)
    , m_name(id)  // Default display name to id
    , m_type(type)
    , m_renderMode(type == ComponentType::Substrate ? RenderMode::Solid : RenderMode::Transparent)
{
}

Component::~Component() = default;

const std::string& Component::id() const
{
    return m_id;
}

ComponentType Component::type() const
{
    return m_type;
}

void Component::set_name(const string_type& name)
{
    m_name = name;
}

const Component::string_type& Component::name() const
{
    return m_name;
}

void Component::set_technology(const string_type& techId)
{
    m_technology = techId;
}

const Component::string_type& Component::technology() const
{
    return m_technology;
}

void Component::set_connection(const string_type& connectionId)
{
    m_connection = connectionId;
}

const Component::string_type& Component::connection() const
{
    return m_connection;
}

void Component::set_layout_path(const string_type& path)
{
    m_layoutPath = path;
}

const Component::string_type& Component::layout_path() const
{
    return m_layoutPath;
}

void Component::set_layout_path_source(const string_type& path)
{
    m_layoutPathSource = path;
}

const Component::string_type& Component::layout_path_source() const
{
    return m_layoutPathSource;
}

void Component::set_top_cell(const string_type& cell)
{
    // Backward compatibility: set as first (and only) cell
    m_cells.clear();
    if (!cell.empty()) {
        m_cells.push_back(cell);
    }
}

const Component::string_type& Component::top_cell() const
{
    // Backward compatibility: return first cell if any
    return m_cells.empty() ? s_emptyString : m_cells[0];
}

void Component::set_cells(const std::vector<string_type>& cells)
{
    m_cells = cells;
}

const std::vector<Component::string_type>& Component::cells() const
{
    return m_cells;
}

void Component::add_cell(const string_type& cell)
{
    m_cells.push_back(cell);
}

void Component::clear_cells()
{
    m_cells.clear();
}

void Component::set_position(const position_type& pos)
{
    m_position = pos;
}

const Component::position_type& Component::position() const
{
    return m_position;
}

void Component::set_rotation(const rotation_type& rot)
{
    m_rotation = rot;
}

const Component::rotation_type& Component::rotation() const
{
    return m_rotation;
}

void Component::set_dimensions(const dimensions_type& dims)
{
    m_dimensions = dims;
}

const Component::dimensions_type& Component::dimensions() const
{
    return m_dimensions;
}

const std::optional<double>& Component::attachment_surface_z() const
{
    return m_attachmentSurfaceZ;
}

void Component::set_attachment_surface_z(const std::optional<double>& z)
{
    m_attachmentSurfaceZ = z;
}

void Component::set_array(const array_type& array)
{
    m_array = array;
}

const std::optional<Component::array_type>& Component::array() const
{
    return m_array;
}

bool Component::is_array() const
{
    return m_array.has_value();
}

void Component::set_metadata(const string_type& key, const string_type& value)
{
    m_metadata[key] = value;
}

Component::string_type Component::metadata(const string_type& key) const
{
    auto it = m_metadata.find(key);
    return (it != m_metadata.end()) ? it->second : string_type();
}

const Component::metadata_type& Component::all_metadata() const
{
    return m_metadata;
}

RenderMode Component::render_mode() const
{
    return m_renderMode;
}

void Component::set_render_mode(RenderMode mode)
{
    m_renderMode = mode;
}

Orientation Component::orientation() const
{
    return m_orientation;
}

void Component::set_orientation(Orientation o)
{
    m_orientation = o;
}

Anchor Component::anchor() const
{
    return m_anchor;
}

void Component::set_anchor(Anchor a)
{
    m_anchor = a;
}

bool Component::anchor_declared() const
{
    return m_anchorDeclared;
}

void Component::set_anchor_declared(bool declared)
{
    m_anchorDeclared = declared;
}

void Component::add_io_pad(const IOPad& pad)
{
    m_ioPads.push_back(pad);
}

void Component::clear_io_pads()
{
    m_ioPads.clear();
}

const std::vector<IOPad>& Component::io_pads() const
{
    return m_ioPads;
}

size_t Component::io_pad_count() const
{
    return m_ioPads.size();
}

std::string anchor_to_string(Anchor a)
{
    switch (a) {
        case Anchor::GdsOrigin:  return "gds_origin";
        case Anchor::BboxCenter: return "bbox_center";
    }
    // Unreachable for a well-formed enum value; pick the safer default
    // so a future Anchor variant added without updating this switch
    // does not silently emit garbage.
    return "bbox_center";
}

std::optional<Anchor> string_to_anchor(const std::string& s)
{
    if (s == "gds_origin")  return Anchor::GdsOrigin;
    if (s == "bbox_center") return Anchor::BboxCenter;
    return std::nullopt;
}

} // namespace chiplet
