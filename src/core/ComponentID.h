// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * ComponentID.h - Type-safe identifier for components
 *
 * Provides a type alias for component identifiers, enabling future
 * migration to different ID types while maintaining current string
 * compatibility.
 */

#ifndef CHIPLET_CORE_COMPONENT_ID_H
#define CHIPLET_CORE_COMPONENT_ID_H

#include <string>
#include <functional>

namespace chiplet {

/**
 * Type-safe wrapper for component identifiers.
 * Currently uses std::string to maintain backward compatibility
 * with .chiplet file format. Can be changed to numeric handles
 * in the future if performance requires.
 */
using ComponentID = std::string;

/**
 * Invalid/empty ID constant.
 * Use this to represent "no component selected" or invalid references.
 */
inline const ComponentID INVALID_COMPONENT_ID = "";

/**
 * Check if a ComponentID is valid (non-empty).
 * @param id The component ID to validate
 * @return true if the ID is valid (non-empty)
 */
inline bool is_valid_id(const ComponentID& id) {
    return !id.empty();
}

} // namespace chiplet

// Note: Hash specialization not needed since ComponentID is std::string.
// If ComponentID is changed to a different type in the future, a hash
// specialization can be added here.

#endif // CHIPLET_CORE_COMPONENT_ID_H
