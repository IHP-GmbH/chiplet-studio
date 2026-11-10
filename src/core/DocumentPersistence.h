// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 IHP GmbH
#pragma once

#include <optional>
#include <memory>
#include <string>
#include <vector>
#include <utility>

namespace chiplet {
enum class SavePurpose { Primary, Autosave };

// Exact bytes, not timestamps: chmod alone is not an external content edit.
class SaveReceipt {
public:
    SaveReceipt(const SaveReceipt&) = default;
    SaveReceipt& operator=(const SaveReceipt&) = default;
    const std::string& canonical_path() const { return m_path; }
    const std::string& bytes() const { return m_bytes; }
private:
    friend class ChipletFormat;
    SaveReceipt(std::shared_ptr<const int> identity, std::string path, std::string bytes)
        : m_identity(std::move(identity)), m_path(std::move(path)), m_bytes(std::move(bytes)) {}
    std::shared_ptr<const int> m_identity;
    std::string m_path;
    std::string m_bytes;
};
// What a refused save does with the text it was about to write.
//
// Preserve is the default and is the behaviour every existing caller wants: a
// refusal writes the rejected text to <path>.rejected so that work which lives
// only in RAM is not lost with the refusal. The name is chosen so no *.chiplet
// glob in this ecosystem picks it up.
//
// Suppress exists for one shape: a save whose entire content was just derived
// from a file the user still has. An import of a GDS is the case. Nothing is
// rescued by writing a sidecar there, because nothing would be lost; what it
// does instead is leave debris at a destination the user chose, from a document
// they never authored, named after a file that does not exist. A typed choice
// rather than an empty rescue path, because the empty path already means "there
// is nowhere to put it" and those are different statements: one is an absent
// destination, the other is a deliberate policy, and reading the second out of
// the first is how a caller ends up suppressing by accident.
enum class RejectedOutput { Preserve, Suppress };

struct SaveRequest {
    std::optional<SaveReceipt> expected;
    RejectedOutput rejected_output = RejectedOutput::Preserve;
};
struct PreservedRoot {
    std::string key;
    std::string raw;
};
struct DocumentPersistence {
    std::string source_path;
    std::string source_bytes;
    bool capture_safe = true; // A fresh in-memory model has no source to lose.
    std::string capture_reason;
    std::string preamble;
    std::vector<PreservedRoot> roots; // Unsupported roots and source flow, in order.
    std::optional<SaveReceipt> primary;
    std::optional<SaveReceipt> autosave;
private:
    friend class ChipletFormat;
    // Process-local document capability, not serialized or a cryptographic key.
    std::shared_ptr<const int> m_identity = std::make_shared<const int>(0);
};
} // namespace chiplet
