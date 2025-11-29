/**
 * Component.cpp - Implementation
 */

#include "Component.h"

namespace chiplet {

Component::Component(const std::string& id, ComponentType type)
    : m_id(id)
    , m_type(type)
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

void Component::set_technology(const string_type& techId)
{
    m_technology = techId;
}

const Component::string_type& Component::technology() const
{
    return m_technology;
}

void Component::set_layout_path(const string_type& path)
{
    m_layoutPath = path;
}

const Component::string_type& Component::layout_path() const
{
    return m_layoutPath;
}

void Component::set_top_cell(const string_type& cell)
{
    m_topCell = cell;
}

const Component::string_type& Component::top_cell() const
{
    return m_topCell;
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

} // namespace chiplet
