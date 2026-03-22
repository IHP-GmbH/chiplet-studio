/**
 * FlowEngine.h - Generic subprocess pipeline runner
 *
 * Manages a collection of FlowSteps, resolves dependencies via topological
 * sort, and executes steps as async subprocesses via QProcess.
 * Contains ZERO business logic -- it is a pure execution framework.
 */

#ifndef CHIPLET_CORE_FLOW_FLOWENGINE_H
#define CHIPLET_CORE_FLOW_FLOWENGINE_H

#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <string>
#include <vector>
#include <memory>
#include <map>
#include "FlowStep.h"

namespace chiplet {

class FlowEngine : public QObject {
    Q_OBJECT

public:
    typedef std::vector<FlowStep> step_list_type;

    explicit FlowEngine(QObject* parent = nullptr);
    ~FlowEngine() override;

    // Non-copyable (QObject)
    FlowEngine(const FlowEngine&) = delete;
    FlowEngine& operator=(const FlowEngine&) = delete;

    // Step management
    void add_step(const FlowStep& step);
    bool remove_step(const std::string& id);
    FlowStep* step(const std::string& id);
    const FlowStep* step(const std::string& id) const;
    const step_list_type& steps() const;
    size_t step_count() const;
    void clear_steps();

    // Execution
    bool run_step(const std::string& id);
    bool run_all();
    void cancel();
    bool is_running() const;

    // Configuration
    void set_working_directory(const std::string& dir);
    const std::string& working_directory() const;
    void set_environment(const std::string& key, const std::string& value);
    const std::map<std::string, std::string>& environment() const;

    // Dependency analysis (public for testability)
    std::vector<std::string> topological_sort() const;

signals:
    void step_started(const QString& id);
    void step_finished(const QString& id, bool success);
    void step_output(const QString& id, const QString& text);
    void flow_finished(bool all_success);

private:
    void execute_next();
    void start_process(FlowStep& step);
    QString build_program(const FlowStep& step) const;
    QStringList build_command_args(const FlowStep& step) const;

    step_list_type m_steps;
    std::string m_workingDir;
    std::map<std::string, std::string> m_environment;

    // Runtime state
    std::unique_ptr<QProcess> m_process;
    std::vector<std::string> m_executionQueue;
    size_t m_currentIndex = 0;
    bool m_running = false;
    bool m_cancelled = false;
};

} // namespace chiplet

#endif // CHIPLET_CORE_FLOW_FLOWENGINE_H
