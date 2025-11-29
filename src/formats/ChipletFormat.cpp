/**
 * ChipletFormat.cpp - Implementation of .chiplet YAML parser/writer
 */

#include "ChipletFormat.h"
#include "core/Technology.h"
#include <yaml-cpp/yaml.h>
#include <fstream>
#include <filesystem>

namespace chiplet {

// ChipletFormatException implementation

ChipletFormatException::ChipletFormatException(const string_type& msg,
                                               size_t line,
                                               const string_type& context)
    : m_line(line)
    , m_context(context)
{
    m_message = context.empty() ? msg : (context + ": " + msg);
    if (line > 0) {
        m_message += " (line " + std::to_string(line) + ")";
    }
}

const char* ChipletFormatException::what() const noexcept
{
    return m_message.c_str();
}

// Helper functions for parsing 3D structures from YAML
namespace {

Position3D parsePosition3D(const YAML::Node& node)
{
    Position3D pos;
    if (node["x"]) pos.x = node["x"].as<double>();
    if (node["y"]) pos.y = node["y"].as<double>();
    if (node["z"]) pos.z = node["z"].as<double>();
    return pos;
}

Rotation3D parseRotation3D(const YAML::Node& node)
{
    Rotation3D rot;
    if (node["z"]) rot.z = node["z"].as<double>();
    return rot;
}

Dimensions3D parseDimensions3D(const YAML::Node& node)
{
    Dimensions3D dims;
    if (node["width"]) dims.width = node["width"].as<double>();
    if (node["height"]) dims.height = node["height"].as<double>();
    if (node["thickness"]) dims.thickness = node["thickness"].as<double>();
    return dims;
}

} // anonymous namespace

// Type conversion functions
ComponentType string_to_component_type(const std::string& s)
{
    if (s == "die") return ComponentType::Die;
    if (s == "die_array") return ComponentType::DieArray;
    if (s == "interposer") return ComponentType::Interposer;
    if (s == "substrate") return ComponentType::Substrate;
    return ComponentType::Die;  // Default
}

std::string component_type_to_string(ComponentType t)
{
    switch (t) {
        case ComponentType::Die: return "die";
        case ComponentType::DieArray: return "die_array";
        case ComponentType::Interposer: return "interposer";
        case ComponentType::Substrate: return "substrate";
    }
    return "die";
}

ChipletFormat::ChipletFormat() = default;
ChipletFormat::~ChipletFormat() = default;

std::unique_ptr<Assembly> ChipletFormat::load(const string_type& path)
{
    // Store base path for relative path resolution
    std::filesystem::path filePath(path);
    m_basePath = filePath.parent_path().string();
    if (m_basePath.empty()) {
        m_basePath = ".";
    }

    // Load YAML file
    YAML::Node root;
    try {
        root = YAML::LoadFile(path);
    } catch (const YAML::Exception& e) {
        throw ChipletFormatException(e.what(), 0, "YAML");
    }

    // Check format version
    if (!root["format_version"]) {
        throw ChipletFormatException("Missing required field", 0, "format_version");
    }

    auto assembly = std::make_unique<Assembly>();

    // Parse sections
    if (root["assembly"]) {
        parse_assembly_metadata(root["assembly"], *assembly);
    } else {
        throw ChipletFormatException("Missing required section", 0, "assembly");
    }

    if (root["technologies"]) {
        parse_technologies(root["technologies"], *assembly);
    }

    if (root["components"]) {
        parse_components(root["components"], *assembly);
    }

    // interfaces, netlist, design_rules, default_views - skipped for now (future extension)

    return assembly;
}

void ChipletFormat::parse_assembly_metadata(const YAML::Node& node, Assembly& assembly)
{
    if (node["name"]) {
        assembly.set_name(node["name"].as<std::string>());
    } else {
        throw ChipletFormatException("Missing required field", 0, "assembly.name");
    }

    if (node["description"]) {
        assembly.set_description(node["description"].as<std::string>());
    }

    if (node["author"]) {
        assembly.set_author(node["author"].as<std::string>());
    }

    if (node["created"]) {
        assembly.set_created(node["created"].as<std::string>());
    }

    if (node["modified"]) {
        assembly.set_modified(node["modified"].as<std::string>());
    }

    if (node["units"]) {
        assembly.set_units(node["units"].as<std::string>());
    }
}

void ChipletFormat::parse_technologies(const YAML::Node& node, Assembly& assembly)
{
    if (!node.IsMap()) {
        throw ChipletFormatException("Expected a map", 0, "technologies");
    }

    for (const auto& item : node) {
        std::string techId = item.first.as<std::string>();
        const YAML::Node& techNode = item.second;

        auto tech = std::make_unique<Technology>(techId);

        if (techNode["description"]) {
            tech->set_description(techNode["description"].as<std::string>());
        }

        if (techNode["layer_properties"]) {
            std::string lpPath = techNode["layer_properties"].as<std::string>();
            tech->set_layer_properties_path(resolve_path(lpPath));
        }

        if (techNode["dbu"]) {
            tech->set_dbu(techNode["dbu"].as<double>());
        }

        // stackup is skipped for now (future extension)

        assembly.add_technology(std::move(tech));
    }
}

void ChipletFormat::parse_components(const YAML::Node& node, Assembly& assembly)
{
    if (!node.IsSequence()) {
        throw ChipletFormatException("Expected a sequence", 0, "components");
    }

    for (const auto& compNode : node) {
        parse_component(compNode, assembly);
    }
}

void ChipletFormat::parse_component(const YAML::Node& node, Assembly& assembly)
{
    // Required fields
    if (!node["id"]) {
        throw ChipletFormatException("Missing required field", 0, "component.id");
    }
    std::string id = node["id"].as<std::string>();

    if (!node["type"]) {
        throw ChipletFormatException("Missing required field for " + id, 0, "component.type");
    }
    ComponentType type = string_to_component_type(node["type"].as<std::string>());

    auto component = std::make_unique<Component>(id, type);

    // Technology reference
    if (node["technology"]) {
        component->set_technology(node["technology"].as<std::string>());
    }

    // Layout file
    if (node["layout"]) {
        std::string layoutPath = node["layout"].as<std::string>();
        component->set_layout_path(resolve_path(layoutPath));
    }

    // Top cell
    if (node["top_cell"]) {
        component->set_top_cell(node["top_cell"].as<std::string>());
    }

    // Position
    if (node["position"]) {
        component->set_position(parsePosition3D(node["position"]));
    }

    // Rotation
    if (node["rotation"]) {
        component->set_rotation(parseRotation3D(node["rotation"]));
    }

    // Dimensions
    if (node["dimensions"]) {
        component->set_dimensions(parseDimensions3D(node["dimensions"]));
    }

    // Array configuration (for DieArray type)
    if (node["array"]) {
        const YAML::Node& arrayNode = node["array"];
        ComponentArray arr;

        if (arrayNode["pattern"]) {
            arr.pattern = arrayNode["pattern"].as<std::string>();
        }

        if (arrayNode["count"]) {
            if (arrayNode["count"]["x"]) arr.countX = arrayNode["count"]["x"].as<int>();
            if (arrayNode["count"]["y"]) arr.countY = arrayNode["count"]["y"].as<int>();
        }

        if (arrayNode["pitch"]) {
            if (arrayNode["pitch"]["x"]) arr.pitchX = arrayNode["pitch"]["x"].as<double>();
            if (arrayNode["pitch"]["y"]) arr.pitchY = arrayNode["pitch"]["y"].as<double>();
        }

        if (arrayNode["start_position"]) {
            arr.startPosition = parsePosition3D(arrayNode["start_position"]);
        }

        component->set_array(arr);
    }

    // Custom metadata
    if (node["metadata"]) {
        const YAML::Node& metaNode = node["metadata"];
        if (metaNode.IsMap()) {
            for (const auto& item : metaNode) {
                component->set_metadata(
                    item.first.as<std::string>(),
                    item.second.as<std::string>()
                );
            }
        }
    }

    assembly.add_component(std::move(component));
}

void ChipletFormat::save(const Assembly& assembly, const string_type& path)
{
    YAML::Emitter out;
    out << YAML::BeginMap;

    // Format version
    out << YAML::Key << "format_version" << YAML::Value << "1.0";

    // Assembly metadata
    out << YAML::Key << "assembly" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "name" << YAML::Value << assembly.name();
    if (!assembly.description().empty()) {
        out << YAML::Key << "description" << YAML::Value << assembly.description();
    }
    if (!assembly.author().empty()) {
        out << YAML::Key << "author" << YAML::Value << assembly.author();
    }
    if (!assembly.created().empty()) {
        out << YAML::Key << "created" << YAML::Value << assembly.created();
    }
    if (!assembly.modified().empty()) {
        out << YAML::Key << "modified" << YAML::Value << assembly.modified();
    }
    out << YAML::Key << "units" << YAML::Value << assembly.units();
    out << YAML::EndMap;

    // Technologies
    if (!assembly.technologies().empty()) {
        out << YAML::Key << "technologies" << YAML::Value << YAML::BeginMap;

        for (const auto& tech : assembly.technologies()) {
            out << YAML::Key << tech->id() << YAML::Value << YAML::BeginMap;

            if (!tech->description().empty()) {
                out << YAML::Key << "description" << YAML::Value << tech->description();
            }

            if (!tech->layer_properties_path().empty()) {
                out << YAML::Key << "layer_properties" << YAML::Value << tech->layer_properties_path();
            }

            out << YAML::Key << "dbu" << YAML::Value << tech->dbu();

            out << YAML::EndMap;
        }

        out << YAML::EndMap;
    }

    // Components
    if (!assembly.components().empty()) {
        out << YAML::Key << "components" << YAML::Value << YAML::BeginSeq;

        for (const auto& comp : assembly.components()) {
            out << YAML::BeginMap;
            out << YAML::Key << "id" << YAML::Value << comp->id();
            out << YAML::Key << "type" << YAML::Value << component_type_to_string(comp->type());

            if (!comp->technology().empty()) {
                out << YAML::Key << "technology" << YAML::Value << comp->technology();
            }

            if (!comp->layout_path().empty()) {
                out << YAML::Key << "layout" << YAML::Value << comp->layout_path();
            }

            if (!comp->top_cell().empty()) {
                out << YAML::Key << "top_cell" << YAML::Value << comp->top_cell();
            }

            // Position
            const auto& pos = comp->position();
            if (pos.x != 0 || pos.y != 0 || pos.z != 0) {
                out << YAML::Key << "position" << YAML::Value << YAML::Flow << YAML::BeginMap;
                out << YAML::Key << "x" << YAML::Value << pos.x;
                out << YAML::Key << "y" << YAML::Value << pos.y;
                out << YAML::Key << "z" << YAML::Value << pos.z;
                out << YAML::EndMap;
            }

            // Rotation
            const auto& rot = comp->rotation();
            if (rot.z != 0) {
                out << YAML::Key << "rotation" << YAML::Value << YAML::Flow << YAML::BeginMap;
                out << YAML::Key << "z" << YAML::Value << rot.z;
                out << YAML::EndMap;
            }

            // Dimensions
            const auto& dims = comp->dimensions();
            if (dims.width != 0 || dims.height != 0 || dims.thickness != 0) {
                out << YAML::Key << "dimensions" << YAML::Value << YAML::Flow << YAML::BeginMap;
                out << YAML::Key << "width" << YAML::Value << dims.width;
                out << YAML::Key << "height" << YAML::Value << dims.height;
                out << YAML::Key << "thickness" << YAML::Value << dims.thickness;
                out << YAML::EndMap;
            }

            // Array configuration
            if (comp->is_array()) {
                const auto& arr = comp->array().value();
                out << YAML::Key << "array" << YAML::Value << YAML::BeginMap;
                out << YAML::Key << "pattern" << YAML::Value << arr.pattern;
                out << YAML::Key << "count" << YAML::Value << YAML::Flow << YAML::BeginMap;
                out << YAML::Key << "x" << YAML::Value << arr.countX;
                out << YAML::Key << "y" << YAML::Value << arr.countY;
                out << YAML::EndMap;
                out << YAML::Key << "pitch" << YAML::Value << YAML::Flow << YAML::BeginMap;
                out << YAML::Key << "x" << YAML::Value << arr.pitchX;
                out << YAML::Key << "y" << YAML::Value << arr.pitchY;
                out << YAML::EndMap;
                out << YAML::Key << "start_position" << YAML::Value << YAML::Flow << YAML::BeginMap;
                out << YAML::Key << "x" << YAML::Value << arr.startPosition.x;
                out << YAML::Key << "y" << YAML::Value << arr.startPosition.y;
                out << YAML::Key << "z" << YAML::Value << arr.startPosition.z;
                out << YAML::EndMap;
                out << YAML::EndMap;
            }

            // Metadata
            const auto& meta = comp->all_metadata();
            if (!meta.empty()) {
                out << YAML::Key << "metadata" << YAML::Value << YAML::BeginMap;
                for (const auto& [key, value] : meta) {
                    out << YAML::Key << key << YAML::Value << value;
                }
                out << YAML::EndMap;
            }

            out << YAML::EndMap;
        }

        out << YAML::EndSeq;
    }

    out << YAML::EndMap;

    // Write to file
    std::ofstream file(path);
    if (!file.is_open()) {
        throw ChipletFormatException("Could not open file for writing: " + path, 0, "file");
    }

    file << out.c_str();
}

ChipletFormat::string_type ChipletFormat::resolve_path(const string_type& relativePath) const
{
    if (relativePath.empty()) {
        return relativePath;
    }

    // Check if already absolute
    std::filesystem::path p(relativePath);
    if (p.is_absolute()) {
        return relativePath;
    }

    // Resolve relative to base path
    std::filesystem::path resolved = std::filesystem::path(m_basePath) / p;
    return resolved.lexically_normal().string();
}

} // namespace chiplet
