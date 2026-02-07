/**
 * CrashRecoveryDialog.cpp - Implementation
 */

#include "CrashRecoveryDialog.h"
#include "core/Assembly.h"
#include "core/CommandJournal.h"
#include "core/CommandFactory.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTextEdit>
#include <QPushButton>
#include <QMessageBox>
#include <QStyle>
#include <QApplication>
#include <QDateTime>
#include <QFileInfo>

namespace chiplet {

CrashRecoveryDialog::Result CrashRecoveryDialog::check_and_recover(
    const std::filesystem::path& directory,
    Assembly* assembly)
{
    // Check for orphaned journal
    if (!CommandJournal::has_orphaned_journal(directory)) {
        return Result::NoJournalFound;
    }

    auto journalPath = CommandJournal::find_orphaned_journal(directory);
    if (journalPath.empty()) {
        return Result::NoJournalFound;
    }

    // Show dialog
    CrashRecoveryDialog dialog(journalPath);
    dialog.exec();

    if (dialog.result() == Result::Recovered && assembly) {
        // Replay commands into the assembly
        for (auto& cmd : dialog.recovered_commands()) {
            if (cmd) {
                cmd->execute(*assembly);
            }
        }
    }

    return dialog.result();
}

CrashRecoveryDialog::CrashRecoveryDialog(const std::filesystem::path& journal_path,
                                         QWidget* parent)
    : QDialog(parent)
    , m_journalPath(journal_path)
{
    setWindowTitle("Crash Recovery");
    setMinimumWidth(450);
    setupUI();
    loadJournalInfo();
}

CrashRecoveryDialog::~CrashRecoveryDialog() = default;

void CrashRecoveryDialog::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);

    // Warning icon and info text
    QHBoxLayout* headerLayout = new QHBoxLayout();
    QLabel* iconLabel = new QLabel();
    iconLabel->setPixmap(QApplication::style()->standardIcon(
        QStyle::SP_MessageBoxWarning).pixmap(48, 48));
    headerLayout->addWidget(iconLabel);

    QVBoxLayout* textLayout = new QVBoxLayout();
    m_infoLabel = new QLabel(
        "It appears the application was not shut down properly.\n"
        "A recovery journal was found that can restore your previous session.");
    m_infoLabel->setWordWrap(true);
    textLayout->addWidget(m_infoLabel);

    m_commandCountLabel = new QLabel("Loading...");
    m_commandCountLabel->setStyleSheet("color: #666;");
    textLayout->addWidget(m_commandCountLabel);

    headerLayout->addLayout(textLayout);
    mainLayout->addLayout(headerLayout);

    // Details text (hidden by default)
    m_detailsText = new QTextEdit();
    m_detailsText->setReadOnly(true);
    m_detailsText->setMaximumHeight(200);
    m_detailsText->setVisible(false);
    mainLayout->addWidget(m_detailsText);

    // Buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();

    m_detailsButton = new QPushButton("View Details");
    m_detailsButton->setCheckable(true);
    connect(m_detailsButton, &QPushButton::toggled,
            m_detailsText, &QTextEdit::setVisible);
    buttonLayout->addWidget(m_detailsButton);

    buttonLayout->addStretch();

    m_discardButton = new QPushButton("Discard");
    m_discardButton->setToolTip("Delete the recovery journal and start fresh");
    connect(m_discardButton, &QPushButton::clicked,
            this, &CrashRecoveryDialog::onDiscard);
    buttonLayout->addWidget(m_discardButton);

    m_recoverButton = new QPushButton("Recover");
    m_recoverButton->setDefault(true);
    m_recoverButton->setToolTip("Replay the recovery journal to restore your session");
    connect(m_recoverButton, &QPushButton::clicked,
            this, &CrashRecoveryDialog::onRecover);
    buttonLayout->addWidget(m_recoverButton);

    mainLayout->addLayout(buttonLayout);
}

void CrashRecoveryDialog::loadJournalInfo()
{
    try {
        // Get file info
        QFileInfo fi(QString::fromStdString(m_journalPath.string()));
        QString dateStr = fi.lastModified().toString("yyyy-MM-dd hh:mm:ss");

        // Load and parse commands
        m_commands = CommandJournal::replay(
            m_journalPath,
            CommandFactory::get_journal_factory());

        m_commandCountLabel->setText(
            QString("Found %1 command(s) from %2")
                .arg(m_commands.size())
                .arg(dateStr));

        // Build details text
        QString details;
        details += QString("Journal file: %1\n")
            .arg(QString::fromStdString(m_journalPath.string()));
        details += QString("Last modified: %1\n").arg(dateStr);
        details += QString("Commands found: %1\n\n").arg(m_commands.size());

        details += "Commands:\n";
        details += "─────────────────────────────\n";

        for (size_t i = 0; i < m_commands.size(); ++i) {
            if (m_commands[i]) {
                details += QString("%1. [%2] %3\n")
                    .arg(i + 1)
                    .arg(QString::fromStdString(m_commands[i]->type()))
                    .arg(QString::fromStdString(m_commands[i]->description()));
            }
        }

        m_detailsText->setPlainText(details);

    } catch (const std::exception& e) {
        m_commandCountLabel->setText(
            QString("Error reading journal: %1").arg(e.what()));
        m_recoverButton->setEnabled(false);
    }
}

void CrashRecoveryDialog::onRecover()
{
    m_result = Result::Recovered;
    accept();
}

void CrashRecoveryDialog::onDiscard()
{
    // Confirm discard
    int ret = QMessageBox::question(
        this,
        "Confirm Discard",
        "Are you sure you want to discard the recovery journal?\n"
        "Your previous work will be lost.",
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (ret == QMessageBox::Yes) {
        // Delete the journal file
        try {
            std::filesystem::remove(m_journalPath);
        } catch (...) {
            // Ignore errors during deletion
        }
        m_result = Result::Discarded;
        accept();
    }
}

void CrashRecoveryDialog::onViewDetails()
{
    m_detailsText->setVisible(!m_detailsText->isVisible());
    m_detailsButton->setText(m_detailsText->isVisible() ? "Hide Details" : "View Details");
}

} // namespace chiplet
