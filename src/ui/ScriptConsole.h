// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * ScriptConsole.h - Interactive Python console widget
 *
 * Provides a REPL (Read-Eval-Print-Loop) interface for Python scripting.
 * Features:
 * - Command history navigation (up/down arrows)
 * - Multiline input support
 * - Output/error display with syntax highlighting
 * - Integration with ScriptEngine
 */

#ifndef CHIPLET_UI_SCRIPT_CONSOLE_H
#define CHIPLET_UI_SCRIPT_CONSOLE_H

#include <QWidget>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QStringList>
#include <memory>

namespace chiplet {

class ScriptEngine;

/**
 * ScriptConsole provides an interactive Python console in the GUI.
 *
 * The console displays output from Python scripts and allows users
 * to enter Python commands interactively. Command history is preserved
 * and can be navigated with up/down arrows.
 */
class ScriptConsole : public QWidget {
    Q_OBJECT

public:
    explicit ScriptConsole(QWidget* parent = nullptr);
    ~ScriptConsole() override;

    /**
     * Set the script engine to use for execution.
     * @param engine The ScriptEngine instance (must outlive console)
     */
    void set_engine(ScriptEngine* engine);

    /**
     * Clear all console output.
     */
    void clear_output();

    /**
     * Print text to the console output.
     * @param text Text to display
     */
    void print(const QString& text);

    /**
     * Print error text to the console (shown in red).
     * @param text Error text to display
     */
    void print_error(const QString& text);

    /**
     * Get whether Python scripting is available.
     */
    bool is_scripting_available() const;

public slots:
    /**
     * Execute a Python command string.
     * @param command Python code to execute
     */
    void execute_command(const QString& command);

    /**
     * Run a Python script file.
     * @param path Path to .py file
     */
    void run_script(const QString& path);

signals:
    /**
     * Emitted when the user submits a command.
     */
    void command_submitted(const QString& command);

protected:
    /**
     * Handle key events for history navigation.
     */
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void on_input_return_pressed();
    void on_engine_output(const QString& text);
    void on_engine_error(const QString& text);
    void on_execution_finished(bool success);

private:
    void setup_ui();
    void navigate_history(int direction);
    void append_output(const QString& text, const QColor& color);

    QPlainTextEdit* m_outputDisplay;
    QLineEdit* m_inputLine;
    ScriptEngine* m_engine;
    QStringList m_history;
    int m_historyIndex;
    QString m_currentInput;
    bool m_waitingForMore;  // True when waiting for multiline input
    QString m_multilineBuffer;
};

} // namespace chiplet

#endif // CHIPLET_UI_SCRIPT_CONSOLE_H
