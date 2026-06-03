// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * CommandJournal.h - Persistent journal for crash recovery
 *
 * Records executed commands to disk in JSON Lines format.
 * On crash, the journal can be replayed to restore the session.
 */

#ifndef CHIPLET_CORE_COMMAND_JOURNAL_H
#define CHIPLET_CORE_COMMAND_JOURNAL_H

#include <string>
#include <filesystem>
#include <fstream>
#include <vector>
#include <functional>
#include <map>
#include "Command.h"

namespace chiplet {

/**
 * CommandJournal records commands to disk for crash recovery.
 *
 * Usage:
 *   CommandJournal journal(path);
 *   journal.record(command);  // After successful execute
 *   // On clean exit:
 *   journal.discard();  // Delete the journal file
 *   // On crash recovery:
 *   auto commands = CommandJournal::replay(path, factory);
 */
class CommandJournal {
public:
    /**
     * Factory function type for deserializing commands.
     */
    using CommandFactory = std::function<CommandPtr(const std::string& type, const nlohmann::json& data)>;

    /**
     * Constructor.
     * @param journal_path Path to the journal file
     */
    explicit CommandJournal(const std::filesystem::path& journal_path);

    /**
     * Destructor. Flushes but does not discard the journal.
     */
    ~CommandJournal();

    // Non-copyable
    CommandJournal(const CommandJournal&) = delete;
    CommandJournal& operator=(const CommandJournal&) = delete;

    /**
     * Record a command to the journal.
     * Called after successful command execution.
     * @param cmd The command to record
     */
    void record(const Command& cmd);

    /**
     * Flush buffered writes to disk.
     */
    void flush();

    /**
     * Close the journal file.
     */
    void close();

    /**
     * Delete the journal file.
     * Call this on clean application exit.
     */
    void discard();

    /**
     * Get the journal file path.
     * @return Path to the journal file
     */
    const std::filesystem::path& path() const { return m_path; }

    /**
     * Check if an orphaned journal exists.
     * An orphaned journal indicates a previous crash.
     * @param directory Directory to check
     * @param journal_name Journal filename (default: ".session.journal")
     * @return true if an orphaned journal exists
     */
    static bool has_orphaned_journal(const std::filesystem::path& directory,
                                     const std::string& journal_name = ".session.journal");

    /**
     * Find the path to an orphaned journal.
     * @param directory Directory to check
     * @param journal_name Journal filename (default: ".session.journal")
     * @return Path to journal, or empty if none exists
     */
    static std::filesystem::path find_orphaned_journal(
        const std::filesystem::path& directory,
        const std::string& journal_name = ".session.journal");

    /**
     * Replay commands from a journal file.
     * @param journal_path Path to the journal file
     * @param factory Factory function to deserialize commands
     * @return Vector of commands in execution order
     */
    static std::vector<CommandPtr> replay(const std::filesystem::path& journal_path,
                                          const CommandFactory& factory);

    /**
     * Default journal filename.
     */
    static constexpr const char* DEFAULT_JOURNAL_NAME = ".session.journal";

private:
    std::filesystem::path m_path;
    std::ofstream m_file;
    bool m_open = false;

    bool ensure_open();
};

} // namespace chiplet

#endif // CHIPLET_CORE_COMMAND_JOURNAL_H
