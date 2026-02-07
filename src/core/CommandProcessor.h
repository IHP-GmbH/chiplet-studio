/**
 * CommandProcessor.h - Manages command execution and undo/redo stacks
 *
 * Central manager for all state-modifying operations. Provides:
 * - Command execution with automatic undo stack management
 * - Undo/Redo operations
 * - Signals for UI updates
 * - Optional journaling integration for crash recovery
 */

#ifndef CHIPLET_CORE_COMMAND_PROCESSOR_H
#define CHIPLET_CORE_COMMAND_PROCESSOR_H

#include <QObject>
#include <QString>
#include <vector>
#include <memory>
#include "Command.h"

namespace chiplet {

class Assembly;
class CommandJournal;

/**
 * CommandProcessor manages command execution and undo/redo stacks.
 *
 * Usage:
 *   CommandProcessor processor(&assembly);
 *   processor.execute(std::make_unique<CmdMoveComponent>(...));
 *   processor.undo();
 *   processor.redo();
 */
class CommandProcessor : public QObject {
    Q_OBJECT

public:
    /**
     * Constructor.
     * @param assembly The assembly to operate on (must outlive processor)
     * @param parent Optional Qt parent
     */
    explicit CommandProcessor(Assembly* assembly, QObject* parent = nullptr);

    /**
     * Destructor.
     */
    ~CommandProcessor() override;

    /**
     * Execute a command and add it to the undo stack.
     * Clears the redo stack on successful execution.
     * @param cmd The command to execute (ownership transferred)
     * @return true if execution succeeded
     */
    bool execute(CommandPtr cmd);

    /**
     * Check if undo is available.
     * @return true if there are commands to undo
     */
    bool can_undo() const;

    /**
     * Check if redo is available.
     * @return true if there are commands to redo
     */
    bool can_redo() const;

    /**
     * Undo the last command.
     * Moves the command to the redo stack.
     */
    void undo();

    /**
     * Redo the last undone command.
     * Moves the command back to the undo stack.
     */
    void redo();

    /**
     * Get the description of the next command to undo.
     * @return Description string, or empty if no undo available
     */
    QString undo_description() const;

    /**
     * Get the description of the next command to redo.
     * @return Description string, or empty if no redo available
     */
    QString redo_description() const;

    /**
     * Get the current undo stack size.
     * @return Number of commands in undo stack
     */
    size_t undo_stack_size() const;

    /**
     * Clear all undo/redo history.
     * Call this when loading a new file.
     */
    void clear();

    /**
     * Set the journal for crash recovery.
     * @param journal The journal to use (may be nullptr to disable)
     */
    void set_journal(CommandJournal* journal);

    /**
     * Get the current assembly.
     * @return Pointer to the assembly being modified
     */
    Assembly* assembly() const { return m_assembly; }

    /**
     * Maximum number of commands to keep in undo history.
     */
    static constexpr size_t MAX_UNDO_DEPTH = 100;

signals:
    /**
     * Emitted after a command is executed.
     * @param description Human-readable command description
     */
    void command_executed(const QString& description);

    /**
     * Emitted when the undo/redo stack state changes.
     */
    void stack_changed();

    /**
     * Emitted when canUndo() state changes.
     * @param can_undo New canUndo() state
     */
    void can_undo_changed(bool can_undo);

    /**
     * Emitted when canRedo() state changes.
     * @param can_redo New canRedo() state
     */
    void can_redo_changed(bool can_redo);

private:
    Assembly* m_assembly;
    CommandJournal* m_journal = nullptr;
    std::vector<CommandPtr> m_undoStack;
    std::vector<CommandPtr> m_redoStack;

    void emit_stack_signals();
    void trim_undo_stack();
};

} // namespace chiplet

#endif // CHIPLET_CORE_COMMAND_PROCESSOR_H
