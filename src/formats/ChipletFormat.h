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
#include <vector>
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
/**
 * What a caller is allowed to do about a failed save.
 *
 * Three categories rather than a description of what went wrong, because the
 * only reason to classify a save failure is to decide what happens next, and
 * there are exactly three answers: write nothing anywhere, ask for a different
 * name, or try a different destination.
 *
 * `Other` is the default and it is the safe one. A site nobody classified says
 * "this document could not be written at all", so a caller retries nothing and
 * writes nothing elsewhere. That polarity is the whole point of the default:
 * forgetting to classify a new failure costs a fallback that would have worked,
 * never a document written somewhere the user did not ask for.
 *
 * DestinationOccupied is deliberately NOT a kind of I/O failure even though one
 * of its two routes is a failed syscall. Something already sits at the name; a
 * different name fixes it and a different directory is not an answer to it. The
 * two routes are the admission check, which is the ordinary case, and EEXIST
 * from the exclusive publication, which is the same fact discovered later by
 * the one operation that cannot be raced.
 *
 * DestinationIo means this destination's filesystem refused, so another one may
 * work: the temporary file could not be created, written, flushed or closed, or
 * publication failed for a reason other than something being in the way.
 */
enum class FailureKind {
    Other,
    DestinationOccupied,
    DestinationIo,
};

class ChipletFormatException : public std::exception {
public:
    typedef std::string string_type;

    // The kind is a defaulted fourth parameter on purpose. Every existing
    // construction in this tree compiles untouched and lands on Other, which is
    // the answer those sites already imply, and what()/line()/context() are
    // unchanged, so every caller that catches this type behaves exactly as
    // before. The kind adds an answer; it replaces nothing.
    ChipletFormatException(const string_type& msg,
                           size_t line = 0,
                           const string_type& context = "",
                           FailureKind kind = FailureKind::Other);

    const char* what() const noexcept override;

    size_t line() const { return m_line; }
    const string_type& context() const { return m_context; }
    FailureKind kind() const { return m_kind; }

private:
    string_type m_message;
    size_t m_line;
    string_type m_context;
    FailureKind m_kind;
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
    void save(Assembly& assembly, const string_type& path,
              SavePurpose purpose = SavePurpose::Primary);
    // Const callers receive, but do not commit, the successful destination
    // receipt. Repeated changed saves use save_snapshot with that explicit receipt.
    [[nodiscard]] SaveReceipt save(const Assembly& assembly, const string_type& path);
    // Serialization/admission cannot mutate the design. No replacement is
    // authorized without a matching expected baseline, unless the path is new.
    [[nodiscard]] SaveReceipt save_snapshot(const Assembly& assembly,
        const string_type& path, const SaveRequest& request);
    // Complete current-state checkpoint plus ORIGINAL source and independent
    // I/O receipts. Does not restore undo history or rebase relative assets.
    // Checkpoint storage is trusted local recovery input: canonical encoding
    // and semantic concordance are not cryptographic payload authentication.
    std::string encode_recovery(const Assembly& assembly);
    std::unique_ptr<Assembly> decode_recovery(const std::string& payload);

    /**
     * Non-fatal notes the reference reader produced during the last load(), in
     * the order they happened and undeduplicated, as its warn channel delivers
     * them. Populated even when that load threw, because the notes that
     * accompany a failure are the ones worth having; load() also writes them to
     * the session log.
     *
     * Today this carries an unrecognised member of a closed vocabulary and a
     * format_version from a newer minor than the reader implements. Neither is
     * an error: the format's policy is that a consumer maps what it knows,
     * reports what it does not, and keeps reading.
     */
    const std::vector<string_type>& warnings() const;

private:
    std::unique_ptr<Assembly> load_buffer(const string_type& text,
        const string_type& path, bool allow_intermediate = false,
        bool restore_current_state = false);
    std::string serialize(const Assembly& assembly, const string_type& rescue_path,
        RejectedOutput rejected_output = RejectedOutput::Preserve);
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
    // For layer_properties and stackup only: resolve like resolve_path(), but
    // a ${VAR} that does not expand returns "" with a one-line reason in `why`
    // instead of throwing. Whether the file exists is the views' question.
    string_type resolve_viewing_path(const string_type& source, string_type& why) const;

    string_type m_basePath;  // Directory containing the .chiplet file
    std::vector<string_type> m_warnings;  // reader notes from the last load()
};

// Helper functions for type conversion
ComponentType string_to_component_type(const std::string& s);
std::string component_type_to_string(ComponentType t);

// interface.type is NOT converted here. Its mapping lives with the value, in
// core/Interface.h (InterfaceTypeValue), because the string is the storage and
// the enum is a derived view of it. A free from-string converter in this file
// is what made an unrecognised member refuse the whole document.

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
// The contract is chiplet-system-spec's schema for the field, which is a PAIR
// and not a single expression: the pattern PLUS the negative that forbids a
// `.drc` suffix. Implementing only the pattern accepts "evil.drc"; that is not
// hypothetical, it is the gap the reference implementations had. The oracle is
// chiplet-system-spec conformance/fixtures/adapter_id_cases.json, vendored
// beside the parity test, and the proposition that test closes is "rejects
// everything the schema rejects".
//
// Anchoring is dialect-specific and must never be copied across languages:
// Python needs \Z (a bare $ also matches before a trailing newline), the
// portable spelling is (?![\s\S]), and C++ std::regex is ECMAScript where \Z
// does not exist at all -- it would match a literal 'Z'. Hence regex_match,
// which anchors both ends by construction, with no end anchor in the pattern.
bool is_valid_adapter_id(const std::string& id);

// True when `path` carries a NUL byte, which makes it unusable as a filesystem
// path whatever else is right about it.
//
// Shared rather than restated, and the sharing is the point. The destination
// rule (reject_invalid_destination_path) has always asked this question, and
// the import now asks it of the path fields it puts INSIDE a document, so the
// same byte is judged by one statement in one place. The two callers keep their
// own messages because they are answering different questions: one is about
// where this program was told to write, the other about what a document says
// the layout is.
//
// It is NOT paired with the emptiness test that sits beside it in the
// destination rule. An empty destination is a caller mistake; an empty
// layer-properties path in an import spec means the author declared none, which
// the writer already handles by emitting nothing.
//
// Why a NUL is worse than it looks. The writer can represent it: yaml-cpp
// escapes it inside a double-quoted scalar and both readers decode it back, so
// the document round-trips exactly and nothing in the format complains. The
// damage is downstream, where the string reaches ::open, which stops at the NUL
// and silently opens a DIFFERENT file. A value that survives every check and
// then names something else is worth refusing at the only point that can still
// tell the author which field was wrong.
bool path_has_nul(const std::string& path);

} // namespace chiplet

#endif // CHIPLET_FORMATS_CHIPLETFORMAT_H
