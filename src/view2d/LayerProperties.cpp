// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * LayerProperties.cpp - Layer properties and .lyp file parser implementation
 */

#include "LayerProperties.h"
#include <QFile>
#include <QXmlStreamReader>
#include <sstream>
#include <iostream>

namespace chiplet {

// Parse hex color string (#RRGGBB or #RGB)
LayerColor parse_hex_color(const std::string& hex)
{
    LayerColor color;

    if (hex.empty() || hex[0] != '#') {
        return color;
    }

    std::string h = hex.substr(1);  // Remove '#'

    if (h.length() == 6) {
        // #RRGGBB format
        unsigned int r, g, b;
        if (sscanf(h.c_str(), "%02x%02x%02x", &r, &g, &b) == 3) {
            color.r = static_cast<uint8_t>(r);
            color.g = static_cast<uint8_t>(g);
            color.b = static_cast<uint8_t>(b);
        }
    } else if (h.length() == 3) {
        // #RGB format (expand to #RRGGBB)
        unsigned int r, g, b;
        if (sscanf(h.c_str(), "%1x%1x%1x", &r, &g, &b) == 3) {
            color.r = static_cast<uint8_t>(r * 17);
            color.g = static_cast<uint8_t>(g * 17);
            color.b = static_cast<uint8_t>(b * 17);
        }
    }

    return color;
}

// Parse layer/datatype source string (e.g., "40/0")
LayerKey parse_layer_source(const std::string& source)
{
    LayerKey key;

    size_t slash_pos = source.find('/');
    if (slash_pos != std::string::npos) {
        try {
            key.layer = std::stoi(source.substr(0, slash_pos));
            key.datatype = std::stoi(source.substr(slash_pos + 1));
        } catch (const std::exception&) {
            // Invalid format, return default (0/0)
        }
    }

    return key;
}

bool LayerPropertiesFile::load(const string_type& path)
{
    m_layers.clear();
    m_layer_map.clear();
    m_path.clear();
    m_error.clear();

    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_error = "Cannot open file: " + path;
        return false;
    }

    QXmlStreamReader xml(&file);

    LayerStyle current_style;
    bool in_properties = false;

    while (!xml.atEnd() && !xml.hasError()) {
        QXmlStreamReader::TokenType token = xml.readNext();

        if (token == QXmlStreamReader::StartElement) {
            QString name = xml.name().toString();

            if (name == "properties") {
                in_properties = true;
                current_style = LayerStyle();
            } else if (in_properties) {
                // Read element content
                QString content = xml.readElementText();
                std::string content_str = content.toStdString();

                if (name == "name") {
                    current_style.name = content_str;
                } else if (name == "source") {
                    current_style.key = parse_layer_source(content_str);
                } else if (name == "fill-color") {
                    current_style.fill_color = parse_hex_color(content_str);
                } else if (name == "frame-color") {
                    current_style.frame_color = parse_hex_color(content_str);
                } else if (name == "visible") {
                    current_style.visible = (content_str == "true");
                } else if (name == "transparent") {
                    current_style.transparent = (content_str == "true");
                } else if (name == "valid") {
                    current_style.valid = (content_str == "true");
                } else if (name == "width") {
                    try {
                        current_style.width = std::stoi(content_str);
                    } catch (const std::exception&) {
                        current_style.width = 1;
                    }
                } else if (name == "dither-pattern") {
                    current_style.dither_pattern = content_str;
                } else if (name == "line-style") {
                    current_style.line_style = content_str;
                } else if (name == "frame-brightness") {
                    try {
                        current_style.frame_brightness = std::stoi(content_str);
                    } catch (const std::exception&) {
                        current_style.frame_brightness = 0;
                    }
                } else if (name == "fill-brightness") {
                    try {
                        current_style.fill_brightness = std::stoi(content_str);
                    } catch (const std::exception&) {
                        current_style.fill_brightness = 0;
                    }
                }
            }
        } else if (token == QXmlStreamReader::EndElement) {
            QString name = xml.name().toString();

            if (name == "properties" && in_properties) {
                in_properties = false;

                // Add layer to collection
                size_t index = m_layers.size();
                m_layers.push_back(current_style);
                m_layer_map[current_style.key] = index;
            }
        }
    }

    if (xml.hasError()) {
        m_error = "XML parse error: " + xml.errorString().toStdString();
        m_layers.clear();
        m_layer_map.clear();
        return false;
    }

    m_path = path;
    return true;
}

const LayerStyle* LayerPropertiesFile::find(const LayerKey& key) const
{
    auto it = m_layer_map.find(key);
    if (it != m_layer_map.end()) {
        return &m_layers[it->second];
    }
    return nullptr;
}

const LayerStyle* LayerPropertiesFile::find(int layer, int datatype) const
{
    return find(LayerKey(layer, datatype));
}

} // namespace chiplet
