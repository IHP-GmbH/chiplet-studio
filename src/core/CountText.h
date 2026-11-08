// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * CountText.h - How a count of shapes is written on screen
 */

#ifndef CHIPLET_CORE_COUNTTEXT_H
#define CHIPLET_CORE_COUNTTEXT_H

#include <cstddef>
#include <string>

namespace chiplet {

// 749218 -> "749,218".
inline std::string groupedCount(std::size_t n)
{
    const std::string digits = std::to_string(n);
    const std::size_t lead = digits.size() % 3;
    std::string out;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i != 0 && i >= lead && (i - lead) % 3 == 0) out += ',';
        out += digits[i];
    }
    return out;
}

} // namespace chiplet

#endif // CHIPLET_CORE_COUNTTEXT_H
