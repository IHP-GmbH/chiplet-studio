// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * FlowPanel.h - Flow pipeline status and control panel
 *
 * Displays flow steps in a table with status indicators, elapsed time,
 * per-step run buttons, and a log area for selected step output.
 * Connects to FlowEngine signals for live updates.
 */

#ifndef CHIPLET_UI_FLOWPANEL_H
#define CHIPLET_UI_FLOWPANEL_H

#include <QWidget>
#include <QString>
#include <QElapsedTimer>
#include <map>

class QTableWidget;
class QPlainTextEdit;
class QSplitter;
class QPushButton;
class QLabel;
class QStackedWidget;

namespace chiplet {

class FlowEngine;

class FlowPanel : public QWidget {
    Q_OBJECT

public:
    explicit FlowPanel(QWidget* parent = nullptr);
    ~FlowPanel() override;

    /**
     * Set the flow engine to visualize and control.
     * Pass nullptr to disconnect and show empty state.
     * FlowPanel does NOT take ownership -- caller must ensure engine outlives panel.
     */
    void set_flow_engine(FlowEngine* engine);

    FlowEngine* flow_engine() const;

    // Test accessors
    QTableWidget* table() const;
    QPlainTextEdit* log_display() const;
    QStackedWidget* stack() const;
    QPushButton* run_all_button() const;

signals:
    void stepRunRequested(const QString& id);
    void runAllRequested();

private slots:
    // FlowEngine signal handlers
    void onStepStarted(const QString& id);
    void onStepFinished(const QString& id, bool success);
    void onStepOutput(const QString& id, const QString& text);
    void onFlowFinished(bool allSuccess);

    // UI handlers
    void onTableCellClicked(int row, int column);
    void onRunButtonClicked();
    void onRunAllClicked();

private:
    void setupUI();
    void populateTable();
    void clearTable();
    void setRunningState(bool running);
    int rowForStepId(const QString& id) const;
    QColor colorForStatus(int status) const;
    QString textForStatus(int status) const;
    QString formatElapsed(qint64 ms) const;

    // Widgets
    QStackedWidget* m_stack = nullptr;
    QLabel* m_emptyLabel = nullptr;
    QTableWidget* m_table = nullptr;
    QPlainTextEdit* m_logDisplay = nullptr;
    QSplitter* m_splitter = nullptr;
    QPushButton* m_runAllButton = nullptr;
    QLabel* m_statusLabel = nullptr;

    // State
    FlowEngine* m_engine = nullptr;
    QString m_selectedStepId;
    std::map<QString, QElapsedTimer> m_timers;
    std::map<QString, qint64> m_elapsedMs;

    // Column indices
    static constexpr int COL_STEP = 0;
    static constexpr int COL_STATUS = 1;
    static constexpr int COL_TIME = 2;
    static constexpr int COL_RUN = 3;
    static constexpr int NUM_COLUMNS = 4;
};

} // namespace chiplet

#endif // CHIPLET_UI_FLOWPANEL_H
