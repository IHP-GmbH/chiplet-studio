// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * CrashRecoveryDialog.h - Dialog for crash recovery options
 *
 * Shown on startup when an orphaned journal file is detected,
 * indicating a previous crash. Offers options to recover, discard,
 * or view journal contents.
 */

#ifndef CHIPLET_UI_CRASH_RECOVERY_DIALOG_H
#define CHIPLET_UI_CRASH_RECOVERY_DIALOG_H

#include <QDialog>
#include <filesystem>
#include <vector>
#include "core/Command.h"

class QLabel;
class QTextEdit;
class QPushButton;

namespace chiplet {

class Assembly;

/**
 * CrashRecoveryDialog presents recovery options after a crash.
 *
 * Usage:
 *   if (CrashRecoveryDialog::check_and_recover(directory, assembly)
 *           == CrashRecoveryDialog::Result::Recovered) {
 *       // User chose to recover, assembly has been modified
 *   }
 */
class CrashRecoveryDialog : public QDialog {
    Q_OBJECT

public:
    /**
     * Result of the recovery check.
     */
    enum class Result {
        NoJournalFound,   ///< No orphaned journal exists
        Recovered,        ///< User chose to recover
        Discarded,        ///< User chose to discard
        Cancelled         ///< User cancelled the dialog
    };

    /**
     * Check for orphaned journal and show dialog if found.
     * @param directory Directory to check for journal
     * @param assembly Assembly to replay commands into
     * @return Result indicating what happened
     */
    static Result check_and_recover(const std::filesystem::path& directory,
                                    Assembly* assembly);

    /**
     * Constructor.
     * @param journal_path Path to the orphaned journal file
     * @param parent Parent widget
     */
    explicit CrashRecoveryDialog(const std::filesystem::path& journal_path,
                                 QWidget* parent = nullptr);
    ~CrashRecoveryDialog() override;

    /**
     * Get the result after dialog is closed.
     * @return User's choice
     */
    Result result() const { return m_result; }

    /**
     * Get the recovered commands.
     * Only valid after dialog is accepted and result is Recovered.
     * @return Vector of recovered commands
     */
    std::vector<CommandPtr>& recovered_commands() { return m_commands; }

private slots:
    void onRecover();
    void onDiscard();

private:
    void setupUI();
    void loadJournalInfo();

    std::filesystem::path m_journalPath;
    std::vector<CommandPtr> m_commands;
    Result m_result = Result::Cancelled;

    // UI elements
    QLabel* m_infoLabel = nullptr;
    QLabel* m_commandCountLabel = nullptr;
    QTextEdit* m_detailsText = nullptr;
    QPushButton* m_recoverButton = nullptr;
    QPushButton* m_discardButton = nullptr;
    QPushButton* m_detailsButton = nullptr;
};

} // namespace chiplet

#endif // CHIPLET_UI_CRASH_RECOVERY_DIALOG_H
