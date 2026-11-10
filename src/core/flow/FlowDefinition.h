// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * FlowDefinition.h - Plain data container for flow pipeline configuration
 *
 * FlowDefinition is a thread-safe, QObject-free representation of a flow
 * pipeline. ChipletFormat populates it during load (on a worker thread),
 * and MainWindow copies the data into FlowEngine on the main thread.
 */

#ifndef CHIPLET_CORE_FLOW_FLOWDEFINITION_H
#define CHIPLET_CORE_FLOW_FLOWDEFINITION_H

#include <string>
#include <vector>
#include <map>
#include "FlowStep.h"

namespace chiplet {

struct FlowDefinition {
    std::vector<FlowStep> steps;
    std::string working_directory;
    std::map<std::string, std::string> environment;

    // Empty means the author declared no flow content AT ALL, not merely no
    // steps. A block carrying only an environment or only a working_directory
    // is a legal thing to write and this predicate gates whether it is written
    // back: when it answered steps.empty(), saving deleted such a block.
    bool empty() const {
        return steps.empty() && environment.empty() && working_directory.empty();
    }
    size_t step_count() const { return steps.size(); }
};

} // namespace chiplet

#endif // CHIPLET_CORE_FLOW_FLOWDEFINITION_H
