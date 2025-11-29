/**
 * Technology.cpp - Implementation
 */

#include "Technology.h"
#include <filesystem>

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

void Technology::set_description(const string_type& desc)
{
    m_description = desc;
}

const Technology::string_type& Technology::description() const
{
    return m_description;
}

void Technology::set_layer_properties_path(const string_type& path)
{
    m_layerPropertiesPath = path;
}

const Technology::string_type& Technology::layer_properties_path() const
{
    return m_layerPropertiesPath;
}

void Technology::set_dbu(double dbu)
{
    m_dbu = dbu;
}

double Technology::dbu() const
{
    return m_dbu;
}

bool Technology::layer_properties_exist() const
{
    if (m_layerPropertiesPath.empty()) {
        return true;  // No path specified is valid (optional)
    }
    return std::filesystem::exists(m_layerPropertiesPath);
}

TechnologyValidation Technology::validate() const
{
    TechnologyValidation result;

    // Check ID
    if (m_id.empty()) {
        result.add_error("Technology ID is empty");
    }

    // Check layer properties path
    if (!m_layerPropertiesPath.empty()) {
        if (!std::filesystem::exists(m_layerPropertiesPath)) {
            result.add_error("Layer properties file not found: " + m_layerPropertiesPath);
        }
    } else {
        result.add_warning("No layer properties file specified");
    }

    // Check dbu is reasonable
    if (m_dbu <= 0) {
        result.add_error("Database unit must be positive");
    } else if (m_dbu > 1.0) {
        result.add_warning("Database unit larger than 1um may be unusual");
    }

    return result;
}

bool Technology::is_valid() const
{
    return validate().valid;
}

} // namespace chiplet
