// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * ComponentStyle.h - Component style definitions and .chiplet-style parser
 *
 * Provides styling information for chiplet components (icons, colors)
 * following the same pattern as LayerPropertiesFile for .lyp files.
 */

#ifndef CHIPLET_UI_COMPONENTSTYLE_H
#define CHIPLET_UI_COMPONENTSTYLE_H

#include <string>
#include <map>
#include <cstdint>
#include "core/Component.h"

namespace chiplet {

/**
 * RGB color structure (0-255 per channel)
 */
struct ComponentColor {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 255;

    ComponentColor() = default;
    ComponentColor(uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_ = 255)
        : r(r_), g(g_), b(b_), a(a_) {}

    bool operator==(const ComponentColor& other) const {
        return r == other.r && g == other.g && b == other.b && a == other.a;
    }
};

/**
 * Icon shapes for component visualization
 */
enum class IconShape {
    Square,
    Circle,
    Diamond,
    Grid,
    Layers,
    Rectangle
};

/**
 * Style properties for a component
 */
struct ComponentStyle {
    typedef std::string string_type;

    ComponentType type = ComponentType::Die;
    string_type id;  // Empty for type-based, set for custom style
    ComponentColor fill_color;
    ComponentColor frame_color;
    IconShape icon = IconShape::Square;

    ComponentStyle() = default;
};

/**
 * Collection of component styles from a .chiplet-style file
 *
 * Supports:
 * - Default styles per ComponentType (Die, Interposer, Substrate)
 * - Custom styles for specific component IDs
 * - Built-in defaults when no file is loaded
 */
class ComponentStyleFile {
public:
    typedef std::string string_type;
    typedef std::map<ComponentType, ComponentStyle> type_map;
    typedef std::map<string_type, ComponentStyle> custom_map;

    ComponentStyleFile();

    // Load from .chiplet-style YAML file
    bool load(const string_type& path);

    // Initialize with built-in defaults (called automatically)
    void load_defaults();

    // Get style for a component (checks custom first, then type default)
    const ComponentStyle* style_for(const Component& comp) const;
    const ComponentStyle* style_for_id(const string_type& id) const;
    const ComponentStyle* style_for_type(ComponentType type) const;

    // Static default style for a type
    static ComponentStyle default_style(ComponentType type);

    // File path (empty if defaults only)
    const string_type& path() const { return m_path; }

    // Error message (if load failed)
    const string_type& error() const { return m_error; }

private:
    type_map m_type_styles;
    custom_map m_custom_styles;
    string_type m_path;
    string_type m_error;
};

// Utility functions
ComponentColor parse_component_color(const std::string& hex);
IconShape parse_icon_shape(const std::string& name);
std::string icon_shape_to_string(IconShape shape);

} // namespace chiplet

#endif // CHIPLET_UI_COMPONENTSTYLE_H
