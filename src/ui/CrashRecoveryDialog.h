// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CHIPLET_UI_CRASH_RECOVERY_DIALOG_H
#define CHIPLET_UI_CRASH_RECOVERY_DIALOG_H
#include <QDialog>
#include <filesystem>
#include <memory>
#include <vector>
class QLabel;
class QListWidget;
class QPushButton;
namespace chiplet {
class Assembly;
// Complete current-state recovery, never baseline-less command replay.
// This dialog neither removes a checkpoint nor adopts it for future writes.
class CrashRecoveryDialog : public QDialog {
    Q_OBJECT
public:
    static std::unique_ptr<Assembly> check_and_recover(const std::filesystem::path& directory,
                                                     bool include_closed = false,
                                                     const std::filesystem::path& exclude_current = {});
    explicit CrashRecoveryDialog(const std::vector<std::filesystem::path>& checkpoints,
                                 bool legacy_journal = false, QWidget* parent = nullptr);
    ~CrashRecoveryDialog() override;
    std::unique_ptr<Assembly> take_recovered_assembly();
private slots:
    void onRecover();
    void onTrash();
private:
    std::vector<std::filesystem::path> m_checkpoints;
    std::unique_ptr<Assembly> m_recovered;
    QListWidget* m_list = nullptr;
    QLabel* m_problem = nullptr;
    QPushButton* m_recoverButton = nullptr;
    QPushButton* m_trashButton = nullptr;
};
}
#endif
