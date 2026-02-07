/**
 * LayerProperties.h - Layer properties and .lyp file parser
 *
 * Parses KLayout .lyp layer properties files to extract layer styling
 * information (colors, visibility, etc.) for GDS/OASIS visualization.
 */

#ifndef CHIPLET_LAYER_PROPERTIES_H
#define CHIPLET_LAYER_PROPERTIES_H

#include <string>
#include <vector>
#include <map>
#include <cstdint>

namespace chiplet {

/**
 * RGB color structure (0-255 per channel)
 */
struct LayerColor {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 255;  // Alpha (opacity)

    LayerColor() = default;
    LayerColor(uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_ = 255)
        : r(r_), g(g_), b(b_), a(a_) {}

    bool operator==(const LayerColor& other) const {
        return r == other.r && g == other.g && b == other.b && a == other.a;
    }

    bool operator!=(const LayerColor& other) const {
        return !(*this == other);
    }
};

/**
 * Layer/datatype pair identifier
 */
struct LayerKey {
    int layer = 0;
    int datatype = 0;

    LayerKey() = default;
    LayerKey(int l, int d) : layer(l), datatype(d) {}

    bool operator==(const LayerKey& other) const {
        return layer == other.layer && datatype == other.datatype;
    }

    bool operator!=(const LayerKey& other) const {
        return !(*this == other);
    }

    bool operator<(const LayerKey& other) const {
        if (layer != other.layer) return layer < other.layer;
        return datatype < other.datatype;
    }
};

/**
 * Properties for a single layer
 */
struct LayerStyle {
    typedef std::string string_type;

    LayerKey key;
    string_type name;
    LayerColor fill_color;
    LayerColor frame_color;
    bool visible = true;
    bool transparent = false;
    bool valid = true;
    int width = 1;

    // Pattern and style fields (from .lyp)
    string_type dither_pattern;   // e.g., "C27", "C0" (solid)
    string_type line_style;       // e.g., "C0", "C1", "C8"
    int frame_brightness = 0;     // -255 to 255
    int fill_brightness = 0;      // -255 to 255

    LayerStyle() = default;

    // Convenience accessors
    bool is_visible() const { return visible && valid; }
    bool has_pattern() const { return !dither_pattern.empty() && dither_pattern != "C0"; }
};

/**
 * Collection of layer properties from a .lyp file
 */
class LayerPropertiesFile {
public:
    typedef std::string string_type;
    typedef std::vector<LayerStyle> layer_list_type;
    typedef std::map<LayerKey, size_t> layer_map_type;

    LayerPropertiesFile() = default;

    // Load from .lyp file
    bool load(const string_type& path);

    // Access layers
    const layer_list_type& layers() const { return m_layers; }
    size_t layer_count() const { return m_layers.size(); }

    // Find layer by key
    const LayerStyle* find(const LayerKey& key) const;
    const LayerStyle* find(int layer, int datatype) const;

    // File path
    const string_type& path() const { return m_path; }

    // Error message (if load failed)
    const string_type& error() const { return m_error; }

private:
    layer_list_type m_layers;
    layer_map_type m_layer_map;
    string_type m_path;
    string_type m_error;
};

// Utility functions
LayerColor parse_hex_color(const std::string& hex);
LayerKey parse_layer_source(const std::string& source);

} // namespace chiplet

#endif // CHIPLET_LAYER_PROPERTIES_H
