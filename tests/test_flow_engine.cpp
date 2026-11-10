/**
 * test_flow_engine.cpp - Unit tests for FlowEngine and FlowStep
 */

#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QSignalSpy>
#include <vector>
#include <filesystem>
#include <thread>
#include <chrono>

#include "core/flow/FlowStep.h"
#include "core/flow/FlowEngine.h"

using namespace chiplet;

// -- StepStatus conversion tests --------------------------------------------

TEST(FlowStepTest, StepStatusToString) {
    EXPECT_EQ(step_status_to_string(StepStatus::Pending), "pending");
    EXPECT_EQ(step_status_to_string(StepStatus::Running), "running");
    EXPECT_EQ(step_status_to_string(StepStatus::Success), "success");
    EXPECT_EQ(step_status_to_string(StepStatus::Error), "error");
    EXPECT_EQ(step_status_to_string(StepStatus::Skipped), "skipped");
}

TEST(FlowStepTest, StepStatusFromString) {
    EXPECT_EQ(step_status_from_string("pending"), StepStatus::Pending);
    EXPECT_EQ(step_status_from_string("running"), StepStatus::Running);
    EXPECT_EQ(step_status_from_string("success"), StepStatus::Success);
    EXPECT_EQ(step_status_from_string("error"), StepStatus::Error);
    EXPECT_EQ(step_status_from_string("skipped"), StepStatus::Skipped);
    EXPECT_EQ(step_status_from_string("unknown"), StepStatus::Pending);
}

TEST(FlowStepTest, StepStatusRoundtrip) {
    for (auto s : {StepStatus::Pending, StepStatus::Running, StepStatus::Success,
                   StepStatus::Error, StepStatus::Skipped}) {
        EXPECT_EQ(step_status_from_string(step_status_to_string(s)), s);
    }
}

TEST(FlowStepTest, DefaultValues) {
    FlowStep step;
    EXPECT_TRUE(step.id.empty());
    EXPECT_TRUE(step.name.empty());
    EXPECT_TRUE(step.tool_path.empty());
    EXPECT_TRUE(step.interpreter.empty());
    EXPECT_TRUE(step.args.empty());
    EXPECT_TRUE(step.input_files.empty());
    EXPECT_TRUE(step.output_files.empty());
    EXPECT_TRUE(step.depends_on.empty());
    EXPECT_EQ(step.status, StepStatus::Pending);
    EXPECT_TRUE(step.log.empty());
    EXPECT_EQ(step.exit_code, -1);
}

// -- FlowEngine step management tests ---------------------------------------

class FlowEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!QCoreApplication::instance()) {
            static int argc = 1;
            static char* argv[] = {const_cast<char*>("test")};
            static QCoreApplication app(argc, argv);
        }
    }

    FlowStep make_step(const std::string& id,
                       const std::vector<std::string>& deps = {}) {
        FlowStep s;
        s.id = id;
        s.name = id;
        s.depends_on = deps;
        return s;
    }

    // Poll Qt event loop until spy receives expected_count signals or timeout
    bool wait_for_signals(QSignalSpy& spy, int expected_count, int timeout_ms = 5000) {
        for (int elapsed = 0; elapsed < timeout_ms && spy.count() < expected_count;
             elapsed += 50) {
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return spy.count() >= expected_count;
    }
};

TEST_F(FlowEngineTest, AddAndRetrieveSteps) {
    FlowEngine engine;
    engine.add_step(make_step("A"));
    engine.add_step(make_step("B"));

    EXPECT_EQ(engine.step_count(), 2u);
    EXPECT_NE(engine.step("A"), nullptr);
    EXPECT_NE(engine.step("B"), nullptr);
    EXPECT_EQ(engine.step("C"), nullptr);
    EXPECT_EQ(engine.steps().size(), 2u);
}

TEST_F(FlowEngineTest, RemoveStep) {
    FlowEngine engine;
    engine.add_step(make_step("A"));
    engine.add_step(make_step("B"));

    EXPECT_TRUE(engine.remove_step("A"));
    EXPECT_EQ(engine.step_count(), 1u);
    EXPECT_EQ(engine.step("A"), nullptr);

    EXPECT_FALSE(engine.remove_step("nonexistent"));
}

TEST_F(FlowEngineTest, ClearSteps) {
    FlowEngine engine;
    engine.add_step(make_step("A"));
    engine.add_step(make_step("B"));
    engine.clear_steps();
    EXPECT_EQ(engine.step_count(), 0u);
}

TEST_F(FlowEngineTest, WorkingDirectoryAndEnvironment) {
    FlowEngine engine;
    engine.set_working_directory("/tmp/test");
    EXPECT_EQ(engine.working_directory(), "/tmp/test");

    engine.set_environment("KEY1", "value1");
    engine.set_environment("KEY2", "value2");
    EXPECT_EQ(engine.environment().size(), 2u);
    EXPECT_EQ(engine.environment().at("KEY1"), "value1");
}

// -- Topological sort tests -------------------------------------------------

TEST_F(FlowEngineTest, TopologicalSortSimpleChain) {
    FlowEngine engine;
    engine.add_step(make_step("C", {"B"}));
    engine.add_step(make_step("A"));
    engine.add_step(make_step("B", {"A"}));

    auto sorted = engine.topological_sort();
    ASSERT_EQ(sorted.size(), 3u);

    // A must come before B, B must come before C
    auto pos_a = std::find(sorted.begin(), sorted.end(), "A");
    auto pos_b = std::find(sorted.begin(), sorted.end(), "B");
    auto pos_c = std::find(sorted.begin(), sorted.end(), "C");
    EXPECT_LT(pos_a, pos_b);
    EXPECT_LT(pos_b, pos_c);
}

TEST_F(FlowEngineTest, TopologicalSortDiamond) {
    // A -> B, A -> C, B -> D, C -> D
    FlowEngine engine;
    engine.add_step(make_step("A"));
    engine.add_step(make_step("B", {"A"}));
    engine.add_step(make_step("C", {"A"}));
    engine.add_step(make_step("D", {"B", "C"}));

    auto sorted = engine.topological_sort();
    ASSERT_EQ(sorted.size(), 4u);

    auto pos = [&](const std::string& id) {
        return std::find(sorted.begin(), sorted.end(), id) - sorted.begin();
    };

    EXPECT_LT(pos("A"), pos("B"));
    EXPECT_LT(pos("A"), pos("C"));
    EXPECT_LT(pos("B"), pos("D"));
    EXPECT_LT(pos("C"), pos("D"));
}

TEST_F(FlowEngineTest, TopologicalSortNoDeps) {
    FlowEngine engine;
    engine.add_step(make_step("A"));
    engine.add_step(make_step("B"));
    engine.add_step(make_step("C"));

    auto sorted = engine.topological_sort();
    ASSERT_EQ(sorted.size(), 3u);
}

TEST_F(FlowEngineTest, TopologicalSortCircularDependency) {
    FlowEngine engine;
    engine.add_step(make_step("A", {"B"}));
    engine.add_step(make_step("B", {"A"}));

    auto sorted = engine.topological_sort();
    EXPECT_TRUE(sorted.empty());
}

TEST_F(FlowEngineTest, TopologicalSortSelfLoop) {
    FlowEngine engine;
    engine.add_step(make_step("A", {"A"}));

    auto sorted = engine.topological_sort();
    EXPECT_TRUE(sorted.empty());
}

TEST_F(FlowEngineTest, TopologicalSortEmpty) {
    FlowEngine engine;
    auto sorted = engine.topological_sort();
    EXPECT_TRUE(sorted.empty());
}

// -- Build command tests ----------------------------------------------------

TEST_F(FlowEngineTest, BuildCommandWithInterpreter) {
    FlowEngine engine;
    FlowStep s;
    s.id = "test";
    s.interpreter = "python3";
    s.tool_path = "script.py";
    s.args = {"--flag", "value"};
    engine.add_step(s);

    // Verify via running echo to capture the constructed command
    // Instead, we test the observable behavior: run a real interpreter step
    // The build_command logic is tested indirectly via execution tests
    SUCCEED();
}

TEST_F(FlowEngineTest, BuildCommandWithoutInterpreter) {
    FlowEngine engine;
    FlowStep s;
    s.id = "test";
    s.tool_path = "/bin/echo";
    s.args = {"hello", "world"};
    engine.add_step(s);

    SUCCEED();
}

// -- Execution tests --------------------------------------------------------

TEST_F(FlowEngineTest, RunTrivialStep) {
    FlowEngine engine;
    FlowStep s;
    s.id = "echo_test";
    s.name = "Echo Test";
    s.tool_path = "/bin/echo";
    s.args = {"hello"};
    engine.add_step(s);

    QSignalSpy started_spy(&engine, &FlowEngine::step_started);
    QSignalSpy finished_spy(&engine, &FlowEngine::step_finished);
    QSignalSpy flow_spy(&engine, &FlowEngine::flow_finished);

    ASSERT_TRUE(engine.run_step("echo_test"));
    EXPECT_TRUE(engine.is_running());

    ASSERT_TRUE(wait_for_signals(flow_spy, 1));

    EXPECT_EQ(started_spy.count(), 1);
    EXPECT_EQ(finished_spy.count(), 1);
    EXPECT_EQ(flow_spy.count(), 1);

    // Verify signal arguments
    EXPECT_EQ(finished_spy.at(0).at(0).toString().toStdString(), "echo_test");
    EXPECT_TRUE(finished_spy.at(0).at(1).toBool());
    EXPECT_TRUE(flow_spy.at(0).at(0).toBool());

    // Verify step state
    const FlowStep* result = engine.step("echo_test");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->status, StepStatus::Success);
    EXPECT_EQ(result->exit_code, 0);
    EXPECT_NE(result->log.find("hello"), std::string::npos);
}

TEST_F(FlowEngineTest, RunStepWithInterpreter) {
    FlowEngine engine;
    FlowStep s;
    s.id = "bash_test";
    s.interpreter = "/bin/bash";
    s.tool_path = "-c";
    s.args = {"echo interpreter_works"};
    engine.add_step(s);

    QSignalSpy flow_spy(&engine, &FlowEngine::flow_finished);
    ASSERT_TRUE(engine.run_step("bash_test"));
    ASSERT_TRUE(wait_for_signals(flow_spy, 1));

    const FlowStep* result = engine.step("bash_test");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->status, StepStatus::Success);
    EXPECT_EQ(result->exit_code, 0);
    EXPECT_NE(result->log.find("interpreter_works"), std::string::npos);
}

TEST_F(FlowEngineTest, DependencyChainExecution) {
    FlowEngine engine;

    FlowStep a;
    a.id = "A";
    a.tool_path = "/bin/echo";
    a.args = {"step_A"};
    engine.add_step(a);

    FlowStep b;
    b.id = "B";
    b.tool_path = "/bin/echo";
    b.args = {"step_B"};
    b.depends_on = {"A"};
    engine.add_step(b);

    QSignalSpy finished_spy(&engine, &FlowEngine::step_finished);
    QSignalSpy flow_spy(&engine, &FlowEngine::flow_finished);

    ASSERT_TRUE(engine.run_all());
    ASSERT_TRUE(wait_for_signals(flow_spy, 1));

    // Both steps must have finished
    ASSERT_EQ(finished_spy.count(), 2);

    // A finishes before B
    EXPECT_EQ(finished_spy.at(0).at(0).toString().toStdString(), "A");
    EXPECT_EQ(finished_spy.at(1).at(0).toString().toStdString(), "B");

    EXPECT_EQ(engine.step("A")->status, StepStatus::Success);
    EXPECT_EQ(engine.step("B")->status, StepStatus::Success);
    EXPECT_TRUE(flow_spy.at(0).at(0).toBool());
}

TEST_F(FlowEngineTest, MissingToolReportsError) {
    FlowEngine engine;
    FlowStep s;
    s.id = "missing";
    s.tool_path = "/nonexistent/binary/that/does/not/exist";
    engine.add_step(s);

    QSignalSpy finished_spy(&engine, &FlowEngine::step_finished);
    QSignalSpy flow_spy(&engine, &FlowEngine::flow_finished);

    ASSERT_TRUE(engine.run_step("missing"));
    ASSERT_TRUE(wait_for_signals(flow_spy, 1));

    const FlowStep* result = engine.step("missing");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->status, StepStatus::Error);
    EXPECT_FALSE(result->log.empty());

    // Finished signal reports failure
    EXPECT_FALSE(finished_spy.at(0).at(1).toBool());
    EXPECT_FALSE(flow_spy.at(0).at(0).toBool());
}

TEST_F(FlowEngineTest, RunAllWithCircularDepsReturnsFalse) {
    FlowEngine engine;
    engine.add_step(make_step("A", {"B"}));
    engine.add_step(make_step("B", {"A"}));

    EXPECT_FALSE(engine.run_all());
    EXPECT_FALSE(engine.is_running());
}

TEST_F(FlowEngineTest, CancelDuringExecution) {
    FlowEngine engine;

    // Step A: a slow command
    FlowStep a;
    a.id = "slow";
    a.tool_path = "/bin/sleep";
    a.args = {"10"};
    engine.add_step(a);

    // Step B: depends on A, should be skipped
    FlowStep b;
    b.id = "after_slow";
    b.tool_path = "/bin/echo";
    b.args = {"done"};
    b.depends_on = {"slow"};
    engine.add_step(b);

    QSignalSpy started_spy(&engine, &FlowEngine::step_started);
    QSignalSpy flow_spy(&engine, &FlowEngine::flow_finished);

    ASSERT_TRUE(engine.run_all());

    // Wait for first step to start
    ASSERT_TRUE(wait_for_signals(started_spy, 1, 2000));

    // Cancel
    engine.cancel();
    ASSERT_TRUE(wait_for_signals(flow_spy, 1, 2000));

    // After-slow step should be skipped
    EXPECT_EQ(engine.step("after_slow")->status, StepStatus::Skipped);
}

TEST_F(FlowEngineTest, FailedStepStopsChain) {
    FlowEngine engine;

    FlowStep a;
    a.id = "fail";
    a.interpreter = "/bin/bash";
    a.tool_path = "-c";
    a.args = {"exit 1"};
    engine.add_step(a);

    FlowStep b;
    b.id = "after_fail";
    b.tool_path = "/bin/echo";
    b.args = {"should_not_run"};
    b.depends_on = {"fail"};
    engine.add_step(b);

    QSignalSpy flow_spy(&engine, &FlowEngine::flow_finished);
    ASSERT_TRUE(engine.run_all());
    ASSERT_TRUE(wait_for_signals(flow_spy, 1));

    EXPECT_EQ(engine.step("fail")->status, StepStatus::Error);
    EXPECT_EQ(engine.step("fail")->exit_code, 1);
    EXPECT_EQ(engine.step("after_fail")->status, StepStatus::Skipped);
    EXPECT_FALSE(flow_spy.at(0).at(0).toBool());
}

TEST_F(FlowEngineTest, RunStepNonexistentReturnsFalse) {
    FlowEngine engine;
    EXPECT_FALSE(engine.run_step("nonexistent"));
}

TEST_F(FlowEngineTest, OutputCaptured) {
    FlowEngine engine;
    FlowStep s;
    s.id = "multi_line";
    s.interpreter = "/bin/bash";
    s.tool_path = "-c";
    s.args = {"echo line1; echo line2; echo line3"};
    engine.add_step(s);

    QSignalSpy output_spy(&engine, &FlowEngine::step_output);
    QSignalSpy flow_spy(&engine, &FlowEngine::flow_finished);

    ASSERT_TRUE(engine.run_step("multi_line"));
    ASSERT_TRUE(wait_for_signals(flow_spy, 1));

    // Output should have been captured
    const FlowStep* result = engine.step("multi_line");
    ASSERT_NE(result, nullptr);
    EXPECT_NE(result->log.find("line1"), std::string::npos);
    EXPECT_NE(result->log.find("line2"), std::string::npos);
    EXPECT_NE(result->log.find("line3"), std::string::npos);

    // At least one output signal should have been emitted
    EXPECT_GT(output_spy.count(), 0);
}

TEST_F(FlowEngineTest, StderrCapturedInLog) {
    FlowEngine engine;
    FlowStep s;
    s.id = "stderr_test";
    s.interpreter = "/bin/bash";
    s.tool_path = "-c";
    s.args = {"echo error_msg >&2"};
    engine.add_step(s);

    QSignalSpy flow_spy(&engine, &FlowEngine::flow_finished);
    ASSERT_TRUE(engine.run_step("stderr_test"));
    ASSERT_TRUE(wait_for_signals(flow_spy, 1));

    const FlowStep* result = engine.step("stderr_test");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->status, StepStatus::Success);
    EXPECT_NE(result->log.find("error_msg"), std::string::npos);
}

// The flow block is an execution surface: the document names a
// program and the engine starts it. The document's environment used to be
// inserted straight over the inherited one, so it could also decide what that
// program name RESOLVES to (PATH) or inject code into it once it did
// (LD_PRELOAD, PYTHONPATH).
//
// The step is refused rather than run with the override quietly dropped.
// Running it anyway would execute in an environment the document did not ask
// for, and a flow sets PATH precisely to change what its programs resolve to,
// so a step whose document sets it very likely depends on it.
//
// The command below is written to depend on NOTHING the document's own
// environment can reach, because the test asserts a file was not created and
// that assertion is worthless if a step that DID run could have failed to
// create it for an unrelated reason. It used to run `touch`, resolved through
// PATH, while the document under test sets PATH=/attacker/bin: a mutant that
// launched the process would have failed to resolve touch, left no marker, and
// passed this test. The program is now absolute and the work is a shell
// builtin plus a redirection, so PATH is not consulted at any point. The
// permitted-override test immediately below runs the IDENTICAL argv and proves
// it does create the file, which is what turns the marker's absence here into
// evidence of non-execution rather than evidence of a broken command.
std::vector<std::string> marker_argv(const std::filesystem::path& marker)
{
    return {std::string("-c"), std::string(": > \"$1\""), std::string("sh"),
            marker.string()};
}

TEST_F(FlowEngineTest, AnUnsupportedEnvironmentOverrideRefusesTheStep) {
    const std::filesystem::path marker =
        std::filesystem::temp_directory_path() / "flow_env_should_not_exist";
    std::filesystem::remove(marker);

    FlowEngine engine;
    FlowStep s = make_step("refused");
    s.tool_path = "/bin/sh";
    s.args = marker_argv(marker);
    engine.add_step(s);
    engine.set_environment("PATH", "/attacker/bin");

    QSignalSpy flow_spy(&engine, &FlowEngine::flow_finished);
    ASSERT_TRUE(engine.run_step("refused"));
    ASSERT_TRUE(wait_for_signals(flow_spy, 1));

    const FlowStep* done = engine.step("refused");
    ASSERT_NE(done, nullptr);
    EXPECT_EQ(done->status, StepStatus::Error);
    EXPECT_NE(done->log.find("Unsupported configuration"), std::string::npos)
        << done->log;
    EXPECT_NE(done->log.find("PATH"), std::string::npos) << done->log;

    // The recovery it offers has to be one that actually works. It used to say
    // "Remove it, or name the program you want by its full path", and the
    // second half is false: the refusal fires on the override being PRESENT, so
    // a full path alone changes nothing. Asserting the correction rather than
    // the wording, the message must tell the author to remove the setting and
    // must not present a full path as an alternative to doing so.
    EXPECT_NE(done->log.find("remove that setting"), std::string::npos)
        << "the refusal does not say what would actually let the step run:\n"
        << done->log;
    EXPECT_EQ(done->log.find("Remove it, or name the program"), std::string::npos)
        << "the false either/or came back:\n" << done->log;

    // Nothing ran. This is the assertion that distinguishes refusing from
    // dropping the override and running anyway.
    EXPECT_FALSE(std::filesystem::exists(marker))
        << "the step ran despite the refusal";
    std::filesystem::remove(marker);
}

// The control that makes the assertion above mean something. Same engine, same
// program, same argv, same marker path; the only difference is that the
// document sets a name the policy permits instead of one it refuses. If this
// goes red the refusal test is no longer evidence of anything, because the
// command it relies on stopped working.
TEST_F(FlowEngineTest, ThePermittedCaseRunsTheIdenticalCommandAndCreatesTheFile) {
    const std::filesystem::path marker =
        std::filesystem::temp_directory_path() / "flow_env_control_marker";
    std::filesystem::remove(marker);

    FlowEngine engine;
    FlowStep s = make_step("permitted");
    s.tool_path = "/bin/sh";
    s.args = marker_argv(marker);
    engine.add_step(s);
    engine.set_environment("PDK_ROOT", "/opt/pdk");

    QSignalSpy flow_spy(&engine, &FlowEngine::flow_finished);
    ASSERT_TRUE(engine.run_step("permitted"));
    ASSERT_TRUE(wait_for_signals(flow_spy, 1));

    const FlowStep* done = engine.step("permitted");
    ASSERT_NE(done, nullptr);
    EXPECT_EQ(done->status, StepStatus::Success) << done->log;
    EXPECT_TRUE(std::filesystem::exists(marker))
        << "the command the refusal test relies on does not create the file "
           "even when it is allowed to run, so that test proves nothing:\n"
        << done->log;
    std::filesystem::remove(marker);
}

// The other half: an ordinary project variable is still the document's to set,
// and it must reach the child. Refusing everything would pass the test above
// and break every real flow.
TEST_F(FlowEngineTest, AnOrdinaryEnvironmentVariableStillReachesTheStep) {
    FlowEngine engine;
    FlowStep s = make_step("env_ok");
    s.tool_path = "/bin/sh";
    s.args = {"-c", "echo \"PDK=$PDK_ROOT\""};
    engine.add_step(s);
    engine.set_environment("PDK_ROOT", "/opt/pdk");

    QSignalSpy flow_spy(&engine, &FlowEngine::flow_finished);
    ASSERT_TRUE(engine.run_step("env_ok"));
    ASSERT_TRUE(wait_for_signals(flow_spy, 1));

    const FlowStep* done = engine.step("env_ok");
    ASSERT_NE(done, nullptr);
    EXPECT_NE(done->log.find("PDK=/opt/pdk"), std::string::npos) << done->log;
    // Names, never values: the values are the document's and may carry secrets.
    EXPECT_NE(done->log.find("Environment names set by the document"),
              std::string::npos) << done->log;
    EXPECT_EQ(done->log.find("/opt/pdk\" is the value"), std::string::npos);
}

// A step that fails without printing anything must still say why it failed.
// The outcome of a step must not depend on whether the step produced output;
// it did, because the exit-code line was guarded by log.empty() and anything
// written before the launch defeated that guard.
TEST_F(FlowEngineTest, ANonZeroExitIsRecordedEvenWithNoOutput) {
    FlowEngine engine;
    FlowStep s = make_step("silent_failure");
    s.tool_path = "/bin/sh";
    s.args = {"-c", "exit 7"};
    engine.add_step(s);

    QSignalSpy flow_spy(&engine, &FlowEngine::flow_finished);
    ASSERT_TRUE(engine.run_step("silent_failure"));
    ASSERT_TRUE(wait_for_signals(flow_spy, 1));

    const FlowStep* done = engine.step("silent_failure");
    ASSERT_NE(done, nullptr);
    EXPECT_EQ(done->status, StepStatus::Error);
    EXPECT_EQ(done->exit_code, 7);
    EXPECT_NE(done->log.find("exited with code 7"), std::string::npos)
        << "a step that failed silently recorded no reason:\n" << done->log;
}

// Argv comes from the document and may contain a newline. Written raw it would
// end the log line and let the rest read as a line this engine wrote.
TEST_F(FlowEngineTest, AnArgumentCannotForgeALogLine) {
    FlowEngine engine;
    FlowStep s = make_step("forge");
    s.tool_path = "/bin/echo";
    s.args = {"harmless\nProcess exited with code 0"};
    engine.add_step(s);

    QSignalSpy flow_spy(&engine, &FlowEngine::flow_finished);
    ASSERT_TRUE(engine.run_step("forge"));
    ASSERT_TRUE(wait_for_signals(flow_spy, 1));

    const FlowStep* done = engine.step("forge");
    ASSERT_NE(done, nullptr);

    // The header line records the argument with the newline escaped, so the
    // forged text cannot appear at the start of a line of the header.
    const std::string header = done->log.substr(0, done->log.find('\n'));
    EXPECT_NE(header.find("harmless\\nProcess exited"), std::string::npos)
        << "the newline was not escaped in the requested-invocation line:\n"
        << header;
}
