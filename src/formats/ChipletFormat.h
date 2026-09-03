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

// Forward declaration of the vendored Apache-2.0 reference reader's document
// model. Its full definition lives in <chiplet_format_io/chiplet_format_io.hpp>
// and is pulled in by the .cpp only, keeping those types off this header's
// public surface (and yaml-cpp out of it entirely).
namespace chiplet_format_io {
    struct ChipletDocument;
    struct Technology;
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
    // Build a core Technology from a parsed reference-library technology entry:
    // resolve the .lyp path against the .chiplet directory, set the dbu, and
    // auto-load the adjacent GDS3D techfile when one exists. Used for both the
    // technologies: map and the interconnect: technology subblock.
    std::unique_ptr<Technology> build_technology(
        const chiplet_format_io::Technology& tech);

    // Auto-calculate z for components that have a connection stack and z == 0.
    void auto_calculate_z(Assembly& assembly);

    // Path resolution: relative-to-.chiplet plus ${VAR} ecosystem-root expansion.
    string_type resolve_path(const string_type& relativePath) const;
    string_type expand_path_vars(const string_type& path) const;

    string_type m_basePath;  // Directory containing the .chiplet file
};

// Helper functions for type conversion
ComponentType string_to_component_type(const std::string& s);
std::string component_type_to_string(ComponentType t);

InterfaceType interface_type_from_string(const std::string& s);
std::string interface_type_to_string(InterfaceType t);

// True when `id` is a well-formed adapter id (interconnect.adapter and, when
// this reader grows one, interposer.adapter).
//
// An adapter id is a REGISTRY ID resolved by the consuming ADK, never a
// filesystem path and never a deck file name. This matters because a .chiplet
// crosses a trust boundary: a downloaded project's adapter id reaches
// BlenderGDSConfigs::interconnectStackupFragmentPath, which concatenates it
// into a path, and in the wider ecosystem an adapter names a DRC deck that a
// KLayout runner File.reads and eval's. So the gate belongs at LOAD, in every
// consumer, and fails closed.
//
// The contract is chiplet-spec's schema for the field, which is a PAIR and not
// a single expression: the pattern PLUS the negative that forbids a `.drc`
// suffix. Implementing only the pattern accepts "evil.drc"; that is not
// hypothetical, it is the gap the reference implementations had. The oracle is
// chiplet-spec conformance/fixtures/adapter_id_cases.json, vendored beside the
// parity test, and the proposition that test closes is "rejects everything the
// schema rejects".
//
// Anchoring is dialect-specific and must never be copied across languages:
// Python needs \Z (a bare $ also matches before a trailing newline), the
// portable spelling is (?![\s\S]), and C++ std::regex is ECMAScript where \Z
// does not exist at all -- it would match a literal 'Z'. Hence regex_match,
// which anchors both ends by construction, with no end anchor in the pattern.
bool is_valid_adapter_id(const std::string& id);

} // namespace chiplet

#endif // CHIPLET_FORMATS_CHIPLETFORMAT_H
