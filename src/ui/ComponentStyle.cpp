// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * ComponentStyle.cpp - Implementation
 */

#include "ComponentStyle.h"
#include <yaml-cpp/yaml.h>
#include <cstdio>   // std::sscanf
#include <fstream>

namespace chiplet {

// =============================================================================
// Utility Functions
// =============================================================================

ComponentColor parse_component_color(const std::string& hex)
{
    ComponentColor color;
    if (hex.empty() || hex[0] != '#') {
        return color;
    }

    std::string h = hex.substr(1);

    // Support #RGB and #RRGGBB formats
    if (h.length() == 3) {
        unsigned int r, g, b;
        if (sscanf(h.c_str(), "%1x%1x%1x", &r, &g, &b) == 3) {
            color.r = static_cast<uint8_t>(r * 17);
            color.g = static_cast<uint8_t>(g * 17);
            color.b = static_cast<uint8_t>(b * 17);
        }
    } else if (h.length() == 6) {
        unsigned int r, g, b;
        if (sscanf(h.c_str(), "%2x%2x%2x", &r, &g, &b) == 3) {
            color.r = static_cast<uint8_t>(r);
            color.g = static_cast<uint8_t>(g);
            color.b = static_cast<uint8_t>(b);
        }
    }

    return color;
}

IconShape parse_icon_shape(const std::string& name)
{
    if (name == "circle") return IconShape::Circle;
    if (name == "diamond") return IconShape::Diamond;
    if (name == "grid") return IconShape::Grid;
    if (name == "layers") return IconShape::Layers;
    if (name == "rectangle") return IconShape::Rectangle;
    return IconShape::Square;  // default
}

std::string icon_shape_to_string(IconShape shape)
{
    switch (shape) {
        case IconShape::Circle: return "circle";
        case IconShape::Diamond: return "diamond";
        case IconShape::Grid: return "grid";
        case IconShape::Layers: return "layers";
        case IconShape::Rectangle: return "rectangle";
        case IconShape::Square:
        default: return "square";
    }
}

// =============================================================================
// ComponentStyleFile Implementation
// =============================================================================

ComponentStyleFile::ComponentStyleFile()
{
    load_defaults();
}

void ComponentStyleFile::load_defaults()
{
    m_type_styles.clear();
    m_custom_styles.clear();
    m_path.clear();
    m_error.clear();

    // Die - Cornflower blue
    m_type_styles[ComponentType::Die] = default_style(ComponentType::Die);

    // DieArray - Medium slate blue
    m_type_styles[ComponentType::DieArray] = default_style(ComponentType::DieArray);

    // Interposer - Medium sea green
    m_type_styles[ComponentType::Interposer] = default_style(ComponentType::Interposer);

    // Substrate - Saddle brown
    m_type_styles[ComponentType::Substrate] = default_style(ComponentType::Substrate);
}

ComponentStyle ComponentStyleFile::default_style(ComponentType type)
{
    ComponentStyle style;
    style.type = type;

    switch (type) {
        case ComponentType::Die:
            style.fill_color = ComponentColor(100, 149, 237);  // #6495ED Cornflower blue
            style.frame_color = ComponentColor(65, 105, 225);  // #4169E1 Royal blue
            style.icon = IconShape::Square;
            break;

        case ComponentType::DieArray:
            style.fill_color = ComponentColor(123, 104, 238);  // #7B68EE Medium slate blue
            style.frame_color = ComponentColor(106, 90, 205);  // #6A5ACD Slate blue
            style.icon = IconShape::Grid;
            break;

        case ComponentType::Interposer:
            style.fill_color = ComponentColor(60, 179, 113);   // #3CB371 Medium sea green
            style.frame_color = ComponentColor(46, 139, 87);   // #2E8B57 Sea green
            style.icon = IconShape::Layers;
            break;

        case ComponentType::Substrate:
            style.fill_color = ComponentColor(139, 90, 43);    // #8B5A2B Saddle brown
            style.frame_color = ComponentColor(101, 67, 33);   // #654321 Dark brown
            style.icon = IconShape::Rectangle;
            break;
    }

    return style;
}

bool ComponentStyleFile::load(const string_type& path)
{
    // Start with defaults
    load_defaults();
    m_path = path;

    try {
        YAML::Node root = YAML::LoadFile(path);

        // Parse component_types section
        if (root["component_types"]) {
            YAML::Node types = root["component_types"];

            auto parse_type_style = [&](const std::string& key, ComponentType type) {
                if (types[key]) {
                    YAML::Node node = types[key];
                    ComponentStyle style;
                    style.type = type;

                    if (node["fill_color"]) {
                        style.fill_color = parse_component_color(node["fill_color"].as<std::string>());
                    }
                    if (node["frame_color"]) {
                        style.frame_color = parse_component_color(node["frame_color"].as<std::string>());
                    }
                    if (node["icon"]) {
                        style.icon = parse_icon_shape(node["icon"].as<std::string>());
                    }

                    m_type_styles[type] = style;
                }
            };

            parse_type_style("die", ComponentType::Die);
            parse_type_style("die_array", ComponentType::DieArray);
            parse_type_style("interposer", ComponentType::Interposer);
            parse_type_style("substrate", ComponentType::Substrate);
        }

        // Parse custom_styles section
        if (root["custom_styles"]) {
            YAML::Node custom = root["custom_styles"];
            for (auto it = custom.begin(); it != custom.end(); ++it) {
                std::string id = it->first.as<std::string>();
                YAML::Node node = it->second;

                ComponentStyle style;
                style.id = id;

                if (node["fill_color"]) {
                    style.fill_color = parse_component_color(node["fill_color"].as<std::string>());
                }
                if (node["frame_color"]) {
                    style.frame_color = parse_component_color(node["frame_color"].as<std::string>());
                }
                if (node["icon"]) {
                    style.icon = parse_icon_shape(node["icon"].as<std::string>());
                }

                m_custom_styles[id] = style;
            }
        }

        return true;

    } catch (const YAML::Exception& e) {
        m_error = std::string("YAML error: ") + e.what();
        return false;
    } catch (const std::exception& e) {
        m_error = std::string("Error: ") + e.what();
        return false;
    }
}

const ComponentStyle* ComponentStyleFile::style_for(const Component& comp) const
{
    // Check custom style first (by component ID)
    const ComponentStyle* custom = style_for_id(comp.id());
    if (custom) {
        return custom;
    }

    // Fall back to type-based style
    return style_for_type(comp.type());
}

const ComponentStyle* ComponentStyleFile::style_for_id(const string_type& id) const
{
    auto it = m_custom_styles.find(id);
    if (it != m_custom_styles.end()) {
        return &it->second;
    }
    return nullptr;
}

const ComponentStyle* ComponentStyleFile::style_for_type(ComponentType type) const
{
    auto it = m_type_styles.find(type);
    if (it != m_type_styles.end()) {
        return &it->second;
    }
    return nullptr;
}

} // namespace chiplet
