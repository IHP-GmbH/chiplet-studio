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

    bool empty() const { return steps.empty(); }
    size_t step_count() const { return steps.size(); }
};

} // namespace chiplet

#endif // CHIPLET_CORE_FLOW_FLOWDEFINITION_H
