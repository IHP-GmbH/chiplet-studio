// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 IHP GmbH
#include <gtest/gtest.h>
#include <QTemporaryDir>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <type_traits>
#include <sys/stat.h>
#include <unistd.h>
#include "core/RecoveryCheckpoint.h"

namespace chiplet {
namespace {
static_assert(!std::is_default_constructible_v<RecoveryCheckpoint::Handle>);
static_assert(!std::is_aggregate_v<RecoveryCheckpoint::Handle>);
static_assert(std::is_copy_constructible_v<RecoveryCheckpoint::Handle>);
static_assert(!std::is_constructible_v<RecoveryCheckpoint::Handle,
              std::filesystem::path, std::string, std::uint64_t, std::uint64_t,
              std::uint64_t, std::uint64_t>);
std::string raw(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
void put(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << bytes;
}
TEST(RecoveryCheckpoint, OpaqueRoundtripAndIndependentSessionIdentities) {
    QTemporaryDir temp; ASSERT_TRUE(temp.isValid());
    const std::filesystem::path dir(temp.path().toStdString());
    const std::string payload("current\0document\r\n", 18);
    auto first = RecoveryCheckpoint::create(dir, payload);
    auto second = RecoveryCheckpoint::create(dir, "other session");
    EXPECT_NE(first.path(), second.path());
    EXPECT_EQ(RecoveryCheckpoint::read(first.path()), payload);
    RecoveryCheckpoint::update(first, "edited");
    EXPECT_EQ(RecoveryCheckpoint::read(first.path()), "edited");
    EXPECT_EQ(RecoveryCheckpoint::read(second.path()), "other session");
    EXPECT_EQ(RecoveryCheckpoint::find(dir).size(), 2u);
}
TEST(RecoveryCheckpoint, FailedPublicationPreservesLastCompleteBytesAndReceipt) {
    QTemporaryDir temp; ASSERT_TRUE(temp.isValid());
    auto handle = RecoveryCheckpoint::create(temp.path().toStdString(), "last complete");
    const auto bytes = raw(handle.path()), receipt = handle.content_sha256();
    bool reached = false;
    EXPECT_THROW(RecoveryCheckpoint::update(handle, "unsaved", [&] {
        reached = true; throw std::runtime_error("injected publication failure");
    }), std::runtime_error);
    EXPECT_TRUE(reached);
    EXPECT_EQ(raw(handle.path()), bytes);
    EXPECT_EQ(handle.content_sha256(), receipt);
    EXPECT_EQ(RecoveryCheckpoint::read(handle.path()), "last complete");
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(handle.path().parent_path()),
                            std::filesystem::directory_iterator()), 1);
}
TEST(RecoveryCheckpoint, CorruptionAndUnrelatedReplacementRefuseWithoutOverwrite) {
    QTemporaryDir temp; ASSERT_TRUE(temp.isValid());
    auto handle = RecoveryCheckpoint::create(temp.path().toStdString(), "baseline");
    auto bytes = raw(handle.path()); bytes.back() ^= 1; put(handle.path(), bytes);
    EXPECT_THROW(RecoveryCheckpoint::read(handle.path()), std::runtime_error);
    EXPECT_THROW(RecoveryCheckpoint::update(handle, "overwrite"), std::runtime_error);
    EXPECT_EQ(raw(handle.path()), bytes);
    EXPECT_TRUE(RecoveryCheckpoint::find(handle.path().parent_path()).empty());
    EXPECT_TRUE(std::filesystem::exists(handle.path()));
}
TEST(RecoveryCheckpoint, SymlinksAreNotCandidatesOrWritableTargets) {
    QTemporaryDir temp; ASSERT_TRUE(temp.isValid());
    const std::filesystem::path dir(temp.path().toStdString());
    auto handle = RecoveryCheckpoint::create(dir, "baseline");
    const auto saved = dir / "retained-original";
    std::filesystem::rename(handle.path(), saved);
    std::filesystem::create_symlink(saved, handle.path());
    EXPECT_THROW(RecoveryCheckpoint::read(handle.path()), std::runtime_error);
    EXPECT_THROW(RecoveryCheckpoint::update(handle, "overwrite"), std::runtime_error);
    EXPECT_TRUE(RecoveryCheckpoint::find(dir).empty());
    EXPECT_EQ(RecoveryCheckpoint::read(saved), "baseline");
    const auto alias = dir / "directory-alias";
    std::filesystem::create_directory_symlink(dir, alias);
    EXPECT_THROW(RecoveryCheckpoint::create(alias, "x"), std::runtime_error);
    EXPECT_THROW(RecoveryCheckpoint::find(alias), std::runtime_error);
}
TEST(RecoveryCheckpoint, DirectoryReplacementAndOversizePreserveEvidence) {
    QTemporaryDir temp; ASSERT_TRUE(temp.isValid());
    const std::filesystem::path root(temp.path().toStdString()), dir=root/"session";
    std::filesystem::create_directory(dir);
    auto handle = RecoveryCheckpoint::create(dir, "baseline");
    const auto bytes = raw(handle.path());
    EXPECT_THROW(RecoveryCheckpoint::update(handle, std::string(128*1024*1024, 'x')), std::runtime_error);
    EXPECT_EQ(raw(handle.path()), bytes);
    std::filesystem::rename(dir, root/"old-session");
    std::filesystem::create_directory(dir);
    put(handle.path(), bytes); // Same bytes, different directory and file identity.
    EXPECT_THROW(RecoveryCheckpoint::update(handle, "overwrite"), std::runtime_error);
    EXPECT_EQ(raw(handle.path()), bytes);
    put(root/".session.journal", "legacy evidence");
    EXPECT_TRUE(RecoveryCheckpoint::find(root).empty());
    EXPECT_EQ(raw(root/".session.journal"), "legacy evidence");
}
TEST(RecoveryCheckpoint, SymlinkedAncestorUsesCanonicalIdentityAndDetectsReplacement) {
    QTemporaryDir temp; ASSERT_TRUE(temp.isValid());
    const std::filesystem::path root(temp.path().toStdString());
    std::filesystem::create_directories(root/"real"/"recovery");
    std::filesystem::create_directory_symlink(root/"real", root/"alias");
    auto handle = RecoveryCheckpoint::create(root/"alias"/"recovery", "initial");
    EXPECT_EQ(handle.path().parent_path(), std::filesystem::canonical(root/"real"/"recovery"));
    EXPECT_EQ(RecoveryCheckpoint::read(root/"alias"/"recovery"/handle.path().filename()), "initial");
    RecoveryCheckpoint::update(handle, "updated");
    EXPECT_EQ(RecoveryCheckpoint::read(handle.path()), "updated");
    const auto bytes = raw(handle.path());
    std::filesystem::rename(root/"real"/"recovery", root/"real"/"preserved");
    std::filesystem::create_directory(root/"real"/"recovery");
    put(handle.path(), bytes);
    EXPECT_THROW(RecoveryCheckpoint::update(handle, "overwrite"), std::runtime_error);
    EXPECT_EQ(raw(handle.path()), bytes);
}
TEST(RecoveryCheckpoint, ClosingArchivesOnlyIssuedSessionAndPreservesExactBytes) {
    QTemporaryDir temp; ASSERT_TRUE(temp.isValid());
    const std::filesystem::path dir(temp.path().toStdString());
    auto first = RecoveryCheckpoint::create(dir, "finished session");
    auto other = RecoveryCheckpoint::create(dir, "still active");
    const auto oldPath = first.path();
    const auto bytes = raw(oldPath), digest = first.content_sha256();
    const auto inode = first.file_inode();
    RecoveryCheckpoint::mark_closed(first);
    EXPECT_TRUE(first.closed());
    EXPECT_FALSE(std::filesystem::exists(oldPath));
    EXPECT_EQ(first.path(), dir/"closed"/oldPath.filename());
    EXPECT_EQ(first.content_sha256(), digest);
    EXPECT_EQ(first.file_inode(), inode);
    EXPECT_EQ(raw(first.path()), bytes);
    EXPECT_EQ(RecoveryCheckpoint::read(first.path()), "finished session");
    EXPECT_EQ(RecoveryCheckpoint::find(dir), (std::vector<std::filesystem::path>{other.path()}));
    EXPECT_EQ(RecoveryCheckpoint::find(dir/"closed"), (std::vector<std::filesystem::path>{first.path()}));
    EXPECT_THROW(RecoveryCheckpoint::mark_closed(first), std::runtime_error);
    EXPECT_THROW(RecoveryCheckpoint::update(first, "overwrite"), std::runtime_error);
    EXPECT_EQ(raw(first.path()), bytes);
    EXPECT_EQ(RecoveryCheckpoint::read(other.path()), "still active");
}
TEST(RecoveryCheckpoint, ArchiveCollisionAndFinalDirectorySymlinkNeverOverwrite) {
    QTemporaryDir temp; ASSERT_TRUE(temp.isValid());
    const std::filesystem::path dir(temp.path().toStdString());
    auto handle = RecoveryCheckpoint::create(dir, "pending");
    const auto original = raw(handle.path());
    std::filesystem::create_directory(dir/"closed");
    const auto collision = dir/"closed"/handle.path().filename();
    put(collision, "unrelated archived bytes");
    EXPECT_THROW(RecoveryCheckpoint::mark_closed(handle), std::runtime_error);
    EXPECT_FALSE(handle.closed());
    EXPECT_EQ(raw(handle.path()), original);
    EXPECT_EQ(raw(collision), "unrelated archived bytes");
    std::filesystem::rename(dir/"closed", dir/"retained-closed");
    std::filesystem::create_directory_symlink(dir/"retained-closed", dir/"closed");
    EXPECT_THROW(RecoveryCheckpoint::mark_closed(handle), std::runtime_error);
    EXPECT_EQ(raw(handle.path()), original);
    EXPECT_EQ(raw(collision), "unrelated archived bytes");
}
TEST(RecoveryCheckpoint, MissingOwnedFileRecreatesNewIdentityWithoutTouchingOtherSession) {
    QTemporaryDir temp; ASSERT_TRUE(temp.isValid());
    const std::filesystem::path dir(temp.path().toStdString());
    auto handle = RecoveryCheckpoint::create(dir, "old current state");
    auto stale = handle;
    auto other = RecoveryCheckpoint::create(dir, "other session");
    const auto otherBytes = raw(other.path());
    ASSERT_TRUE(std::filesystem::remove(handle.path()));
    RecoveryCheckpoint::update_or_recreate(handle, "new current state");
    EXPECT_NE(handle.path(), stale.path());
    EXPECT_EQ(handle.parent_device(), stale.parent_device());
    EXPECT_EQ(handle.parent_inode(), stale.parent_inode());
    EXPECT_EQ(RecoveryCheckpoint::read(handle.path()), "new current state");
    EXPECT_FALSE(std::filesystem::exists(stale.path()));
    EXPECT_THROW(RecoveryCheckpoint::update(stale, "must not return"), std::runtime_error);
    EXPECT_FALSE(std::filesystem::exists(stale.path()));
    EXPECT_EQ(raw(other.path()), otherBytes);
    const auto livePath = handle.path();
    RecoveryCheckpoint::update_or_recreate(handle, "normal next update");
    EXPECT_EQ(handle.path(), livePath);
    EXPECT_EQ(RecoveryCheckpoint::read(handle.path()), "normal next update");
    EXPECT_EQ(RecoveryCheckpoint::find(dir).size(), 2u);
}
TEST(RecoveryCheckpoint, RecreateRefusesCorruptionAndFinalSymlinkWithoutNewFiles) {
    QTemporaryDir temp; ASSERT_TRUE(temp.isValid());
    const std::filesystem::path dir(temp.path().toStdString());
    auto handle = RecoveryCheckpoint::create(dir, "baseline");
    const auto oldPath = handle.path();
    const auto oldDigest = handle.content_sha256();
    auto corrupt = raw(oldPath); corrupt.back() ^= 1; put(oldPath, corrupt);
    EXPECT_THROW(RecoveryCheckpoint::update_or_recreate(handle, "overwrite"), std::runtime_error);
    EXPECT_EQ(handle.path(), oldPath);
    EXPECT_EQ(handle.content_sha256(), oldDigest);
    EXPECT_EQ(raw(oldPath), corrupt);
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(dir), std::filesystem::directory_iterator()), 1);
    ASSERT_TRUE(std::filesystem::remove(oldPath));
    std::filesystem::create_symlink(dir / "missing-link-target", oldPath);
    EXPECT_THROW(RecoveryCheckpoint::update_or_recreate(handle, "overwrite"), std::runtime_error);
    EXPECT_TRUE(std::filesystem::is_symlink(oldPath));
    EXPECT_FALSE(std::filesystem::exists(dir / "missing-link-target"));
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(dir), std::filesystem::directory_iterator()), 1);
}
TEST(RecoveryCheckpoint, RecreateRefusesMissingFilenameInReplacedParent) {
    QTemporaryDir temp; ASSERT_TRUE(temp.isValid());
    const std::filesystem::path root(temp.path().toStdString()), dir = root / "session";
    std::filesystem::create_directory(dir);
    auto handle = RecoveryCheckpoint::create(dir, "retained old state");
    const auto oldPath = handle.path();
    std::filesystem::rename(dir, root / "retained");
    std::filesystem::create_directory(dir);
    ASSERT_FALSE(std::filesystem::exists(oldPath));
    EXPECT_THROW(RecoveryCheckpoint::update_or_recreate(handle, "must not create"), std::runtime_error);
    EXPECT_TRUE(std::filesystem::is_empty(dir));
    EXPECT_EQ(handle.path(), oldPath);
    EXPECT_EQ(RecoveryCheckpoint::read(root / "retained" / oldPath.filename()), "retained old state");
}
TEST(RecoveryCheckpoint, RecreateRefusesPermissionFailureAndClosedHandle) {
    // This native gate runs as the assigned non-root user; root is not a valid
    // permission-refusal witness and must not silently skip this control.
    ASSERT_NE(::geteuid(), 0u);
    QTemporaryDir temp; ASSERT_TRUE(temp.isValid());
    const std::filesystem::path dir(temp.path().toStdString());
    auto handle = RecoveryCheckpoint::create(dir, "permission protected");
    const auto oldPath = handle.path();
    const auto oldDigest = handle.content_sha256();
    ASSERT_EQ(::chmod(oldPath.c_str(), 0000), 0);
    EXPECT_THROW(RecoveryCheckpoint::update_or_recreate(handle, "must not create"), std::runtime_error);
    EXPECT_EQ(::chmod(oldPath.c_str(), 0600), 0);
    EXPECT_EQ(handle.path(), oldPath);
    EXPECT_EQ(handle.content_sha256(), oldDigest);
    EXPECT_EQ(RecoveryCheckpoint::read(oldPath), "permission protected");
    EXPECT_EQ(RecoveryCheckpoint::find(dir).size(), 1u);
    RecoveryCheckpoint::mark_closed(handle);
    const auto archivedPath = handle.path();
    ASSERT_TRUE(std::filesystem::remove(archivedPath));
    EXPECT_THROW(RecoveryCheckpoint::update_or_recreate(handle, "closed must stay closed"), std::runtime_error);
    EXPECT_EQ(handle.path(), archivedPath);
    EXPECT_TRUE(std::filesystem::is_empty(dir / "closed"));
}
}
}
