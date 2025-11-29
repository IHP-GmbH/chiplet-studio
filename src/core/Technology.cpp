/**
 * Technology.cpp - Implementation
 */

#include "Technology.h"

namespace chiplet {

Technology::Technology(const std::string& id)
    : m_id(id)
{
}

Technology::~Technology() = default;

const std::string& Technology::id() const
{
    return m_id;
}

void Technology::setDescription(const std::string& desc)
{
    m_description = desc;
}

const std::string& Technology::description() const
{
    return m_description;
}

void Technology::setLayerPropertiesPath(const std::string& path)
{
    m_layerPropertiesPath = path;
}

const std::string& Technology::layerPropertiesPath() const
{
    return m_layerPropertiesPath;
}

void Technology::setDbu(double dbu)
{
    m_dbu = dbu;
}

double Technology::dbu() const
{
    return m_dbu;
}

} // namespace chiplet
