// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 IHP GmbH
#include "RecoveryCheckpoint.h"
#include <QCryptographicHash>
#include <QTemporaryFile>
#include <QUuid>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace chiplet {
namespace {
constexpr const char* magic = "studio-recovery-checkpoint-v1\n";
void need(bool value, const std::string& reason) {
    if (!value) throw std::runtime_error("Recovery checkpoint: " + reason);
}
std::string digest(const std::string& bytes) {
    return QCryptographicHash::hash(QByteArray(bytes.data(), static_cast<int>(bytes.size())),
                                    QCryptographicHash::Sha256).toHex().toStdString();
}
struct stat directory(const std::filesystem::path& input) {
    const auto absolute = std::filesystem::absolute(input).lexically_normal();
    struct stat st{};
    need(::lstat(absolute.c_str(), &st) == 0 && S_ISDIR(st.st_mode),
         "final directory missing or symlink: " + absolute.string());
    return st;
}
struct Content { std::string bytes; struct stat st{}; };
class MissingCheckpoint final : public std::runtime_error {
public:
    explicit MissingCheckpoint(const std::filesystem::path& path)
        : std::runtime_error("Recovery checkpoint file is missing: " + path.string()) {}
};
Content content(const std::filesystem::path& path) {
    const auto absolute = std::filesystem::absolute(path).lexically_normal();
    directory(absolute.parent_path());
    const auto normalized = std::filesystem::canonical(absolute.parent_path()) / absolute.filename();
    const int fd = ::open(normalized.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0 && errno == ENOENT) {
        struct stat absent{};
        // O_NOFOLLOW already excludes final links. Confirm true absence, not
        // an inspection error or a name that appeared after open failed.
        if (::lstat(normalized.c_str(), &absent) != 0 && errno == ENOENT)
            throw MissingCheckpoint(normalized);
    }
    need(fd >= 0, "cannot open regular checkpoint: " + path.string());
    Content result;
    try {
        need(::fstat(fd, &result.st) == 0 && S_ISREG(result.st.st_mode), "not a regular checkpoint");
        // A bounded recovery artifact, never a device or unbounded stream.
        need(result.st.st_size >= 0 && result.st.st_size <= 128 * 1024 * 1024,
             "checkpoint exceeds 128 MiB");
        char buffer[16384];
        for (;;) {
            const auto count = ::read(fd, buffer, sizeof(buffer));
            if (count < 0 && errno == EINTR) continue;
            need(count >= 0, "checkpoint read failed");
            if (count == 0) break;
            result.bytes.append(buffer, static_cast<std::size_t>(count));
            need(result.bytes.size() <= 128 * 1024 * 1024, "checkpoint grew beyond limit");
        }
        need(result.bytes.size() == static_cast<std::size_t>(result.st.st_size), "checkpoint changed while reading");
    } catch (...) { ::close(fd); throw; }
    ::close(fd);
    return result;
}
std::string envelope(const std::string& payload) {
    need(payload.size() < 128 * 1024 * 1024 - 128, "payload exceeds checkpoint limit");
    return std::string(magic) + digest(payload) + "\n" + payload;
}
void authenticate(const RecoveryCheckpoint::Handle& expected) {
    need(expected.path() == std::filesystem::absolute(expected.path()).lexically_normal(),
         "handle path is not its original normalized absolute identity");
    const auto actual = content(expected.path());
    const auto parent = directory(expected.path().parent_path());
    need(digest(actual.bytes) == expected.content_sha256() &&
         actual.st.st_dev == expected.file_device() && actual.st.st_ino == expected.file_inode() &&
         parent.st_dev == expected.parent_device() && parent.st_ino == expected.parent_inode(),
         "checkpoint changed externally; previous evidence retained");
}
void publish(const std::filesystem::path& target, const std::string& bytes,
             const RecoveryCheckpoint::Handle* previous, const std::function<void()>& hook) {
    const auto parent = directory(target.parent_path());
    QTemporaryFile temp(QString::fromStdString((target.parent_path() / ".recovery-write-XXXXXX").string()));
    need(temp.open(), "cannot create temporary checkpoint");
    need(temp.write(bytes.data(), static_cast<qint64>(bytes.size())) == static_cast<qint64>(bytes.size()) &&
         temp.flush() && ::fsync(temp.handle()) == 0, "temporary checkpoint write failed");
    if (hook) hook();
    const auto now = directory(target.parent_path());
    need(now.st_dev == parent.st_dev && now.st_ino == parent.st_ino, "checkpoint directory changed");
    if (previous) authenticate(*previous);
    const auto temporary = temp.fileName().toStdString();
    if (previous) {
        need(::rename(temporary.c_str(), target.c_str()) == 0, "atomic checkpoint replacement failed");
    } else {
        // Unlike rename, link refuses an existing target, including symlinks.
        need(::link(temporary.c_str(), target.c_str()) == 0, "exclusive checkpoint creation failed");
    }
    // Same-directory atomic publication. Not immunity to hostile concurrent
    // path changes, nor a claim of power-loss durability for directory metadata.
}
}
RecoveryCheckpoint::Handle RecoveryCheckpoint::receipt(const std::filesystem::path& path) {
    const auto c = content(path);
    const auto parent = directory(path.parent_path());
    return {path, digest(c.bytes), static_cast<std::uint64_t>(c.st.st_dev),
            static_cast<std::uint64_t>(c.st.st_ino), static_cast<std::uint64_t>(parent.st_dev),
            static_cast<std::uint64_t>(parent.st_ino)};
}
RecoveryCheckpoint::Handle RecoveryCheckpoint::create(const std::filesystem::path& dir,
                                                       const std::string& payload) {
    directory(dir);
    const auto path = std::filesystem::canonical(dir) /
        (".studio-recovery-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString() + ".checkpoint");
    publish(path, envelope(payload), nullptr, {});
    return receipt(path);
}
void RecoveryCheckpoint::update(Handle& handle, const std::string& payload) { update(handle, payload, {}); }
void RecoveryCheckpoint::update_or_recreate(Handle& handle, const std::string& payload) {
    need(!handle.closed(), "closed checkpoint cannot be recreated");
    try {
        update(handle, payload);
        return;
    } catch (const MissingCheckpoint&) {
        // This is an issued handle, not authority inferred from a discovered
        // filename. Parent replacement must not be mistaken for file removal.
        const auto parentPath = handle.path().parent_path();
        const auto parent = directory(parentPath);
        need(parent.st_dev == handle.parent_device() && parent.st_ino == handle.parent_inode()
             && std::filesystem::canonical(parentPath) == parentPath,
             "missing checkpoint parent changed; recreation refused");
        struct stat absent{};
        need(::lstat(handle.path().c_str(), &absent) != 0 && errno == ENOENT,
             "checkpoint is not genuinely absent; recreation refused");
        auto replacement = create(parentPath, payload);
        need(replacement.path() != handle.path()
             && replacement.parent_device() == handle.parent_device()
             && replacement.parent_inode() == handle.parent_inode(),
             "recreated checkpoint parent changed; original handle retained");
        handle = replacement;
    }
}
void RecoveryCheckpoint::update(Handle& handle, const std::string& payload,
                                const std::function<void()>& before_publish) {
    need(!handle.closed(), "closed checkpoint cannot be updated");
    authenticate(handle);
    publish(handle.path(), envelope(payload), &handle, before_publish);
    handle = receipt(handle.path());
}
void RecoveryCheckpoint::mark_closed(Handle& handle) {
    need(!handle.closed(), "checkpoint is already closed");
    authenticate(handle);
    // A valid checksum envelope is required even for an issued handle.
    read(handle.path());
    const auto closedDir = handle.path().parent_path() / "closed";
    need(::mkdir(closedDir.c_str(), 0700) == 0 || errno == EEXIST,
         "cannot create closed checkpoint directory");
    directory(closedDir); // Existing final symlink/non-directory is refused.
    const auto destination = std::filesystem::canonical(closedDir) / handle.path().filename();
    authenticate(handle);
    if (::link(handle.path().c_str(), destination.c_str()) != 0) {
        const int error = errno;
        need(false, "closed checkpoint exclusive publication failed: " + std::string(std::strerror(error)));
    }
    auto archived = receipt(destination);
    need(archived.content_sha256() == handle.content_sha256() &&
         archived.file_device() == handle.file_device() && archived.file_inode() == handle.file_inode(),
         "archive identity changed; both paths retained");
    authenticate(handle);
    need(::unlink(handle.path().c_str()) == 0, "archive published but active path could not be removed; both retained");
    archived.m_closed = true;
    handle = archived;
}
std::string RecoveryCheckpoint::read(const std::filesystem::path& path) {
    const auto bytes = content(path).bytes;
    const std::string prefix(magic);
    need(bytes.compare(0, prefix.size(), prefix) == 0, "unknown or corrupt envelope");
    const auto end = bytes.find('\n', prefix.size());
    need(end != std::string::npos && end == prefix.size() + 64, "corrupt checksum header");
    const auto payload = bytes.substr(end + 1);
    need(bytes.substr(prefix.size(), 64) == digest(payload), "payload checksum mismatch");
    return payload;
}
std::vector<std::filesystem::path> RecoveryCheckpoint::find(const std::filesystem::path& dir) {
    directory(dir);
    std::vector<std::filesystem::path> paths;
    for (const auto& item : std::filesystem::directory_iterator(dir)) {
        const auto name = item.path().filename().string();
        if (name.rfind(".studio-recovery-", 0) == 0 && name.size() > 11 &&
            name.compare(name.size()-11, 11, ".checkpoint") == 0 &&
            std::filesystem::is_regular_file(item.symlink_status())) {
            try { read(item.path()); paths.push_back(item.path()); }
            catch (const std::runtime_error&) { /* Leave invalid evidence untouched. */ }
        }
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}
}
