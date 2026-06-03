// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * CommandProcessor.cpp - Implementation
 */

#include "CommandProcessor.h"
#include "CommandJournal.h"
#include "Assembly.h"

#include <QDebug>

namespace chiplet {

CommandProcessor::CommandProcessor(Assembly* assembly, QObject* parent)
    : QObject(parent)
    , m_assembly(assembly)
{
}

CommandProcessor::~CommandProcessor() = default;

bool CommandProcessor::execute(CommandPtr cmd)
{
    if (!cmd || !m_assembly) {
        return false;
    }

    // Try to merge with previous command (for coalescing rapid updates)
    if (!m_undoStack.empty() && m_undoStack.back()->can_merge_with(*cmd)) {
        m_undoStack.back()->merge_with(*cmd);
        emit command_executed(QString::fromStdString(m_undoStack.back()->description()));
        return true;
    }

    // Execute the command
    bool success = false;
    try {
        success = cmd->execute(*m_assembly);
    } catch (const std::exception& e) {
        qWarning("Command execution failed: %s", e.what());
        return false;
    } catch (...) {
        qWarning("Command execution failed with unknown error");
        return false;
    }

    if (!success) {
        return false;
    }

    // Record to journal if available
    if (m_journal) {
        m_journal->record(*cmd);
    }

    // Add to undo stack
    QString description = QString::fromStdString(cmd->description());
    m_undoStack.push_back(std::move(cmd));

    // Clear redo stack (branching history model)
    m_redoStack.clear();

    // Trim undo stack if too large
    trim_undo_stack();

    // Emit signals
    emit command_executed(description);
    emit_stack_signals();

    return true;
}

bool CommandProcessor::can_undo() const
{
    return !m_undoStack.empty();
}

bool CommandProcessor::can_redo() const
{
    return !m_redoStack.empty();
}

void CommandProcessor::undo()
{
    if (!can_undo() || !m_assembly) {
        return;
    }

    // Pop from undo stack
    CommandPtr cmd = std::move(m_undoStack.back());
    m_undoStack.pop_back();

    // Execute undo
    try {
        cmd->undo(*m_assembly);
    } catch (const std::exception& e) {
        qWarning("Command undo failed: %s", e.what());
        // Still push to redo stack for potential retry
    } catch (...) {
        qWarning("Command undo failed with unknown error");
    }

    // Push to redo stack
    m_redoStack.push_back(std::move(cmd));

    emit_stack_signals();
}

void CommandProcessor::redo()
{
    if (!can_redo() || !m_assembly) {
        return;
    }

    // Pop from redo stack
    CommandPtr cmd = std::move(m_redoStack.back());
    m_redoStack.pop_back();

    // Execute redo
    try {
        cmd->redo(*m_assembly);
    } catch (const std::exception& e) {
        qWarning("Command redo failed: %s", e.what());
        // Still push to undo stack for potential retry
    } catch (...) {
        qWarning("Command redo failed with unknown error");
    }

    // Push to undo stack
    m_undoStack.push_back(std::move(cmd));

    emit_stack_signals();
}

QString CommandProcessor::undo_description() const
{
    if (m_undoStack.empty()) {
        return QString();
    }
    return QString::fromStdString(m_undoStack.back()->description());
}

QString CommandProcessor::redo_description() const
{
    if (m_redoStack.empty()) {
        return QString();
    }
    return QString::fromStdString(m_redoStack.back()->description());
}

size_t CommandProcessor::undo_stack_size() const
{
    return m_undoStack.size();
}

void CommandProcessor::clear()
{
    bool hadUndo = can_undo();
    bool hadRedo = can_redo();

    m_undoStack.clear();
    m_redoStack.clear();

    if (hadUndo) {
        emit can_undo_changed(false);
    }
    if (hadRedo) {
        emit can_redo_changed(false);
    }
    emit stack_changed();
}

void CommandProcessor::set_journal(CommandJournal* journal)
{
    m_journal = journal;
}

void CommandProcessor::emit_stack_signals()
{
    static bool s_lastCanUndo = false;
    static bool s_lastCanRedo = false;

    bool currentCanUndo = can_undo();
    bool currentCanRedo = can_redo();

    if (currentCanUndo != s_lastCanUndo) {
        s_lastCanUndo = currentCanUndo;
        emit can_undo_changed(currentCanUndo);
    }

    if (currentCanRedo != s_lastCanRedo) {
        s_lastCanRedo = currentCanRedo;
        emit can_redo_changed(currentCanRedo);
    }

    emit stack_changed();
}

void CommandProcessor::trim_undo_stack()
{
    while (m_undoStack.size() > MAX_UNDO_DEPTH) {
        m_undoStack.erase(m_undoStack.begin());
    }
}

} // namespace chiplet
