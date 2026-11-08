// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 IHP GmbH
#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace chiplet {
// Current-state persistence, not command replay. The format owns payload schema.
// No deletion API: discovery/recovery never destroys other session evidence.
class RecoveryCheckpoint {
public:
    class Handle {
    public:
        Handle(const Handle&) = default;
        Handle& operator=(const Handle&) = default;
        const std::filesystem::path& path() const { return m_path; }
        const std::string& content_sha256() const { return m_content_sha256; }
        std::uint64_t file_device() const { return m_file_device; }
        std::uint64_t file_inode() const { return m_file_inode; }
        std::uint64_t parent_device() const { return m_parent_device; }
        std::uint64_t parent_inode() const { return m_parent_inode; }
        bool closed() const { return m_closed; }
    private:
        friend class RecoveryCheckpoint;
        Handle(const std::filesystem::path& path, const std::string& digest,
               std::uint64_t device, std::uint64_t inode,
               std::uint64_t parentDevice, std::uint64_t parentInode)
            : m_path(path), m_content_sha256(digest), m_file_device(device),
              m_file_inode(inode), m_parent_device(parentDevice), m_parent_inode(parentInode) {}
        std::filesystem::path m_path;
        std::string m_content_sha256;
        std::uint64_t m_file_device, m_file_inode, m_parent_device, m_parent_inode;
        bool m_closed = false;
    };
    // Directory must already exist. Each call allocates a new document/session
    // identity; an existing discovered checkpoint cannot be silently adopted.
    // Symlinked ancestors are allowed; final directory/file links are not.
    // The issued handle retains the canonical directory, not its alias.
    static Handle create(const std::filesystem::path& directory, const std::string& payload);
    static void update(Handle& handle, const std::string& payload);
    // An externally removed active checkpoint may be replaced by a NEW UUID
    // only in its still-authenticated parent. Never reuse the missing pathname;
    // all other errors retain the original handle and refuse.
    static void update_or_recreate(Handle& handle, const std::string& payload);
    // Archive only an issued, current, open handle into its parent's closed/.
    // No overwrite/deletion of another checkpoint. Exact bytes are retained;
    // files remain until explicit user trash in the UI. On success update is
    // disabled. Publication/unlink is not a hostile-filesystem transaction;
    // an unlink failure can leave both copies, never silently overwrite either.
    static void mark_closed(Handle& handle);
    // Valid-envelope regular files only; corrupt files remain untouched.
    // Existence does not establish a crashed session. Format must admit payload.
    static std::vector<std::filesystem::path> find(const std::filesystem::path& directory);
    // Envelope/file limit: 128 MiB. Unknown/corrupt/oversized data throws.
    static std::string read(const std::filesystem::path& path);

    // Narrow deterministic I/O-failure seam: called after temporary bytes are
    // written, before publication. A throw preserves the previous checkpoint.
    static void update(Handle& handle, const std::string& payload,
                       const std::function<void()>& before_publish);
private:
    static Handle receipt(const std::filesystem::path& path);
};
}
