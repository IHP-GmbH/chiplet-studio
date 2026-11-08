// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 IHP GmbH
#pragma once

#include <string>
#include <vector>

#include <chiplet_format_io/chiplet_format_io.hpp>

namespace chiplet {

// Compare the top-level key sequence a writer INTENDED to emit against the one
// the format's own grammar reads out of the bytes that writer produced. Returns
// an empty string when they agree, otherwise the reason to refuse the save.
//
// It lives away from the emitter deliberately. `intended` arrives as a
// parameter and nothing here can reach a YAML::Emitter or the writer's state,
// so the single mistake that would make this check VACUOUS -- deriving the
// expected sequence by reading the produced text back, after which the
// comparison is `x == x` and passes on every document, broken ones included --
// is a rewrite rather than a local edit. The vendored library records the same
// hazard about its own writer's version of this check, in chiplet_format_io.cpp
// under dumps_text: "Parsing the final text would let an extra key in flow_yaml
// define its own expected output and defeat the ownership check."
//
// The comparison is on the SEQUENCE. Set equality or a sorted compare would let
// a reordering through, and order is part of what a top-level split means.
//
// Two things this deliberately does NOT answer.
//
//   1. Whether the document kept what it was read from. A top-level block this
//      writer does not model is absent from the intent and absent from the
//      output, so the two agree perfectly while the block is gone. Answering
//      that needs the block bytes, which this reader release does not
//      publish.
//   2. Anything about the CONTENTS of a block. The wrong components under a
//      correct `components:` key agree here and load cleanly.
inline std::string check_top_level_key_sequence(
    const std::vector<std::string>& intended, const std::string& text)
{
    namespace cfio = chiplet_format_io;

    const auto join = [](const std::vector<std::string>& keys) {
        std::string joined;
        for (const auto& key : keys) {
            if (!joined.empty()) joined += ", ";
            joined += key;
        }
        return joined.empty() ? std::string("(none)") : joined;
    };

    cfio::TopLevelKeys split;
    try {
        split = cfio::split_top_level_keys(text);
    } catch (const cfio::ChipletFormatError& e) {
        // A repeated top-level key. There is no sequence to compare against,
        // and the library's message already explains the damage: PyYAML keeps
        // the last value and yaml-cpp the first, so two conforming readers read
        // different documents from these bytes.
        return e.what();
    }

    // `keys` MUST NOT be read in this state; the library says so and means it.
    // The blocks behind those keys mis-attribute bytes, so a comparison here
    // would be against a sequence the grammar itself has disowned. Treating a
    // non-splittable document as "no keys" is the way this check goes green
    // while its property is false, because `keys` really is empty here.
    if (!split.not_splittable.empty()) {
        return "these bytes do not split into top-level blocks: " +
               split.not_splittable;
    }

    // Agreement reached by having nothing to say is not agreement. Today the
    // caller always records format_version, so this is unreachable; it is here
    // so that it stays unreachable after someone rearranges the caller.
    if (intended.empty()) {
        return "the writer recorded no top-level keys at all, so there was "
               "nothing to check its output against";
    }

    if (split.keys != intended) {
        return "intended: " + join(intended) + "\n  produced: " + join(split.keys);
    }
    return std::string();
}

}  // namespace chiplet
