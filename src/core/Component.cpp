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

void Component::setTechnology(const std::string& techId)
{
    m_technology = techId;
}

const std::string& Component::technology() const
{
    return m_technology;
}

void Component::setLayoutPath(const std::string& path)
{
    m_layoutPath = path;
}

const std::string& Component::layoutPath() const
{
    return m_layoutPath;
}

void Component::setTopCell(const std::string& cell)
{
    m_topCell = cell;
}

const std::string& Component::topCell() const
{
    return m_topCell;
}

void Component::setPosition(const Position3D& pos)
{
    m_position = pos;
}

const Position3D& Component::position() const
{
    return m_position;
}

void Component::setRotation(const Rotation3D& rot)
{
    m_rotation = rot;
}

const Rotation3D& Component::rotation() const
{
    return m_rotation;
}

void Component::setDimensions(const Dimensions3D& dims)
{
    m_dimensions = dims;
}

const Dimensions3D& Component::dimensions() const
{
    return m_dimensions;
}

} // namespace chiplet
