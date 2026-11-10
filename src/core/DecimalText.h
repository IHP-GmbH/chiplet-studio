// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/*
 * DecimalText.h - A z derived from the attach plane, summed and written as decimals
 *
 * The plane is written with its own decimal places (10.8103) and so are the
 * heights stacked on it. Their double sum can carry float noise that 17
 * significant digits then print: 10.8103 + 18 is 28.810299999999998. The
 * contract writes such a z rounded to the decimal places it was made from, in
 * the shortest text that reads back as that value.
 */

#ifndef CHIPLET_CORE_DECIMALTEXT_H
#define CHIPLET_CORE_DECIMALTEXT_H

#include <algorithm>
#include <charconv>
#include <cmath>
#include <string>

namespace chiplet {

// The shortest fixed-point text that reads back as v: 54.8103, where 17
// significant digits give 54.810299999999998.
inline std::string shortestText(double v)
{
    char buf[400];  // fixed notation of the largest double fits
    const auto r = std::to_chars(buf, buf + sizeof buf, v, std::chars_format::fixed);
    return std::string(buf, r.ptr);
}

// The decimal places v is written with, ignoring float noise below 1e-9:
// 10.8103 has 4, 48 has 0, 0.30000000000000004 has 1.
inline int decimalPlaces(double v)
{
    const std::string s = shortestText(std::round(v * 1e9) / 1e9);
    const auto dot = s.find('.');
    return dot == std::string::npos ? 0 : static_cast<int>(s.size() - dot - 1);
}

// a + b rounded to the larger of their decimal places, as a decimal sum
// gives it: 10.8103 + 18 is 28.8103.
inline double decimalSum(double a, double b)
{
    const double scale = std::pow(10.0, std::max(decimalPlaces(a), decimalPlaces(b)));
    return std::round((a + b) * scale) / scale;
}

} // namespace chiplet

#endif // CHIPLET_CORE_DECIMALTEXT_H
