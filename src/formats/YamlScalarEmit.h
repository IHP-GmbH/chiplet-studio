// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * YamlScalarEmit.h - how this writer puts a string scalar on the wire.
 *
 * Shared by the .chiplet writer (formats/ChipletFormat.cpp) and the flow-block
 * writer (core/flow/FlowConfig.cpp) so the two cannot drift: they emit into the
 * same document and a rule that holds in one and not the other is not a rule.
 */

#ifndef CHIPLET_FORMATS_YAMLSCALAREMIT_H
#define CHIPLET_FORMATS_YAMLSCALAREMIT_H

#include <yaml-cpp/yaml.h>
#include <string>
#include <utility>

namespace chiplet {

// True when a scalar carries a Unicode line/paragraph separator: U+2028 (LS),
// U+2029 (PS) or U+0085 (NEL).
//
// These three are the characters YAML 1.1 counts as line breaks and YAML 1.2
// does not, which is why they are worth singling out. Emitted RAW they make the
// two readers of this format disagree about the structure of the same bytes:
// PyYAML (1.1) starts a new line at them, yaml-cpp (1.2) keeps reading the same
// one. That difference is not cosmetic -- a raw separator inside an unquoted
// flow-block key injects what PyYAML reads as a second top-level key, so the
// same file can yield two different format_version values. chiplet-system-spec
// therefore rejects raw occurrences at the text level, before YAML sees them.
//
// Escaped, they are ordinary content again: the escape is plain ASCII, so it
// passes that raw-byte check, and both readers decode it back to the same code
// point. Escaping is what lets the value survive verbatim; stripping it would
// silently mangle the user's data.
inline bool scalarNeedsUnicodeEscaping(const std::string& s)
{
    return s.find("\xe2\x80\xa8") != std::string::npos    // U+2028 LINE SEPARATOR
        || s.find("\xe2\x80\xa9") != std::string::npos    // U+2029 PARAGRAPH SEPARATOR
        || s.find("\xc2\x85") != std::string::npos;       // U+0085 NEXT LINE
}

// yaml-cpp's EscapeNonAscii, applied to ONE scalar.
//
// Both EscapeNonAscii and DoubleQuoted are locally scoped manipulators: they
// apply to the next node and revert. That is what keeps this safe to reach
// for -- the scalar next to an escaped one keeps its raw UTF-8, so a document
// with no separator in it comes out byte-for-byte as before. Measured against
// the build image's yaml-cpp: an escaped neighbour emits "caf\xe9" while the
// following scalar still emits café.
//
// Setting it globally (Emitter::SetOutputCharset) would instead re-encode every
// character above 0x7E in the whole document, rewriting the bytes of every file
// that contains an accent or a CJK name, and promoting plain scalars to quoted
// ones. That is why this is per-value and not a one-line emitter setting.
//
// Note the deliberate cost: when a scalar DOES carry a separator, any other
// non-ASCII in that same scalar is escaped too. It decodes identically, and it
// only touches values that contain a separator, of which the corpus has none.
// The alternative is to trust that yaml-cpp keeps escaping U+0085 on its own,
// which it happens to do today; relying on that is exactly the kind of
// unchecked dependency this guard exists to remove.
inline YAML::Emitter& emitEscapedIfNeeded(YAML::Emitter& out, const std::string& value)
{
    if (scalarNeedsUnicodeEscaping(value)) {
        out << YAML::EscapeNonAscii;
    }
    return out << YAML::DoubleQuoted << value;
}

// A scalar the format declares a string, emitted always double-quoted.
//
// Quoting is STRUCTURAL here, not cosmetic. yaml-cpp omits quotes whenever YAML
// does not strictly require them, and the reader on the other side then
// re-types the value: `id: 0755` comes back as the integer 493 under YAML 1.1
// (PyYAML, which is what the KiCad plugin and the reference Python reader use),
// `top_cell: 1.10` comes back as 1.1, and `created: 2026-03-22` comes back as a
// date. yaml-cpp itself returns all three as strings, so the two readers
// disagree about the same bytes and the disagreement is about IDENTITY, not
// formatting. chiplet-system-spec holds the same line: an unquoted
// format_version is a SCHEMA NEGATIVE there (conformance fixture
// v1_0_unquoted_numeric.chiplet); the reference reader only coerces it for
// back-compat.
//
// The rule is unconditional on purpose: quote every scalar the format
// declares a string, without inspecting the value. Quoting only the values
// that "would" re-type requires modelling every YAML version's resolution
// table, is untestable, and makes correctness depend on the data.
//
// KEYS are deliberately NOT routed through this. Top-level keys are literals
// and must stay bare: the downstream KiCad merge guard identifies top-level
// blocks with a line regex that does not match a quoted key, so quoting them
// would make every file we write unparseable to it. Document-derived keys
// that are nested (technology ids, connection-stack ids) DO get quoted --
// they are never at column 0, so the guard is unaffected, and they re-type
// exactly like values do.
struct QuotedScalar {
    std::string value;
};

inline QuotedScalar qs(std::string value)
{
    return QuotedScalar{std::move(value)};
}

inline YAML::Emitter& operator<<(YAML::Emitter& out, const QuotedScalar& s)
{
    return emitEscapedIfNeeded(out, s.value);
}

// A scalar that stays PLAIN (unquoted) unless it has to be quoted.
//
// The flow block emits unquoted scalars and must keep doing so: quoting them
// would rewrite `id: step1` as `id: "step1"` in every existing file that has a
// flow, and the merge guard in the KiCad plugin matches flow keys with a line
// regex that a quoted key does not satisfy. So the shape only changes for the
// values that would otherwise put a raw separator on disk, and those cannot
// stay plain: a plain scalar has no escape syntax at all, so a backslash in it
// is literal text. Quoting is not a preference there, it is the only way to
// write the character down.
struct PlainScalar {
    std::string value;
};

inline PlainScalar ps(std::string value)
{
    return PlainScalar{std::move(value)};
}

inline YAML::Emitter& operator<<(YAML::Emitter& out, const PlainScalar& s)
{
    if (!scalarNeedsUnicodeEscaping(s.value)) {
        return out << s.value;
    }
    return emitEscapedIfNeeded(out, s.value);
}

} // namespace chiplet

#endif // CHIPLET_FORMATS_YAMLSCALAREMIT_H
