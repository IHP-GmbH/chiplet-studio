// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Technology.cpp - Implementation
 */

#include "Technology.h"
#include "LayerStackup.h"
#include "process_cfg.h"  // GDS3D process parser
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

void Technology::set_layer_properties_source(const string_type& path)
{
    m_layerPropertiesSource = path;
}

const Technology::string_type& Technology::layer_properties_source() const
{
    return m_layerPropertiesSource;
}

void Technology::set_stackup_path(const string_type& path)
{
    m_stackupPath = path;
}

const Technology::string_type& Technology::stackup_path() const
{
    return m_stackupPath;
}

void Technology::set_stackup_source(const string_type& path)
{
    m_stackupSource = path;
}

const Technology::string_type& Technology::stackup_source() const
{
    return m_stackupSource;
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

// GDS3D Process Definition support

bool Technology::load_process_def(const std::string& path)
{
    if (path.empty()) return false;

    m_process = std::make_unique<GDSProcess>();
    m_process->Parse(const_cast<char*>(path.c_str()));

    if (m_process->IsValid() && m_process->LayerCount() > 0) {
        m_processDefPath = path;
        return true;
    }

    m_process.reset();
    return false;
}

bool Technology::has_process_def() const
{
    return m_process && m_process->IsValid();
}

const std::string& Technology::process_def_path() const
{
    return m_processDefPath;
}

double Technology::get_layer_z_um(int layer, int datatype) const
{
    if (!m_process) return 0.0;

    ProcessLayer* pl = m_process->GetLayer(layer, datatype);
    if (!pl) return 0.0;

    // Convert nm to um
    return pl->Height / 1000.0;
}

double Technology::get_layer_thickness_um(int layer, int datatype) const
{
    if (!m_process) return 1.0;  // Default 1um

    ProcessLayer* pl = m_process->GetLayer(layer, datatype);
    if (!pl) return 1.0;

    // Convert nm to um
    return pl->Thickness / 1000.0;
}

std::array<float, 3> Technology::get_layer_color(int layer, int datatype) const
{
    if (!m_process) return {0.5f, 0.5f, 0.5f};

    ProcessLayer* pl = m_process->GetLayer(layer, datatype);
    if (!pl) return {0.5f, 0.5f, 0.5f};

    return {pl->Red, pl->Green, pl->Blue};
}

LayerStackup Technology::createStackup() const
{
    LayerStackup stackup;

    if (!m_process) return stackup;

    // Iterate through all layers in the process
    for (int i = 0; i < m_process->LayerCount(); ++i) {
        ProcessLayer* pl = m_process->GetLayer(i);
        if (!pl) continue;

        // Convert nm to um
        double z_um = pl->Height / 1000.0;
        double thickness_um = pl->Thickness / 1000.0;

        std::string name = pl->Name ? pl->Name : "";

        stackup.addLayer(pl->Layer, pl->Datatype, z_um, thickness_um, name);
    }

    return stackup;
}

} // namespace chiplet
