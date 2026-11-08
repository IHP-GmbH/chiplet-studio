// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later
#include "CrashRecoveryDialog.h"
#include "core/Assembly.h"
#include "core/CommandJournal.h"
#include "core/RecoveryCheckpoint.h"
#include "formats/ChipletFormat.h"
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QMessageBox>
#include <algorithm>
namespace chiplet {
std::unique_ptr<Assembly> CrashRecoveryDialog::check_and_recover(
    const std::filesystem::path& directory, bool include_closed,
    const std::filesystem::path& exclude_current)
{
    const auto recovery_directory = directory / "recovery";
    auto checkpoints = std::filesystem::exists(recovery_directory)
        ? RecoveryCheckpoint::find(recovery_directory)
        : std::vector<std::filesystem::path>{};
    if (include_closed && std::filesystem::exists(recovery_directory / "closed")) {
        const auto closed = RecoveryCheckpoint::find(recovery_directory / "closed");
        checkpoints.insert(checkpoints.end(), closed.begin(), closed.end());
    }
    // Recovering the current window's own checkpoint would immediately update
    // and archive the selected input while retiring that same open document.
    // Other sessions and closed copies remain available and are never adopted.
    if (!exclude_current.empty()) {
        checkpoints.erase(std::remove_if(checkpoints.begin(), checkpoints.end(),
            [&](const auto& path) {
                std::error_code error;
                return path == exclude_current ||
                    std::filesystem::equivalent(path, exclude_current, error);
            }), checkpoints.end());
    }
    const bool legacy = CommandJournal::has_orphaned_journal(directory);
    if (checkpoints.empty() && !legacy && !include_closed) return {};
    CrashRecoveryDialog dialog(checkpoints, legacy);
    if (dialog.exec() != QDialog::Accepted) return {};
    return dialog.take_recovered_assembly();
}
CrashRecoveryDialog::CrashRecoveryDialog(
    const std::vector<std::filesystem::path>& checkpoints, bool legacy, QWidget* parent)
    : QDialog(parent), m_checkpoints(checkpoints)
{
    std::sort(m_checkpoints.begin(), m_checkpoints.end(), [](const auto& a, const auto& b) {
        const auto ta = QFileInfo(QString::fromStdString(a.string())).lastModified();
        const auto tb = QFileInfo(QString::fromStdString(b.string())).lastModified();
        return ta == tb ? a < b : ta > tb;
    });
    setWindowTitle(tr("Recovery checkpoints"));
    setMinimumSize(560, 340);
    auto* layout = new QVBoxLayout(this);
    auto* description = new QLabel(tr(
        "Select a complete checkpoint to open. It may belong to another running "
        "session; its existence does not prove a crash.\n\n"
        "Recovery restores the saved state and file-preservation information "
        "with empty undo/redo history. New edits get a separate recovery copy. "
        "Recovery leaves all existing files unchanged. Cleanly closed copies are "
        "archived and available through File > Recovery Checkpoints. They are kept "
        "until you explicitly move a selected copy to the system trash."), this);
    description->setWordWrap(true);
    layout->addWidget(description);
    if (legacy) {
        auto* warning = new QLabel(tr(
            "An older command journal was also found. It has no complete document "
            "baseline and does not record undo/redo, so it cannot safely restore "
            "the document. It has been left untouched."), this);
        warning->setObjectName("legacyRecoveryWarning");
        warning->setWordWrap(true);
        layout->addWidget(warning);
    }
    m_list = new QListWidget(this);
    m_list->setObjectName("recoveryCheckpointList");
    for (const auto& path : m_checkpoints) {
        const QFileInfo info(QString::fromStdString(path.string()));
        auto* item = new QListWidgetItem(
            info.lastModified().toString("yyyy-MM-dd hh:mm:ss") + "  " + info.fileName(),
            m_list);
        item->setToolTip(info.absoluteFilePath());
    }
    layout->addWidget(m_list);
    m_problem = new QLabel(this);
    m_problem->setObjectName("recoveryProblem");
    m_problem->setWordWrap(true);
    layout->addWidget(m_problem);
    auto* buttons = new QHBoxLayout();
    m_trashButton = new QPushButton(tr("Move selected to trash..."), this);
    m_trashButton->setObjectName("trashCheckpointButton");
    m_trashButton->setEnabled(false);
    connect(m_trashButton, &QPushButton::clicked, this, &CrashRecoveryDialog::onTrash);
    buttons->addWidget(m_trashButton);
    auto* keep = new QPushButton(tr("Keep files and continue"), this);
    keep->setDefault(true);
    connect(keep, &QPushButton::clicked, this, &QDialog::reject);
    buttons->addWidget(keep);
    buttons->addStretch();
    m_recoverButton = new QPushButton(tr("Open selected checkpoint"), this);
    m_recoverButton->setObjectName("recoverCheckpointButton");
    m_recoverButton->setEnabled(false);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        m_recoverButton->setEnabled(row >= 0);
        m_trashButton->setEnabled(row >= 0);
        m_problem->clear();
    });
    connect(m_recoverButton, &QPushButton::clicked, this, &CrashRecoveryDialog::onRecover);
    buttons->addWidget(m_recoverButton);
    layout->addLayout(buttons);
}
CrashRecoveryDialog::~CrashRecoveryDialog() = default;
std::unique_ptr<Assembly> CrashRecoveryDialog::take_recovered_assembly()
{
    return std::move(m_recovered);
}
void CrashRecoveryDialog::onRecover()
{
    const int row = m_list->currentRow();
    if (row < 0 || static_cast<size_t>(row) >= m_checkpoints.size()) return;
    try {
        // Re-read and validate at selection time, not from a stale preview.
        ChipletFormat format;
        m_recovered = format.decode_recovery(RecoveryCheckpoint::read(m_checkpoints[row]));
        accept();
    } catch (const std::exception& e) {
        m_recovered.reset();
        m_problem->setText(tr("This checkpoint cannot be opened: %1\n"
            "No recovery file was changed. Select another checkpoint or continue.")
            .arg(QString::fromUtf8(e.what())));
    }
}

void CrashRecoveryDialog::onTrash()
{
    const int row = m_list->currentRow();
    if (row < 0 || static_cast<size_t>(row) >= m_checkpoints.size()) return;
    const QString path = QString::fromStdString(m_checkpoints[row].string());
    const QString activeWarning = m_checkpoints[row].parent_path().filename() != "closed"
        ? tr("\nThis pending copy may belong to a running session. That session will "
             "need to write a new recovery copy on its next checkpoint.") : QString();
    if (QMessageBox::question(this, tr("Move checkpoint to trash?"),
            tr("Move only this recovery copy to the system trash?\n%1%2\n\n"
               "The document and other recovery copies are not changed. "
               "You can restore this copy from your system trash.").arg(path, activeWarning),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    if (!QFile::moveToTrash(path)) {
        m_problem->setText(tr("Could not move this checkpoint to trash. "
                             "There is no permanent-deletion fallback."));
        return;
    }
    m_checkpoints.erase(m_checkpoints.begin() + row);
    delete m_list->takeItem(row);
}
}
