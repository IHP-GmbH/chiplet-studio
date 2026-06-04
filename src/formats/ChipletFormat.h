// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * ChipletFormat.h - Parser/Writer for .chiplet YAML format
 */

#ifndef CHIPLET_FORMATS_CHIPLETFORMAT_H
#define CHIPLET_FORMATS_CHIPLETFORMAT_H

#include <string>
#include <memory>
#include <stdexcept>
#include "core/Assembly.h"

// Forward declaration to avoid exposing yaml-cpp in header
namespace YAML {
    class Node;
}

namespace chiplet {

/**
 * Exception thrown on .chiplet format errors.
 */
class ChipletFormatException : public std::exception {
public:
    typedef std::string string_type;

    ChipletFormatException(const string_type& msg,
                           size_t line = 0,
                           const string_type& context = "");

    const char* what() const noexcept override;

    size_t line() const { return m_line; }
    const string_type& context() const { return m_context; }

private:
    string_type m_message;
    size_t m_line;
    string_type m_context;
};

/**
 * ChipletFormat handles reading and writing .chiplet files.
 * Throws ChipletFormatException on errors.
 */
class ChipletFormat {
public:
    // Type definitions
    typedef std::string string_type;

    // Constructors and destructor
    ChipletFormat();
    ~ChipletFormat();

    /**
     * Load an assembly from a .chiplet file.
     * @param path Path to .chiplet file
     * @return Assembly
     * @throws ChipletFormatException on parse error
     */
    std::unique_ptr<Assembly> load(const string_type& path);

    /**
     * Save an assembly to a .chiplet file.
     * @param assembly Assembly to save
     * @param path Path to .chiplet file
     * @throws ChipletFormatException on write error
     */
    void save(const Assembly& assembly, const string_type& path);

private:
    // Parsing helpers (throw on error)
    void parse_assembly_metadata(const YAML::Node& node, Assembly& assembly);
    void parse_technologies(const YAML::Node& node, Assembly& assembly);
    std::unique_ptr<Technology> parse_technology_entry(const string_type& techId,
                                                       const YAML::Node& techNode);
    void parse_connection_stacks(const YAML::Node& node, Assembly& assembly);
    void parse_components(const YAML::Node& node, Assembly& assembly);
    void parse_component(const YAML::Node& node, Assembly& assembly);
    void parse_interfaces(const YAML::Node& node, Assembly& assembly);
    void parse_netlist(const YAML::Node& node, Assembly& assembly);
    void auto_calculate_z(Assembly& assembly);

    // Utility
    string_type resolve_path(const string_type& relativePath) const;
    string_type expand_path_vars(const string_type& path) const;

    string_type m_basePath;  // Directory containing the .chiplet file
};

// Helper functions for type conversion
ComponentType string_to_component_type(const std::string& s);
std::string component_type_to_string(ComponentType t);

InterfaceType interface_type_from_string(const std::string& s);
std::string interface_type_to_string(InterfaceType t);

} // namespace chiplet

#endif // CHIPLET_FORMATS_CHIPLETFORMAT_H
