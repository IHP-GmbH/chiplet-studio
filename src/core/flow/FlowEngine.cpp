// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * FlowEngine.cpp - Generic subprocess pipeline runner implementation
 */

#include "FlowEngine.h"

#include <optional>
#include <set>
#include <QCoreApplication>
#include <QDebug>
#include <algorithm>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace chiplet {

namespace {

// Environment names a .chiplet may not set for a step it runs.
//
// The flow block is an execution surface: the document names a program and this
// engine starts it. That is the feature. What it must not also be able to do is
// change what that name RESOLVES to, or inject code into the process once it
// resolves, because then a step that reads as `tool: python3` is not python3.
// The document's map used to be inserted straight over the inherited
// environment, so PATH, LD_PRELOAD and PYTHONPATH were all in reach.
//
// This is a deny-list, and a deny-list has the wrong polarity: forgetting a
// name fails open and says nothing. It is used anyway because the permitted
// side is genuinely open-ended (PDK_ROOT and whatever else a project needs),
// so there is no allow-list to write.
//
// Scope, stated the same way as the policy at the use site in start_process:
// this is a BOUNDED POLICY about which overrides this host honours, not a
// sandbox, and it does not make an untrusted flow safe. The inherited
// environment, the program and the argv all remain under the document's
// influence. The step's log names every variable the document sets, which is
// a record, not a mitigation: a dangerous name nobody listed still takes
// effect, and reading the log afterwards is not the same as refusing it. A
// name missing from this list is honoured with nothing going red anywhere.
bool refuses_environment_name(const std::string& name)
{
    // Families. Everything under these prefixes changes dynamic loading.
    static const char* kRefusedPrefixes[] = { "LD_", "DYLD_" };
    for (const char* prefix : kRefusedPrefixes) {
        if (name.rfind(prefix, 0) == 0) return true;
    }

    static const std::set<std::string> kRefusedNames = {
        // What a bare program name resolves to, and how a shell would read it.
        "PATH", "SHELL", "IFS", "ENV", "BASH_ENV", "CDPATH", "GLOBIGNORE",
        // Loader and locale lookup paths that pull in code.
        "GCONV_PATH", "LOCPATH", "NLSPATH", "HOSTALIASES", "RESOLV_HOST_CONF",
        // Language runtimes that import from a path or take options as code.
        "PYTHONPATH", "PYTHONHOME", "PYTHONSTARTUP", "PYTHONEXECUTABLE",
        "PERL5LIB", "PERL5OPT", "PERLLIB", "RUBYLIB", "RUBYOPT",
        "NODE_OPTIONS", "NODE_PATH", "CLASSPATH",
        "JAVA_TOOL_OPTIONS", "_JAVA_OPTIONS",
        // Qt loads these into any Qt child, including ours.
        "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH",
    };
    return kRefusedNames.count(name) > 0;
}

// Render an untrusted string for a single log line.
//
// Everything on these lines comes from the document: variable names, the
// program, the arguments. A name or an argument may legally contain a newline,
// and a newline written raw would end the line and let the rest read as a line
// this engine wrote. So control characters are escaped and the result is
// quoted, which also keeps an argument containing spaces from reading as two.
std::string for_log(const std::string& raw)
{
    std::string out = "\"";
    for (const unsigned char c : raw) {
        switch (c) {
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            default:
                if (c < 0x20 || c == 0x7f) {
                    static const char* kHex = "0123456789abcdef";
                    out += "\\x";
                    out += kHex[(c >> 4) & 0xf];
                    out += kHex[c & 0xf];
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += "\"";
    return out;
}

} // namespace


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

void FlowEngine::fail_before_launch(FlowStep& flowStep, const std::string& reason)
{
    flowStep.status = StepStatus::Error;
    flowStep.log += reason + "\n";

    emit step_finished(QString::fromStdString(flowStep.id), false);

    for (size_t i = m_currentIndex + 1; i < m_executionQueue.size(); ++i) {
        FlowStep* remaining = step(m_executionQueue[i]);
        if (remaining)
            remaining->status = StepStatus::Skipped;
    }
    m_running = false;
    emit flow_finished(false);
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

    // Environment policy.
    //
    // This is a BOUNDED POLICY about which overrides this host will honour. It
    // is not a sandbox and does not make an untrusted flow safe: the inherited
    // environment, the program and the argv all remain under the document's
    // influence, and the logging below does not compensate for that.
    //
    // The step is REFUSED rather than run with the override dropped. Running it
    // anyway would execute in an environment the document did not ask for, and
    // for exactly these names that is not a detail: a flow sets PATH in order
    // to change what its programs resolve to, so a step whose document sets it
    // very likely depends on it. Silently ignoring the request would produce a
    // run that looks successful and means something else. Refusing says the
    // configuration is unsupported and runs nothing.
    if (!m_environment.empty()) {
        std::string unsupported;
        for (const auto& [key, value] : m_environment) {
            if (!refuses_environment_name(key)) continue;
            if (!unsupported.empty()) unsupported += ", ";
            unsupported += for_log(key);
        }
        if (!unsupported.empty()) {
            fail_before_launch(
                flowStep,
                "Unsupported configuration: this document asks to set " +
                unsupported +
                " for the step, and this host does not honour overrides of the "
                "variables that decide which program a name resolves to or what "
                "is loaded into it. The step was NOT run, because dropping the "
                "override and running anyway would execute in an environment "
                "the document did not ask for. To run this step, remove that "
                "setting; naming a full path does not lift the refusal on its "
                "own. If the setting was PATH and its purpose was to choose "
                "which program runs, remove it and name that program by its "
                "full path in the step instead.");
            return;
        }

        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        std::string applied;
        for (const auto& [key, value] : m_environment) {
            env.insert(QString::fromStdString(key), QString::fromStdString(value));
            if (!applied.empty()) applied += ", ";
            applied += for_log(key);
        }
        if (!applied.empty()) {
            // Names, not values. That is a statement about this line only, and
            // not a promise that the step's secrets stay out of the log: the
            // values still reach the child process, and the invocation line
            // below writes argv in full, so anything the document passes as an
            // argument is recorded verbatim. Nothing here redacts.
            flowStep.log += "Environment names set by the document: " + applied + "\n";
        }
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
                    // Unconditionally. This used to be guarded by log.empty(),
                    // which stopped working the moment anything was written to
                    // the log before the launch: a step that exits non-zero
                    // having printed nothing then recorded no reason at all.
                    // The outcome of a step must not depend on whether the step
                    // happened to produce output.
                    s->log += "Process exited with code " +
                              std::to_string(exitCode) + "\n";
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

    // Build and start the command.
    //
    // A step whose declared shape names no runnable program does not get
    // started. It used to: build_program returned the empty string and
    // QProcess::start("") produced a failure whose message says nothing about
    // the actual cause, which is that the document declared a `script:` with no
    // `tool:` to run it with. Say that instead, and say it where the user is
    // already looking, in the step's own log.
    const std::optional<std::string> program = flowStep.program();
    if (!program) {
        fail_before_launch(
            flowStep,
            "This step names no program to run. It declares a 'script:' but no "
            "'tool:', so there is nothing to execute it with; add a 'tool:' "
            "naming the interpreter (for example tool: python3). Nothing was "
            "run.");
        return;
    }

    QStringList args = build_command_args(flowStep);

    // What was ASKED FOR, recorded before the launch is attempted.
    //
    // Deliberately not called the resolved program: FlowStep::program() only
    // chooses between the document's interpreter and its tool_path, and neither
    // has been looked up on PATH or checked for existence. What actually gets
    // executed is decided by the OS after this line is written, so this line is
    // a record of the request and is not evidence that anything ran. The exit
    // result recorded when the process finishes is that evidence.
    //
    // Each item is escaped and quoted: argv entries come from the document and
    // may contain spaces or newlines, and a raw newline here would let a
    // crafted argument forge a line that reads as one this engine wrote.
    //
    // Escaping is not redaction. Every argument is written out in full, so a
    // credential passed as an argument by the document ends up in this log and
    // in anything that collects it. If that matters for a given flow, the fix
    // is not to pass it on the command line.
    {
        std::string line = "Requested invocation: " + for_log(*program);
        for (const QString& a : args) line += " " + for_log(a.toStdString());
        flowStep.log += line + "\n";
    }

    m_process->start(QString::fromStdString(*program), args);
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
