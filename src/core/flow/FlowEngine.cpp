// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * FlowEngine.cpp - Generic subprocess pipeline runner implementation
 */

#include "FlowEngine.h"
#include <QCoreApplication>
#include <QDebug>
#include <algorithm>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace chiplet {

// -- StepStatus conversion --------------------------------------------------

std::string step_status_to_string(StepStatus status)
{
    switch (status) {
        case StepStatus::Pending:  return "pending";
        case StepStatus::Running:  return "running";
        case StepStatus::Success:  return "success";
        case StepStatus::Error:    return "error";
        case StepStatus::Skipped:  return "skipped";
    }
    return "pending";
}

StepStatus step_status_from_string(const std::string& s)
{
    if (s == "running")  return StepStatus::Running;
    if (s == "success")  return StepStatus::Success;
    if (s == "error")    return StepStatus::Error;
    if (s == "skipped")  return StepStatus::Skipped;
    return StepStatus::Pending;
}

// -- Construction / Destruction ---------------------------------------------

FlowEngine::FlowEngine(QObject* parent)
    : QObject(parent)
{
}

FlowEngine::~FlowEngine()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

// -- Step management --------------------------------------------------------

void FlowEngine::add_step(const FlowStep& step)
{
    m_steps.push_back(step);
}

bool FlowEngine::remove_step(const std::string& id)
{
    auto it = std::find_if(m_steps.begin(), m_steps.end(),
        [&id](const FlowStep& s) { return s.id == id; });
    if (it == m_steps.end())
        return false;
    m_steps.erase(it);
    return true;
}

FlowStep* FlowEngine::step(const std::string& id)
{
    for (auto& s : m_steps) {
        if (s.id == id)
            return &s;
    }
    return nullptr;
}

const FlowStep* FlowEngine::step(const std::string& id) const
{
    for (const auto& s : m_steps) {
        if (s.id == id)
            return &s;
    }
    return nullptr;
}

const FlowEngine::step_list_type& FlowEngine::steps() const
{
    return m_steps;
}

size_t FlowEngine::step_count() const
{
    return m_steps.size();
}

void FlowEngine::clear_steps()
{
    m_steps.clear();
}

// -- Configuration ----------------------------------------------------------

void FlowEngine::set_working_directory(const std::string& dir)
{
    m_workingDir = dir;
}

const std::string& FlowEngine::working_directory() const
{
    return m_workingDir;
}

void FlowEngine::set_environment(const std::string& key, const std::string& value)
{
    m_environment[key] = value;
}

const std::map<std::string, std::string>& FlowEngine::environment() const
{
    return m_environment;
}

// -- Topological sort (Kahn's algorithm) ------------------------------------

std::vector<std::string> FlowEngine::topological_sort() const
{
    if (m_steps.empty())
        return {};

    // Build ID set for validation
    std::unordered_set<std::string> step_ids;
    for (const auto& s : m_steps)
        step_ids.insert(s.id);

    // Build in-degree map and reverse adjacency list
    std::unordered_map<std::string, int> in_degree;
    std::unordered_map<std::string, std::vector<std::string>> dependents;

    for (const auto& s : m_steps) {
        if (in_degree.find(s.id) == in_degree.end())
            in_degree[s.id] = 0;

        for (const auto& dep : s.depends_on) {
            // Only count dependencies on known steps
            if (step_ids.count(dep)) {
                in_degree[s.id]++;
                dependents[dep].push_back(s.id);
            }
        }
    }

    // Seed queue with zero in-degree steps
    std::queue<std::string> ready;
    for (const auto& s : m_steps) {
        if (in_degree[s.id] == 0)
            ready.push(s.id);
    }

    // Process
    std::vector<std::string> sorted;
    sorted.reserve(m_steps.size());

    while (!ready.empty()) {
        std::string current = ready.front();
        ready.pop();
        sorted.push_back(current);

        for (const auto& dep_id : dependents[current]) {
            in_degree[dep_id]--;
            if (in_degree[dep_id] == 0)
                ready.push(dep_id);
        }
    }

    // Cycle detection: not all nodes visited
    if (sorted.size() != m_steps.size())
        return {};

    return sorted;
}

// -- Execution --------------------------------------------------------------

bool FlowEngine::run_all()
{
    if (m_running)
        return false;

    auto sorted = topological_sort();
    if (sorted.empty() && !m_steps.empty())
        return false;  // Circular dependency

    // Surface dependencies that name unknown step ids: topological_sort silently
    // drops them, so without this a typo'd depends_on would lose the intended
    // ordering constraint with no feedback to the user.
    {
        std::unordered_set<std::string> ids;
        for (const auto& s : m_steps)
            ids.insert(s.id);
        for (const auto& s : m_steps)
            for (const auto& dep : s.depends_on)
                if (!ids.count(dep))
                    qWarning("FlowEngine: step '%s' depends on unknown step '%s' (ignored)",
                             s.id.c_str(), dep.c_str());
    }

    // Reset all steps
    for (auto& s : m_steps) {
        s.status = StepStatus::Pending;
        s.log.clear();
        s.exit_code = -1;
    }

    m_executionQueue = sorted;
    m_currentIndex = 0;
    m_running = true;
    m_cancelled = false;

    execute_next();
    return true;
}

bool FlowEngine::run_step(const std::string& id)
{
    if (m_running)
        return false;

    FlowStep* s = step(id);
    if (!s)
        return false;

    s->status = StepStatus::Pending;
    s->log.clear();
    s->exit_code = -1;

    m_executionQueue = {id};
    m_currentIndex = 0;
    m_running = true;
    m_cancelled = false;

    execute_next();
    return true;
}

void FlowEngine::cancel()
{
    if (!m_running)
        return;  // nothing in flight

    m_cancelled = true;

    // Mark everything still queued after the current step as skipped.
    for (size_t i = m_currentIndex + 1; i < m_executionQueue.size(); ++i) {
        FlowStep* s = step(m_executionQueue[i]);
        if (s)
            s->status = StepStatus::Skipped;
    }

    if (m_process && m_process->state() != QProcess::NotRunning) {
        // A process is in flight: killing it fires finished/errorOccurred, whose
        // handler finalizes the run (sets m_running=false and emits
        // flow_finished). m_cancelled stops the queue from advancing.
        m_process->kill();
        return;
    }

    // No process is driving completion (cancelled between steps): finalize here
    // so the engine is not left permanently "running" with no flow_finished.
    if (m_currentIndex < m_executionQueue.size()) {
        FlowStep* current = step(m_executionQueue[m_currentIndex]);
        if (current && current->status == StepStatus::Running)
            current->status = StepStatus::Skipped;
    }
    m_running = false;
    emit flow_finished(false);
}

bool FlowEngine::is_running() const
{
    return m_running;
}

// -- Private execution helpers ----------------------------------------------

void FlowEngine::execute_next()
{
    if (m_cancelled || m_currentIndex >= m_executionQueue.size()) {
        m_running = false;

        // Check overall success
        bool all_success = true;
        for (const auto& id : m_executionQueue) {
            const FlowStep* s = step(id);
            if (s && s->status != StepStatus::Success)
                all_success = false;
        }

        emit flow_finished(all_success);
        return;
    }

    FlowStep* s = step(m_executionQueue[m_currentIndex]);
    if (!s) {
        m_currentIndex++;
        execute_next();
        return;
    }

    start_process(*s);
}

void FlowEngine::start_process(FlowStep& flowStep)
{
    flowStep.status = StepStatus::Running;
    flowStep.log.clear();
    flowStep.exit_code = -1;
    emit step_started(QString::fromStdString(flowStep.id));

    m_process = std::make_unique<QProcess>(this);

    // Set working directory
    if (!m_workingDir.empty())
        m_process->setWorkingDirectory(QString::fromStdString(m_workingDir));

    // Set environment
    if (!m_environment.empty()) {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        for (const auto& [key, value] : m_environment)
            env.insert(QString::fromStdString(key), QString::fromStdString(value));
        m_process->setProcessEnvironment(env);
    }

    // Capture step ID for lambdas (flowStep reference may move)
    std::string step_id = flowStep.id;

    // Capture stdout
    connect(m_process.get(), &QProcess::readyReadStandardOutput, this,
        [this, step_id]() {
            FlowStep* s = step(step_id);
            if (!s || !m_process) return;
            QByteArray data = m_process->readAllStandardOutput();
            std::string text = data.toStdString();
            s->log += text;
            emit step_output(QString::fromStdString(step_id),
                             QString::fromUtf8(data));
        });

    // Capture stderr (appended to same log)
    connect(m_process.get(), &QProcess::readyReadStandardError, this,
        [this, step_id]() {
            FlowStep* s = step(step_id);
            if (!s || !m_process) return;
            QByteArray data = m_process->readAllStandardError();
            std::string text = data.toStdString();
            s->log += text;
            emit step_output(QString::fromStdString(step_id),
                             QString::fromUtf8(data));
        });

    // Handle process completion
    connect(m_process.get(),
        QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
        [this, step_id](int exitCode, QProcess::ExitStatus exitStatus) {
            FlowStep* s = step(step_id);
            // A crashed process fires both errorOccurred and finished. If
            // errorOccurred already finalized this step, bail out so we don't
            // emit step_finished/flow_finished twice and advance m_currentIndex
            // a second time.
            if (s && s->status != StepStatus::Running) {
                return;
            }
            if (s) {
                s->exit_code = exitCode;
                if (exitCode == 0 && exitStatus == QProcess::NormalExit) {
                    s->status = StepStatus::Success;
                } else {
                    s->status = StepStatus::Error;
                    if (s->log.empty())
                        s->log = "Process exited with code " + std::to_string(exitCode);
                }
            }

            bool success = (s && s->status == StepStatus::Success);
            emit step_finished(QString::fromStdString(step_id), success);

            m_currentIndex++;

            // If step failed, mark remaining as skipped and stop
            if (!success) {
                for (size_t i = m_currentIndex; i < m_executionQueue.size(); ++i) {
                    FlowStep* remaining = step(m_executionQueue[i]);
                    if (remaining)
                        remaining->status = StepStatus::Skipped;
                }
                m_running = false;
                emit flow_finished(false);
                return;
            }

            execute_next();
        });

    // Handle process errors (e.g., tool not found)
    connect(m_process.get(), &QProcess::errorOccurred, this,
        [this, step_id](QProcess::ProcessError error) {
            FlowStep* s = step(step_id);
            if (!s) return;

            // Avoid double-handling if finished signal already fired
            if (s->status != StepStatus::Running)
                return;

            s->status = StepStatus::Error;
            s->exit_code = -1;

            switch (error) {
                case QProcess::FailedToStart:
                    s->log += "Failed to start: tool not found or not executable";
                    break;
                case QProcess::Crashed:
                    s->log += "Process crashed";
                    break;
                case QProcess::Timedout:
                    s->log += "Process timed out";
                    break;
                default:
                    s->log += "Process error occurred";
                    break;
            }

            emit step_finished(QString::fromStdString(step_id), false);

            // Mark remaining as skipped
            for (size_t i = m_currentIndex + 1; i < m_executionQueue.size(); ++i) {
                FlowStep* remaining = step(m_executionQueue[i]);
                if (remaining)
                    remaining->status = StepStatus::Skipped;
            }

            m_running = false;
            emit flow_finished(false);
        });

    // Build and start the command
    QString program = build_program(flowStep);
    QStringList args = build_command_args(flowStep);
    m_process->start(program, args);
}

QString FlowEngine::build_program(const FlowStep& step) const
{
    if (!step.interpreter.empty())
        return QString::fromStdString(step.interpreter);
    return QString::fromStdString(step.tool_path);
}

QStringList FlowEngine::build_command_args(const FlowStep& step) const
{
    QStringList args;
    if (!step.interpreter.empty())
        args << QString::fromStdString(step.tool_path);
    for (const auto& arg : step.args)
        args << QString::fromStdString(arg);
    return args;
}

} // namespace chiplet
