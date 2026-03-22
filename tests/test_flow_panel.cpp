/**
 * test_flow_panel.cpp - UI state tests for FlowPanel
 */

#include <gtest/gtest.h>
#include <QApplication>
#include <QSignalSpy>
#include <QTableWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <thread>
#include <chrono>

#include "ui/FlowPanel.h"
#include "core/flow/FlowEngine.h"
#include "core/flow/FlowStep.h"

using namespace chiplet;

class FlowPanelTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!QApplication::instance()) {
            static int argc = 1;
            static char* argv[] = {const_cast<char*>("test")};
            static QApplication app(argc, argv);
        }
    }

    FlowStep make_step(const std::string& id, const std::string& name = "",
                       const std::string& tool = "/bin/echo",
                       const std::vector<std::string>& args = {},
                       const std::vector<std::string>& deps = {}) {
        FlowStep s;
        s.id = id;
        s.name = name.empty() ? id : name;
        s.tool_path = tool;
        s.args = args;
        s.depends_on = deps;
        return s;
    }

    bool wait_for_signals(QSignalSpy& spy, int expected_count, int timeout_ms = 5000) {
        for (int elapsed = 0; elapsed < timeout_ms && spy.count() < expected_count;
             elapsed += 50) {
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return spy.count() >= expected_count;
    }
};

TEST_F(FlowPanelTest, ConstructionNeverCrashes) {
    FlowPanel panel;
    EXPECT_NE(panel.table(), nullptr);
    EXPECT_NE(panel.log_display(), nullptr);
    EXPECT_NE(panel.stack(), nullptr);
    EXPECT_NE(panel.run_all_button(), nullptr);
    EXPECT_EQ(panel.flow_engine(), nullptr);
}

TEST_F(FlowPanelTest, EmptyStateByDefault) {
    FlowPanel panel;
    EXPECT_EQ(panel.stack()->currentIndex(), 0);
    EXPECT_FALSE(panel.run_all_button()->isEnabled());
}

TEST_F(FlowPanelTest, SetFlowEnginePopulatesTable) {
    FlowPanel panel;
    FlowEngine engine;
    engine.add_step(make_step("s1", "Step One"));
    engine.add_step(make_step("s2", "Step Two"));
    engine.add_step(make_step("s3", "Step Three"));

    panel.set_flow_engine(&engine);

    EXPECT_EQ(panel.stack()->currentIndex(), 1);
    EXPECT_EQ(panel.table()->rowCount(), 3);
    EXPECT_TRUE(panel.run_all_button()->isEnabled());
    EXPECT_EQ(panel.flow_engine(), &engine);
}

TEST_F(FlowPanelTest, SetFlowEngineNullShowsEmpty) {
    FlowPanel panel;
    FlowEngine engine;
    engine.add_step(make_step("s1"));

    panel.set_flow_engine(&engine);
    EXPECT_EQ(panel.stack()->currentIndex(), 1);

    panel.set_flow_engine(nullptr);
    EXPECT_EQ(panel.stack()->currentIndex(), 0);
    EXPECT_FALSE(panel.run_all_button()->isEnabled());
    EXPECT_EQ(panel.flow_engine(), nullptr);
}

TEST_F(FlowPanelTest, ZeroStepsShowsEmpty) {
    FlowPanel panel;
    FlowEngine engine;  // no steps

    panel.set_flow_engine(&engine);
    EXPECT_EQ(panel.stack()->currentIndex(), 0);
    EXPECT_FALSE(panel.run_all_button()->isEnabled());
}

TEST_F(FlowPanelTest, StepNamesInTable) {
    FlowPanel panel;
    FlowEngine engine;
    engine.add_step(make_step("extract", "Extract Symbols"));
    engine.add_step(make_step("drc", "Run DRC"));

    panel.set_flow_engine(&engine);

    auto* item0 = panel.table()->item(0, 0);
    auto* item1 = panel.table()->item(1, 0);
    ASSERT_NE(item0, nullptr);
    ASSERT_NE(item1, nullptr);
    EXPECT_TRUE(item0->text().contains("Extract Symbols"));
    EXPECT_TRUE(item1->text().contains("Run DRC"));

    // Step IDs stored in UserRole
    EXPECT_EQ(item0->data(Qt::UserRole).toString(), "extract");
    EXPECT_EQ(item1->data(Qt::UserRole).toString(), "drc");
}

TEST_F(FlowPanelTest, InitialStatusIsPending) {
    FlowPanel panel;
    FlowEngine engine;
    engine.add_step(make_step("s1"));
    panel.set_flow_engine(&engine);

    auto* statusItem = panel.table()->item(0, 1);
    ASSERT_NE(statusItem, nullptr);
    EXPECT_EQ(statusItem->text(), "Pending");
}

TEST_F(FlowPanelTest, RunStepUpdatesStatusAndTime) {
    FlowPanel panel;
    FlowEngine engine;
    engine.add_step(make_step("echo_step", "Echo Step", "/bin/echo",
                              {"hello from flow"}));
    panel.set_flow_engine(&engine);

    QSignalSpy finishedSpy(&engine, &FlowEngine::step_finished);
    engine.run_step("echo_step");

    ASSERT_TRUE(wait_for_signals(finishedSpy, 1));

    // Status should be Success
    auto* statusItem = panel.table()->item(0, 1);
    ASSERT_NE(statusItem, nullptr);
    EXPECT_EQ(statusItem->text(), "Success");

    // Time should be filled (not "--")
    auto* timeItem = panel.table()->item(0, 2);
    ASSERT_NE(timeItem, nullptr);
    EXPECT_NE(timeItem->text(), "--");
}

TEST_F(FlowPanelTest, FailedStepShowsError) {
    FlowPanel panel;
    FlowEngine engine;
    engine.add_step(make_step("fail_step", "Fail Step", "/bin/false"));
    panel.set_flow_engine(&engine);

    QSignalSpy finishedSpy(&engine, &FlowEngine::step_finished);
    engine.run_step("fail_step");

    ASSERT_TRUE(wait_for_signals(finishedSpy, 1));

    auto* statusItem = panel.table()->item(0, 1);
    ASSERT_NE(statusItem, nullptr);
    EXPECT_EQ(statusItem->text(), "Error");
}

TEST_F(FlowPanelTest, LogDisplayOnStepSelection) {
    FlowPanel panel;
    FlowEngine engine;
    engine.add_step(make_step("echo_step", "Echo Step", "/bin/echo",
                              {"test output line"}));
    panel.set_flow_engine(&engine);

    // Run the step so it produces output
    QSignalSpy finishedSpy(&engine, &FlowEngine::step_finished);
    engine.run_step("echo_step");
    ASSERT_TRUE(wait_for_signals(finishedSpy, 1));

    // Click the row to select it
    emit panel.table()->cellClicked(0, 0);
    QCoreApplication::processEvents();

    // Log should contain the output
    QString logText = panel.log_display()->toPlainText();
    EXPECT_TRUE(logText.contains("test output line"))
        << "Log was: " << logText.toStdString();
}

TEST_F(FlowPanelTest, RunAllSignalEmitted) {
    FlowPanel panel;
    FlowEngine engine;
    engine.add_step(make_step("s1"));
    panel.set_flow_engine(&engine);

    QSignalSpy spy(&panel, &FlowPanel::runAllRequested);
    panel.run_all_button()->click();

    EXPECT_EQ(spy.count(), 1);
}

TEST_F(FlowPanelTest, StepRunSignalEmitted) {
    FlowPanel panel;
    FlowEngine engine;
    engine.add_step(make_step("my_step", "My Step"));
    panel.set_flow_engine(&engine);

    QSignalSpy spy(&panel, &FlowPanel::stepRunRequested);

    // Find and click the per-row run button
    auto* runButton = qobject_cast<QPushButton*>(
        panel.table()->cellWidget(0, 3));
    ASSERT_NE(runButton, nullptr);
    runButton->click();

    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.at(0).at(0).toString(), "my_step");
}

TEST_F(FlowPanelTest, RunAllDisablesButtons) {
    FlowPanel panel;
    FlowEngine engine;
    engine.add_step(make_step("s1", "Step 1"));
    engine.add_step(make_step("s2", "Step 2"));
    panel.set_flow_engine(&engine);

    // Click Run All -- buttons should be disabled
    panel.run_all_button()->click();

    EXPECT_FALSE(panel.run_all_button()->isEnabled());
    auto* runButton = qobject_cast<QPushButton*>(
        panel.table()->cellWidget(0, 3));
    ASSERT_NE(runButton, nullptr);
    EXPECT_FALSE(runButton->isEnabled());
}

TEST_F(FlowPanelTest, ReconnectEngineDisconnectsOld) {
    FlowPanel panel;
    FlowEngine engine1;
    engine1.add_step(make_step("s1", "Step 1"));

    FlowEngine engine2;
    engine2.add_step(make_step("s2a", "Step A"));
    engine2.add_step(make_step("s2b", "Step B"));

    panel.set_flow_engine(&engine1);
    EXPECT_EQ(panel.table()->rowCount(), 1);

    panel.set_flow_engine(&engine2);
    EXPECT_EQ(panel.table()->rowCount(), 2);
    EXPECT_EQ(panel.flow_engine(), &engine2);

    // Verify step names from engine2
    auto* item = panel.table()->item(0, 0);
    EXPECT_TRUE(item->text().contains("Step A"));
}
