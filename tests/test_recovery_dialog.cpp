// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 IHP GmbH
#include <gtest/gtest.h>
#include <QApplication>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTemporaryDir>
#include <QMessageBox>
#include <QTimer>
#include <chrono>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include "core/Assembly.h"
#include "core/RecoveryCheckpoint.h"
#include "formats/ChipletFormat.h"
#include "ui/CrashRecoveryDialog.h"

namespace chiplet {
namespace {
std::string readBytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
void writeBytes(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << bytes;
}
class RecoveryDialogTest : public ::testing::Test {
protected:
    QTemporaryDir temp;
    std::filesystem::path dir, source;
    ChipletFormat format;
    std::unique_ptr<Assembly> model;
    std::string original;
    void SetUp() override {
        ASSERT_NE(qobject_cast<QApplication*>(QCoreApplication::instance()), nullptr);
        ASSERT_TRUE(temp.isValid());
        dir = temp.path().toStdString(); source = dir / "source.chiplet";
        original = readBytes(std::filesystem::path(FIXTURES_DIR) / "minimal.chiplet");
        ASSERT_FALSE(original.empty());
        original += "\ninterposer:\n  adapter: intm4tm2\n_metadata:\n  marker: 'opaque value'\n";
        writeBytes(source, original);
        model = format.load(source.string());
        ASSERT_NE(model, nullptr);
        model->set_name("Unsaved recovered edit");
    }
    RecoveryCheckpoint::Handle checkpoint() {
        return RecoveryCheckpoint::create(dir, format.encode_recovery(*model));
    }
};

TEST_F(RecoveryDialogTest, ClickRestoresEditedModelAndOpaqueSourceWithoutChangingFiles) {
    auto handle = checkpoint();
    const auto before = readBytes(handle.path());
    CrashRecoveryDialog dialog({handle.path()});
    auto* list = dialog.findChild<QListWidget*>("recoveryCheckpointList");
    auto* button = dialog.findChild<QPushButton*>("recoverCheckpointButton");
    ASSERT_NE(list, nullptr); ASSERT_NE(button, nullptr);
    EXPECT_FALSE(button->isEnabled());
    list->setCurrentRow(0);
    EXPECT_TRUE(button->isEnabled());
    EXPECT_EQ(dialog.take_recovered_assembly(), nullptr);
    button->click();
    EXPECT_EQ(dialog.result(), QDialog::Accepted);
    auto recovered = dialog.take_recovered_assembly();
    ASSERT_NE(recovered, nullptr);
    EXPECT_EQ(recovered->name(), "Unsaved recovered edit");
    const auto& state = recovered->document_persistence();
    EXPECT_TRUE(state.capture_safe);
    EXPECT_EQ(state.source_bytes, original);
    EXPECT_EQ(state.source_path, source.string());
    ASSERT_TRUE(state.primary.has_value());
    EXPECT_EQ(state.primary->bytes(), original);
    EXPECT_EQ(state.primary->canonical_path(), std::filesystem::canonical(source).string());
    bool sawInterposer = false, sawMetadata = false;
    for (const auto& root : state.roots) {
        if (root.key == "interposer") { sawInterposer = true; EXPECT_EQ(root.raw, "interposer:\n  adapter: intm4tm2\n"); }
        if (root.key == "_metadata") { sawMetadata = true; EXPECT_EQ(root.raw, "_metadata:\n  marker: 'opaque value'\n"); }
    }
    EXPECT_TRUE(sawInterposer); EXPECT_TRUE(sawMetadata);
    EXPECT_EQ(readBytes(source), original);
    EXPECT_EQ(readBytes(handle.path()), before);
    EXPECT_EQ(RecoveryCheckpoint::find(dir), (std::vector<std::filesystem::path>{handle.path()}));
    EXPECT_EQ(dialog.take_recovered_assembly(), nullptr);
}

TEST_F(RecoveryDialogTest, ValidEnvelopeWithInvalidPayloadRefusesInProblemLabel) {
    auto handle = RecoveryCheckpoint::create(dir, "not a format recovery payload");
    const auto before = readBytes(handle.path());
    CrashRecoveryDialog dialog({handle.path()});
    auto* list = dialog.findChild<QListWidget*>("recoveryCheckpointList");
    auto* button = dialog.findChild<QPushButton*>("recoverCheckpointButton");
    auto* problem = dialog.findChild<QLabel*>("recoveryProblem");
    ASSERT_NE(list, nullptr); ASSERT_NE(button, nullptr); ASSERT_NE(problem, nullptr);
    list->setCurrentRow(0);
    EXPECT_TRUE(problem->text().isEmpty()); // Selection does not decode.
    button->click();
    EXPECT_NE(dialog.result(), QDialog::Accepted);
    EXPECT_EQ(dialog.take_recovered_assembly(), nullptr);
    EXPECT_TRUE(problem->text().contains("cannot be opened"));
    EXPECT_FALSE(problem->isHidden());
    EXPECT_EQ(readBytes(handle.path()), before);
    EXPECT_EQ(readBytes(source), original);
}

TEST_F(RecoveryDialogTest, ModifiedAfterSelectionIsRereadAndChecksumRefused) {
    auto handle = checkpoint();
    CrashRecoveryDialog dialog({handle.path()});
    auto* list = dialog.findChild<QListWidget*>("recoveryCheckpointList");
    auto* button = dialog.findChild<QPushButton*>("recoverCheckpointButton");
    auto* problem = dialog.findChild<QLabel*>("recoveryProblem");
    ASSERT_NE(list, nullptr); ASSERT_NE(button, nullptr); ASSERT_NE(problem, nullptr);
    list->setCurrentRow(0);
    auto corrupted = readBytes(handle.path()); ASSERT_FALSE(corrupted.empty());
    corrupted.back() ^= 1; writeBytes(handle.path(), corrupted);
    EXPECT_TRUE(problem->text().isEmpty());
    button->click();
    EXPECT_EQ(dialog.take_recovered_assembly(), nullptr);
    EXPECT_NE(dialog.result(), QDialog::Accepted);
    EXPECT_TRUE(problem->text().contains("checksum"));
    EXPECT_EQ(readBytes(handle.path()), corrupted);
    EXPECT_EQ(readBytes(source), original);
}

TEST_F(RecoveryDialogTest, LegacyOnlyHasNoRecoverActionAndKeepsJournal) {
    const auto legacy = dir / ".session.journal";
    const std::string bytes = "legacy commands without baseline\n";
    writeBytes(legacy, bytes);
    CrashRecoveryDialog dialog({}, true);
    auto* button = dialog.findChild<QPushButton*>("recoverCheckpointButton");
    auto* warning = dialog.findChild<QLabel*>("legacyRecoveryWarning");
    ASSERT_NE(button, nullptr); ASSERT_NE(warning, nullptr);
    EXPECT_FALSE(button->isEnabled());
    EXPECT_TRUE(warning->text().contains("no complete document"));
    button->click();
    EXPECT_EQ(dialog.take_recovered_assembly(), nullptr);
    EXPECT_EQ(readBytes(legacy), bytes);
    EXPECT_TRUE(RecoveryCheckpoint::find(dir).empty());
}
TEST_F(RecoveryDialogTest, NewestCheckpointFirstAndEqualTimesUsePathOrder) {
    model->set_name("old snapshot"); auto old = checkpoint();
    model->set_name("new snapshot"); auto newer = checkpoint();
    model->set_name("equal-time snapshot"); auto tied = checkpoint();
    const auto now = std::filesystem::file_time_type::clock::now();
    std::filesystem::last_write_time(old.path(), now - std::chrono::hours(2));
    std::filesystem::last_write_time(newer.path(), now - std::chrono::hours(1));
    std::filesystem::last_write_time(tied.path(), now - std::chrono::hours(1));
    CrashRecoveryDialog dialog({old.path(), tied.path(), newer.path()});
    auto* list = dialog.findChild<QListWidget*>("recoveryCheckpointList");
    ASSERT_NE(list, nullptr); ASSERT_EQ(list->count(), 3);
    const auto first = std::min(newer.path(), tied.path());
    const auto second = std::max(newer.path(), tied.path());
    EXPECT_EQ(list->item(0)->toolTip().toStdString(), first.string());
    EXPECT_EQ(list->item(1)->toolTip().toStdString(), second.string());
    EXPECT_EQ(list->item(2)->toolTip().toStdString(), old.path().string());
    list->setCurrentRow(2);
    dialog.findChild<QPushButton*>("recoverCheckpointButton")->click();
    auto recovered = dialog.take_recovered_assembly(); ASSERT_NE(recovered, nullptr);
    EXPECT_EQ(recovered->name(), "old snapshot"); // Selection and sorted backing paths agree.
}

TEST_F(RecoveryDialogTest, ArchivedCopiesDoNotPromptAtStartupButManualPickerCanOpen) {
    const auto recovery = dir / "recovery";
    std::filesystem::create_directory(recovery);
    auto handle = RecoveryCheckpoint::create(recovery, format.encode_recovery(*model));
    RecoveryCheckpoint::mark_closed(handle);
    const auto before = readBytes(handle.path());
    bool startupPrompted = false;
    {
        QObject timerOwner;
        QTimer::singleShot(0, &timerOwner, [&] {
            if (auto* modal = qobject_cast<CrashRecoveryDialog*>(QApplication::activeModalWidget())) {
                startupPrompted = true; modal->reject();
            }
        });
        EXPECT_EQ(CrashRecoveryDialog::check_and_recover(dir), nullptr);
    } // Context destruction cancels the unused startup callback.
    EXPECT_FALSE(startupPrompted);
    bool manualOpened = false;
    QTimer manualTimer;
    manualTimer.setSingleShot(true);
    QObject::connect(&manualTimer, &QTimer::timeout, [&] {
        auto* modal = qobject_cast<CrashRecoveryDialog*>(QApplication::activeModalWidget());
        if (!modal) { ADD_FAILURE() << "Expected manual recovery dialog"; return; }
        manualOpened = true;
        auto* list = modal->findChild<QListWidget*>("recoveryCheckpointList");
        if (!list || list->count() != 1) { ADD_FAILURE() << "Expected archived row"; modal->reject(); return; }
        list->setCurrentRow(0);
        modal->findChild<QPushButton*>("recoverCheckpointButton")->click();
        if (modal->result() != QDialog::Accepted) modal->reject();
    });
    manualTimer.start(0);
    auto recovered = CrashRecoveryDialog::check_and_recover(dir, true);
    EXPECT_TRUE(manualOpened); ASSERT_NE(recovered, nullptr);
    EXPECT_EQ(recovered->name(), model->name());
    EXPECT_EQ(readBytes(handle.path()), before);
    EXPECT_TRUE(RecoveryCheckpoint::find(recovery).empty());
    EXPECT_EQ(RecoveryCheckpoint::find(recovery/"closed").size(), 1u);
}

TEST_F(RecoveryDialogTest, TrashConfirmationDefaultsToNoAndCancellationChangesNothing) {
    auto handle = checkpoint(); const auto before = readBytes(handle.path());
    CrashRecoveryDialog dialog({handle.path()});
    auto* list = dialog.findChild<QListWidget*>("recoveryCheckpointList");
    auto* trash = dialog.findChild<QPushButton*>("trashCheckpointButton");
    ASSERT_NE(list, nullptr); ASSERT_NE(trash, nullptr);
    EXPECT_FALSE(trash->isEnabled()); list->setCurrentRow(0);
    EXPECT_TRUE(trash->isEnabled());
    bool confirmed = false;
    QTimer timer; timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, [&] {
        auto* question = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (!question) { ADD_FAILURE() << "Expected trash confirmation"; return; }
        confirmed = true;
        EXPECT_EQ(question->standardButton(question->defaultButton()), QMessageBox::No);
        EXPECT_TRUE(question->text().contains("running session"));
        EXPECT_TRUE(question->text().contains("next checkpoint"));
        question->button(QMessageBox::No)->click();
    });
    timer.start(0); trash->click();
    EXPECT_TRUE(confirmed);
    EXPECT_EQ(list->count(), 1);
    EXPECT_EQ(readBytes(handle.path()), before);
    EXPECT_EQ(readBytes(source), original);
    EXPECT_EQ(dialog.take_recovered_assembly(), nullptr);
}

TEST_F(RecoveryDialogTest, ManualPickerExcludesOwnIdentityButKeepsOtherActiveAndArchivedCopies) {
    const auto recovery = dir / "recovery";
    std::filesystem::create_directory(recovery);
    auto make = [&](const std::string& name) {
        model->set_name(name);
        return RecoveryCheckpoint::create(recovery, format.encode_recovery(*model));
    };
    auto own = make("own current");
    auto other = make("other active");
    auto archived = make("other archived");
    RecoveryCheckpoint::mark_closed(archived);
    const auto alias = recovery / ".studio-recovery-own-hardlink.checkpoint";
    std::filesystem::create_hard_link(own.path(), alias);
    const auto ownBytes = readBytes(own.path()), otherBytes = readBytes(other.path()),
               archivedBytes = readBytes(archived.path());
    for (const auto& target : {other.path(), archived.path()}) {
        bool inspected = false;
        QTimer timer; timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, [&] {
            auto* modal = qobject_cast<CrashRecoveryDialog*>(QApplication::activeModalWidget());
            if (!modal) { ADD_FAILURE() << "Expected manual picker"; return; }
            auto* list = modal->findChild<QListWidget*>("recoveryCheckpointList");
            if (!list) { ADD_FAILURE() << "Missing checkpoint list"; modal->reject(); return; }
            inspected = true;
            EXPECT_EQ(list->count(), 2); // Own pathname AND same-inode alias excluded.
            int row = -1;
            for (int i=0; i<list->count(); ++i) {
                const auto candidate = list->item(i)->toolTip().toStdString();
                EXPECT_NE(candidate, own.path().string());
                EXPECT_NE(candidate, alias.string());
                if (candidate == target.string()) row = i;
            }
            if (row < 0) { ADD_FAILURE() << "Other session copy unavailable"; modal->reject(); return; }
            list->setCurrentRow(row);
            modal->findChild<QPushButton*>("recoverCheckpointButton")->click();
            if (modal->result() != QDialog::Accepted) modal->reject();
        });
        timer.start(0);
        auto recovered = CrashRecoveryDialog::check_and_recover(dir, true, own.path());
        EXPECT_TRUE(inspected); ASSERT_NE(recovered, nullptr);
        EXPECT_EQ(recovered->name(), target == other.path() ? "other active" : "other archived");
        EXPECT_EQ(readBytes(own.path()), ownBytes);
        EXPECT_EQ(readBytes(alias), ownBytes);
        EXPECT_EQ(readBytes(other.path()), otherBytes);
        EXPECT_EQ(readBytes(archived.path()), archivedBytes);
        EXPECT_EQ(readBytes(source), original);
    }
}
}
}
