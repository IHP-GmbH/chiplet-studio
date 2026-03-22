/**
 * FlowPanel.cpp - Implementation
 */

#include "FlowPanel.h"
#include "core/flow/FlowEngine.h"
#include "core/flow/FlowStep.h"

#include <QTableWidget>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QPushButton>
#include <QLabel>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFont>
#include <QScrollBar>

namespace chiplet {

FlowPanel::FlowPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
}

FlowPanel::~FlowPanel() = default;

void FlowPanel::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Header bar
    auto* headerWidget = new QWidget(this);
    auto* headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(6, 4, 6, 4);

    auto* titleLabel = new QLabel("Flow Pipeline", headerWidget);
    QFont titleFont = titleLabel->font();
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    headerLayout->addWidget(titleLabel);

    headerLayout->addStretch();

    m_statusLabel = new QLabel(headerWidget);
    headerLayout->addWidget(m_statusLabel);

    m_runAllButton = new QPushButton("Run All", headerWidget);
    m_runAllButton->setEnabled(false);
    connect(m_runAllButton, &QPushButton::clicked,
            this, &FlowPanel::onRunAllClicked);
    headerLayout->addWidget(m_runAllButton);

    mainLayout->addWidget(headerWidget);

    // Stacked widget: index 0 = empty, index 1 = populated
    m_stack = new QStackedWidget(this);

    // Empty state
    m_emptyLabel = new QLabel("No flow defined in this assembly", m_stack);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setStyleSheet("color: #888; font-style: italic;");
    m_stack->addWidget(m_emptyLabel);

    // Populated state: splitter with table + log
    m_splitter = new QSplitter(Qt::Vertical, m_stack);

    // Step table
    m_table = new QTableWidget(m_splitter);
    m_table->setColumnCount(NUM_COLUMNS);
    m_table->setHorizontalHeaderLabels({"Step", "Status", "Time", ""});
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(COL_STEP, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(COL_STATUS, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(COL_TIME, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(COL_RUN, QHeaderView::Fixed);
    m_table->horizontalHeader()->resizeSection(COL_STATUS, 90);
    m_table->horizontalHeader()->resizeSection(COL_TIME, 80);
    m_table->horizontalHeader()->resizeSection(COL_RUN, 50);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);

    connect(m_table, &QTableWidget::cellClicked,
            this, &FlowPanel::onTableCellClicked);

    m_splitter->addWidget(m_table);

    // Log display
    m_logDisplay = new QPlainTextEdit(m_splitter);
    m_logDisplay->setReadOnly(true);
    m_logDisplay->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont monoFont("Monospace", 9);
    monoFont.setStyleHint(QFont::Monospace);
    m_logDisplay->setFont(monoFont);
    m_logDisplay->setStyleSheet(
        "QPlainTextEdit { background-color: #1e1e1e; color: #d4d4d4; }");

    m_splitter->addWidget(m_logDisplay);
    m_splitter->setStretchFactor(0, 3);
    m_splitter->setStretchFactor(1, 2);

    m_stack->addWidget(m_splitter);
    m_stack->setCurrentIndex(0);

    mainLayout->addWidget(m_stack);
}

void FlowPanel::set_flow_engine(FlowEngine* engine)
{
    if (m_engine) {
        disconnect(m_engine, nullptr, this, nullptr);
    }

    m_engine = engine;
    m_timers.clear();
    m_elapsedMs.clear();
    m_selectedStepId.clear();
    m_logDisplay->clear();
    m_statusLabel->clear();

    if (!m_engine || m_engine->step_count() == 0) {
        m_stack->setCurrentIndex(0);
        m_runAllButton->setEnabled(false);
        clearTable();
        return;
    }

    connect(m_engine, &FlowEngine::step_started,
            this, &FlowPanel::onStepStarted);
    connect(m_engine, &FlowEngine::step_finished,
            this, &FlowPanel::onStepFinished);
    connect(m_engine, &FlowEngine::step_output,
            this, &FlowPanel::onStepOutput);
    connect(m_engine, &FlowEngine::flow_finished,
            this, &FlowPanel::onFlowFinished);

    populateTable();
    m_stack->setCurrentIndex(1);
    m_runAllButton->setEnabled(true);
}

FlowEngine* FlowPanel::flow_engine() const
{
    return m_engine;
}

QTableWidget* FlowPanel::table() const
{
    return m_table;
}

QPlainTextEdit* FlowPanel::log_display() const
{
    return m_logDisplay;
}

QStackedWidget* FlowPanel::stack() const
{
    return m_stack;
}

QPushButton* FlowPanel::run_all_button() const
{
    return m_runAllButton;
}

void FlowPanel::populateTable()
{
    clearTable();
    if (!m_engine) {
        return;
    }

    const auto& steps = m_engine->steps();
    m_table->setRowCount(static_cast<int>(steps.size()));

    for (int row = 0; row < static_cast<int>(steps.size()); ++row) {
        const auto& step = steps[row];
        QString stepId = QString::fromStdString(step.id);

        // Step name (with 1-based index)
        auto* nameItem = new QTableWidgetItem(
            QString("%1. %2").arg(row + 1).arg(QString::fromStdString(step.name)));
        nameItem->setData(Qt::UserRole, stepId);
        m_table->setItem(row, COL_STEP, nameItem);

        // Status
        int statusInt = static_cast<int>(step.status);
        auto* statusItem = new QTableWidgetItem(textForStatus(statusInt));
        statusItem->setForeground(colorForStatus(statusInt));
        m_table->setItem(row, COL_STATUS, statusItem);

        // Time
        auto* timeItem = new QTableWidgetItem("--");
        timeItem->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(row, COL_TIME, timeItem);

        // Run button
        auto* runButton = new QPushButton(">");
        runButton->setFixedWidth(40);
        runButton->setToolTip("Run this step");
        runButton->setProperty("stepId", stepId);
        connect(runButton, &QPushButton::clicked,
                this, &FlowPanel::onRunButtonClicked);
        m_table->setCellWidget(row, COL_RUN, runButton);
    }
}

void FlowPanel::clearTable()
{
    m_table->setRowCount(0);
}

void FlowPanel::setRunningState(bool running)
{
    m_runAllButton->setEnabled(!running);

    for (int row = 0; row < m_table->rowCount(); ++row) {
        auto* button = qobject_cast<QPushButton*>(
            m_table->cellWidget(row, COL_RUN));
        if (button) {
            button->setEnabled(!running);
        }
    }
}

int FlowPanel::rowForStepId(const QString& id) const
{
    for (int row = 0; row < m_table->rowCount(); ++row) {
        auto* item = m_table->item(row, COL_STEP);
        if (item && item->data(Qt::UserRole).toString() == id) {
            return row;
        }
    }
    return -1;
}

QColor FlowPanel::colorForStatus(int status) const
{
    switch (static_cast<StepStatus>(status)) {
    case StepStatus::Pending:  return QColor("#808080");
    case StepStatus::Running:  return QColor("#3daee9");
    case StepStatus::Success:  return QColor("#27ae60");
    case StepStatus::Error:    return QColor("#e74c3c");
    case StepStatus::Skipped:  return QColor("#f39c12");
    }
    return QColor("#808080");
}

QString FlowPanel::textForStatus(int status) const
{
    switch (static_cast<StepStatus>(status)) {
    case StepStatus::Pending:  return "Pending";
    case StepStatus::Running:  return "Running";
    case StepStatus::Success:  return "Success";
    case StepStatus::Error:    return "Error";
    case StepStatus::Skipped:  return "Skipped";
    }
    return "Pending";
}

QString FlowPanel::formatElapsed(qint64 ms) const
{
    if (ms < 1000) {
        return QString("%1ms").arg(ms);
    }
    double secs = ms / 1000.0;
    if (secs < 60.0) {
        return QString("%1s").arg(secs, 0, 'f', 1);
    }
    int minutes = static_cast<int>(secs) / 60;
    double remainSecs = secs - minutes * 60.0;
    return QString("%1m %2s").arg(minutes).arg(remainSecs, 0, 'f', 0);
}

// Slots: FlowEngine signal handlers

void FlowPanel::onStepStarted(const QString& id)
{
    int row = rowForStepId(id);
    if (row < 0) {
        return;
    }

    // Start timer
    m_timers[id].start();

    // Update status
    auto* statusItem = m_table->item(row, COL_STATUS);
    if (statusItem) {
        statusItem->setText("Running");
        statusItem->setForeground(colorForStatus(static_cast<int>(StepStatus::Running)));
    }

    // Disable this row's run button
    auto* button = qobject_cast<QPushButton*>(m_table->cellWidget(row, COL_RUN));
    if (button) {
        button->setEnabled(false);
    }

    // Update status label
    auto* nameItem = m_table->item(row, COL_STEP);
    if (nameItem) {
        m_statusLabel->setText(QString("Running: %1").arg(
            QString::fromStdString(m_engine->step(id.toStdString())->name)));
        m_statusLabel->setStyleSheet("color: #3daee9;");
    }
}

void FlowPanel::onStepFinished(const QString& id, bool success)
{
    int row = rowForStepId(id);
    if (row < 0) {
        return;
    }

    // Read elapsed time
    auto it = m_timers.find(id);
    if (it != m_timers.end()) {
        m_elapsedMs[id] = it->second.elapsed();
        m_timers.erase(it);
    }

    // Update status
    StepStatus st = success ? StepStatus::Success : StepStatus::Error;
    auto* statusItem = m_table->item(row, COL_STATUS);
    if (statusItem) {
        statusItem->setText(textForStatus(static_cast<int>(st)));
        statusItem->setForeground(colorForStatus(static_cast<int>(st)));
    }

    // Update time
    auto* timeItem = m_table->item(row, COL_TIME);
    if (timeItem) {
        auto elapsed = m_elapsedMs.find(id);
        if (elapsed != m_elapsedMs.end()) {
            timeItem->setText(formatElapsed(elapsed->second));
        }
    }

    // Re-enable run button
    auto* button = qobject_cast<QPushButton*>(m_table->cellWidget(row, COL_RUN));
    if (button) {
        button->setEnabled(true);
    }

    // Update log if this step is selected
    if (id == m_selectedStepId) {
        const FlowStep* step = m_engine->step(id.toStdString());
        if (step) {
            m_logDisplay->setPlainText(QString::fromStdString(step->log));
            m_logDisplay->verticalScrollBar()->setValue(
                m_logDisplay->verticalScrollBar()->maximum());
        }
    }
}

void FlowPanel::onStepOutput(const QString& id, const QString& text)
{
    if (id != m_selectedStepId) {
        return;
    }

    m_logDisplay->appendPlainText(text);
    m_logDisplay->verticalScrollBar()->setValue(
        m_logDisplay->verticalScrollBar()->maximum());
}

void FlowPanel::onFlowFinished(bool allSuccess)
{
    if (allSuccess) {
        m_statusLabel->setText("Complete");
        m_statusLabel->setStyleSheet("color: #27ae60;");
    } else {
        m_statusLabel->setText("Failed");
        m_statusLabel->setStyleSheet("color: #e74c3c;");
    }

    // Update any skipped steps in the table
    if (m_engine) {
        for (const auto& step : m_engine->steps()) {
            if (step.status == StepStatus::Skipped) {
                int row = rowForStepId(QString::fromStdString(step.id));
                if (row >= 0) {
                    auto* statusItem = m_table->item(row, COL_STATUS);
                    if (statusItem) {
                        statusItem->setText("Skipped");
                        statusItem->setForeground(
                            colorForStatus(static_cast<int>(StepStatus::Skipped)));
                    }
                }
            }
        }
    }

    setRunningState(false);
}

// Slots: UI handlers

void FlowPanel::onTableCellClicked(int row, int /*column*/)
{
    auto* item = m_table->item(row, COL_STEP);
    if (!item) {
        return;
    }

    m_selectedStepId = item->data(Qt::UserRole).toString();

    // Load existing log from engine
    m_logDisplay->clear();
    if (m_engine) {
        const FlowStep* step = m_engine->step(m_selectedStepId.toStdString());
        if (step && !step->log.empty()) {
            m_logDisplay->setPlainText(QString::fromStdString(step->log));
            m_logDisplay->verticalScrollBar()->setValue(
                m_logDisplay->verticalScrollBar()->maximum());
        }
    }
}

void FlowPanel::onRunButtonClicked()
{
    auto* button = qobject_cast<QPushButton*>(sender());
    if (!button) {
        return;
    }

    QString stepId = button->property("stepId").toString();
    if (!stepId.isEmpty()) {
        setRunningState(true);
        emit stepRunRequested(stepId);
    }
}

void FlowPanel::onRunAllClicked()
{
    m_timers.clear();
    m_elapsedMs.clear();

    // Reset all rows to Pending visual state
    for (int row = 0; row < m_table->rowCount(); ++row) {
        auto* statusItem = m_table->item(row, COL_STATUS);
        if (statusItem) {
            statusItem->setText("Pending");
            statusItem->setForeground(colorForStatus(static_cast<int>(StepStatus::Pending)));
        }
        auto* timeItem = m_table->item(row, COL_TIME);
        if (timeItem) {
            timeItem->setText("--");
        }
    }

    setRunningState(true);
    emit runAllRequested();
}

} // namespace chiplet
