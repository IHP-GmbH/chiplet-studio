/**
 * ScriptConsole.cpp - Interactive Python console widget implementation
 */

#include "ScriptConsole.h"
#include "scripting/ScriptEngine.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFont>
#include <QKeyEvent>
#include <QScrollBar>
#include <QFileInfo>
#include <QTextCharFormat>

namespace chiplet {

ScriptConsole::ScriptConsole(QWidget* parent)
    : QWidget(parent)
    , m_outputDisplay(nullptr)
    , m_inputLine(nullptr)
    , m_engine(nullptr)
    , m_historyIndex(-1)
    , m_waitingForMore(false)
{
    setup_ui();
}

ScriptConsole::~ScriptConsole() = default;

void ScriptConsole::setup_ui()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);

    // Output display area
    m_outputDisplay = new QPlainTextEdit(this);
    m_outputDisplay->setReadOnly(true);
    m_outputDisplay->setLineWrapMode(QPlainTextEdit::NoWrap);

    // Use monospace font
    QFont monoFont("Monospace", 10);
    monoFont.setStyleHint(QFont::TypeWriter);
    m_outputDisplay->setFont(monoFont);

    // Set dark background for output
    m_outputDisplay->setStyleSheet(
        "QPlainTextEdit { background-color: #1e1e1e; color: #d4d4d4; }"
    );

    layout->addWidget(m_outputDisplay, 1);

    // Input area
    auto* inputLayout = new QHBoxLayout();
    inputLayout->setSpacing(4);

    auto* promptLabel = new QLabel(">>>", this);
    promptLabel->setFont(monoFont);
    promptLabel->setStyleSheet("QLabel { color: #569cd6; }");
    inputLayout->addWidget(promptLabel);

    m_inputLine = new QLineEdit(this);
    m_inputLine->setFont(monoFont);
    m_inputLine->setStyleSheet(
        "QLineEdit { background-color: #252526; color: #d4d4d4; border: 1px solid #3c3c3c; }"
    );
    m_inputLine->setPlaceholderText("Enter Python command...");
    m_inputLine->installEventFilter(this);

    connect(m_inputLine, &QLineEdit::returnPressed,
            this, &ScriptConsole::on_input_return_pressed);

    inputLayout->addWidget(m_inputLine, 1);

    layout->addLayout(inputLayout);

    // Welcome message
    if (ScriptEngine::is_available()) {
        append_output("Python Console - Chiplet Studio", QColor("#569cd6"));
        append_output("Type Python commands below. Use up/down arrows for history.", QColor("#808080"));
        append_output("", QColor("#d4d4d4"));
    } else {
        append_output("Python scripting not available", QColor("#ff6b6b"));
        append_output("Build with -DENABLE_PYTHON_SCRIPTING=ON to enable.", QColor("#808080"));
        m_inputLine->setEnabled(false);
    }
}

void ScriptConsole::set_engine(ScriptEngine* engine)
{
    // Disconnect from old engine
    if (m_engine) {
        disconnect(m_engine, nullptr, this, nullptr);
    }

    m_engine = engine;

    // Connect to new engine
    if (m_engine) {
        connect(m_engine, &ScriptEngine::output,
                this, &ScriptConsole::on_engine_output);
        connect(m_engine, &ScriptEngine::error_output,
                this, &ScriptConsole::on_engine_error);
        connect(m_engine, &ScriptEngine::execution_finished,
                this, &ScriptConsole::on_execution_finished);
    }
}

void ScriptConsole::clear_output()
{
    m_outputDisplay->clear();
}

void ScriptConsole::print(const QString& text)
{
    append_output(text, QColor("#d4d4d4"));
}

void ScriptConsole::print_error(const QString& text)
{
    append_output(text, QColor("#ff6b6b"));
}

bool ScriptConsole::is_scripting_available() const
{
    return ScriptEngine::is_available();
}

void ScriptConsole::execute_command(const QString& command)
{
    if (!m_engine || command.trimmed().isEmpty()) {
        return;
    }

    // Add to history
    if (m_history.isEmpty() || m_history.last() != command) {
        m_history.append(command);
    }
    m_historyIndex = m_history.size();

    // Show command in output
    QString prompt = m_waitingForMore ? "... " : ">>> ";
    append_output(prompt + command, QColor("#569cd6"));

    // Execute
    bool executed = m_engine->execute_line(command);

    if (!executed) {
        // Need more input (multiline)
        m_waitingForMore = true;
    } else {
        m_waitingForMore = false;
    }

    emit command_submitted(command);
}

void ScriptConsole::run_script(const QString& path)
{
    if (!m_engine) {
        print_error("No script engine available");
        return;
    }

    QFileInfo info(path);
    if (!info.exists()) {
        print_error(QString("File not found: %1").arg(path));
        return;
    }

    print(QString("Running script: %1").arg(path));
    m_engine->execute_file(path);
}

bool ScriptConsole::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_inputLine && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);

        if (keyEvent->key() == Qt::Key_Up) {
            navigate_history(-1);
            return true;
        } else if (keyEvent->key() == Qt::Key_Down) {
            navigate_history(1);
            return true;
        }
    }

    return QWidget::eventFilter(obj, event);
}

void ScriptConsole::on_input_return_pressed()
{
    QString command = m_inputLine->text();
    m_inputLine->clear();
    execute_command(command);
}

void ScriptConsole::on_engine_output(const QString& text)
{
    append_output(text, QColor("#d4d4d4"));
}

void ScriptConsole::on_engine_error(const QString& text)
{
    append_output(text, QColor("#ff6b6b"));
}

void ScriptConsole::on_execution_finished(bool success)
{
    Q_UNUSED(success);
    // Scroll to bottom after execution completes
    auto* scrollBar = m_outputDisplay->verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());
}

void ScriptConsole::navigate_history(int direction)
{
    if (m_history.isEmpty()) {
        return;
    }

    // Save current input if at end of history
    if (m_historyIndex == m_history.size()) {
        m_currentInput = m_inputLine->text();
    }

    // Navigate
    m_historyIndex += direction;

    // Clamp to valid range
    if (m_historyIndex < 0) {
        m_historyIndex = 0;
    } else if (m_historyIndex > m_history.size()) {
        m_historyIndex = m_history.size();
    }

    // Update input
    if (m_historyIndex < m_history.size()) {
        m_inputLine->setText(m_history[m_historyIndex]);
    } else {
        m_inputLine->setText(m_currentInput);
    }

    // Move cursor to end
    m_inputLine->end(false);
}

void ScriptConsole::append_output(const QString& text, const QColor& color)
{
    QTextCharFormat format;
    format.setForeground(color);

    QTextCursor cursor = m_outputDisplay->textCursor();
    cursor.movePosition(QTextCursor::End);

    cursor.insertText(text + "\n", format);

    // Scroll to bottom
    auto* scrollBar = m_outputDisplay->verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());
}

} // namespace chiplet
