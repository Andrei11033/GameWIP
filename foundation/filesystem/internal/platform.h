/// @file platform.h
/// @brief Internal platform abstraction used by the FileSystem library.

#pragma once

#include "filesystem/filesystem.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace GameWIP::FileSystem::Internal
{
    /// @brief Platform-owned native file handle state used by File, FileReader, and FileWriter.
    struct FileState
    {
        FileState();
        FileState(const FileState &) = delete;
        FileState &operator=(const FileState &) = delete;
        FileState(FileState &&) = delete;
        FileState &operator=(FileState &&) = delete;
        ~FileState() noexcept;

        // Native handle and open policy
        void *nativeHandle = nullptr; ///< Backend-native file handle owned by this state.
        Types::File::Access access =
            Types::File::Access::ReadWrite; ///< Public access selected at open time; meaningful while nativeHandle is active.
        IO::Types::FlushMode flushOnClose =
            IO::Types::FlushMode::None;             ///< Flush strength attempted before explicit close releases a writable handle.
        std::shared_ptr<std::uint32_t> activeLocks; ///< Shared active-lock count retained by the handle and every detached lock owner.

        // Effective access mode
        bool readable = false;
        bool writable = false;
        bool appendMode = false;
    };

    /// @brief Platform-owned native whole-file lock state.
    struct FileLockState
    {
        FileLockState() noexcept = default;
        FileLockState(const FileLockState &) = delete;
        FileLockState &operator=(const FileLockState &) = delete;
        FileLockState(FileLockState &&) = delete;
        FileLockState &operator=(FileLockState &&) = delete;
        ~FileLockState() noexcept;

        // Detached lock ownership
        void *nativeHandle = nullptr;               ///< Duplicated backend handle that lets the lock outlive its originating wrapper.
        std::shared_ptr<std::uint32_t> activeLocks; ///< Counter shared with the originating file state for explicit-close ResourceBusy checks.

        // Lock state
        bool active = false;
        bool exclusive = false;
    };

    /// @brief Owns one backend directory enumeration and the handles that prevent path-component replacement races.
    struct DirectoryCursorState
    {
        DirectoryCursorState() = default;
        DirectoryCursorState(const DirectoryCursorState &) = delete;
        DirectoryCursorState &operator=(const DirectoryCursorState &) = delete;
        DirectoryCursorState(DirectoryCursorState &&) = delete;
        DirectoryCursorState &operator=(DirectoryCursorState &&) = delete;
        ~DirectoryCursorState() noexcept;

        // Current directory and buffered entry
        Types::Path directoryPath;             ///< Path used to enumerate direct children.
        Types::Directory::Entry bufferedEntry; ///< Next visible entry retained for the caller.

        // Handles and policy retained for enumeration
        std::vector<void *> stableHandles; ///< Native handles retaining strict-policy path components during enumeration.
        void *nativeFindHandle = nullptr;  ///< Backend enumeration handle.
        Types::SymlinkPolicy symlinkPolicy = Types::SymlinkPolicy::DoNotFollow; ///< Policy applied while resolving the directory path.

        // Cursor state
        bool bufferedEntryHidden = false; ///< Whether the buffered entry is hidden.
        bool hasBufferedEntry = false;    ///< Whether a next visible entry is buffered.
        bool finished = false;            ///< Whether enumeration reached the end or failed.
    };
} // namespace GameWIP::FileSystem::Internal

namespace GameWIP::FileSystem::Platform
{
    /// ------------------------------------------------------------
    /// Native operation results
    /// ------------------------------------------------------------

    /// @brief Backend entry retained until the core applies public filters.
    struct DirectoryCursorEntry
    {
        Types::Directory::Entry entry;
        bool hidden = false;
    };

    /// ------------------------------------------------------------
    /// Entry queries
    /// ------------------------------------------------------------

    /// @brief Queries one existing entry using backend-native filesystem semantics.
    /// @details FollowAll follows normal platform path resolution. DoNotFollow and FollowFinal must reject intermediate symlinks
    /// during handle-relative traversal rather than checking a path and reopening it later.
    /// @param symlinkPolicy Symlink policy requested by the public operation.
    /// @return Entry metadata, InvalidArgument for invalid policy, or a portable/backend-native failure status.
    [[nodiscard]] GameWIP::Status::Types::Result<Types::EntryInfo> queryEntry(const Types::Path &path, Types::SymlinkPolicy symlinkPolicy) noexcept;

    /// ------------------------------------------------------------
    /// File operations
    /// ------------------------------------------------------------

    /// @brief Opens a read-only file handle.
    /// @param state Receives newly opened native state on success. Failure leaves it unchanged.
    /// @param options Sharing and symlink-resolution behavior.
    /// @return Success, or a validation/open failure status.
    [[nodiscard]] GameWIP::Status::Types::Result<void> openReader(
        std::unique_ptr<Internal::FileState> &state,
        const Types::Path &path,
        const Types::File::ReaderOpenOptions &options) noexcept;

    /// @brief Opens a write-only file handle.
    /// @param state Receives newly opened native state on success. Failure leaves it unchanged.
    /// @param options Creation, append, sharing, symlink, and close-flush behavior.
    /// @return Success, or a validation/open failure status.
    [[nodiscard]] GameWIP::Status::Types::Result<void> openWriter(
        std::unique_ptr<Internal::FileState> &state,
        const Types::Path &path,
        const Types::File::WriterOpenOptions &options) noexcept;

    /// @brief Opens a read/write, read-only, or write-only file handle.
    /// @param state Receives newly opened native state on success. Failure leaves it unchanged.
    /// @param options Access, creation, sharing, initial-position, symlink, and close-flush behavior.
    /// @return Success, or a validation/open/seek failure status.
    [[nodiscard]] GameWIP::Status::Types::Result<void> openFile(
        std::unique_ptr<Internal::FileState> &state,
        const Types::Path &path,
        const Types::File::OpenOptions &options) noexcept;

    /// @brief Reads from an open native file handle.
    [[nodiscard]] GameWIP::Status::Types::Result<IO::Types::ReadProgress> readFile(Internal::FileState &state, std::span<std::byte> destination) noexcept;

    /// @brief Writes to an open native file handle.
    [[nodiscard]] GameWIP::Status::Types::Result<IO::Types::WriteProgress> writeFile(Internal::FileState &state, std::span<const std::byte> bytes) noexcept;

    /// @brief Flushes an open native file handle.
    [[nodiscard]] GameWIP::Status::Types::Result<void> flushFile(Internal::FileState &state, IO::Types::FlushMode mode) noexcept;

    /// @brief Closes an open native file handle. ResourceBusy is returned while active locks remain.
    [[nodiscard]] GameWIP::Status::Types::Result<void> closeFile(Internal::FileState &state) noexcept;

    /// @brief Returns the current file position for a seekable native handle.
    [[nodiscard]] GameWIP::Status::Types::Result<std::uint64_t> filePosition(const Internal::FileState &state) noexcept;

    /// @brief Returns the current file size for an open native handle.
    [[nodiscard]] GameWIP::Status::Types::Result<std::uint64_t> fileSize(const Internal::FileState &state) noexcept;

    /// @brief Moves the current file position for a seekable native handle.
    [[nodiscard]] GameWIP::Status::Types::Result<void> seekFile(Internal::FileState &state, std::int64_t offset, IO::Types::SeekOrigin origin) noexcept;

    /// @brief Resizes an open writable native file handle, optionally preserving its observable position.
    [[nodiscard]] GameWIP::Status::Types::Result<void> resizeFile(Internal::FileState &state, std::uint64_t sizeBytes, bool restorePosition) noexcept;

    /// @brief Attempts to acquire a non-blocking whole-file lock from an open native handle.
    [[nodiscard]] GameWIP::Status::Types::Result<std::unique_ptr<Internal::FileLockState>>
    tryLockFile(Internal::FileState &state, Types::Lock::Mode mode) noexcept;

    /// @brief Unlocks a whole-file lock. Failure leaves the lock active.
    [[nodiscard]] GameWIP::Status::Types::Result<void> unlockFile(Internal::FileLockState &state) noexcept;

    /// ------------------------------------------------------------
    /// Path operations
    /// ------------------------------------------------------------

    /// @brief Moves or replaces one path with the platform-native rename primitive.
    [[nodiscard]] GameWIP::Status::Types::Result<void> movePath(
        const Types::Path &from,
        const Types::Path &to,
        Types::ReplaceMode replaceMode,
        Types::SymlinkPolicy symlinkPolicy) noexcept;

    /// ------------------------------------------------------------
    /// Directory operations
    /// ------------------------------------------------------------

    /// @brief Creates one directory level using backend-native path traversal.
    [[nodiscard]] GameWIP::Status::Types::Result<void> createDirectory(const Types::Path &path, const Types::Directory::CreateOptions &options) noexcept;

    /// @brief Creates a directory and any missing parents using backend-native path traversal.
    [[nodiscard]] GameWIP::Status::Types::Result<void> createDirectories(const Types::Path &path, const Types::Directory::CreateOptions &options) noexcept;

    /// @brief Reports whether a path names a filesystem root.
    [[nodiscard]] GameWIP::Status::Types::Result<bool> isRootPath(const Types::Path &path) noexcept;

    /// @brief Lists direct directory children using backend-native path traversal.
    [[nodiscard]] GameWIP::Status::Types::Result<std::vector<Types::Directory::Entry>>
    listDirectory(const Types::Path &path, const Types::Directory::ListOptions &options) noexcept;

    /// @brief Opens a streaming cursor over direct children while stabilizing strict-policy path components.
    [[nodiscard]] GameWIP::Status::Types::Result<std::unique_ptr<Internal::DirectoryCursorState>>
    openDirectoryCursor(const Types::Path &path, Types::SymlinkPolicy symlinkPolicy) noexcept;

    /// @brief Opens a nested cursor relative to its active parent, retaining only the new directory handle.
    /// The parent cursor must outlive the returned cursor.
    [[nodiscard]] GameWIP::Status::Types::Result<std::unique_ptr<Internal::DirectoryCursorState>> openChildDirectoryCursor(
        const Internal::DirectoryCursorState &parent,
        const Types::Path &path,
        Types::SymlinkPolicy symlinkPolicy) noexcept;

    /// @brief Reads one direct child without collecting siblings; exhaustion remains successful.
    [[nodiscard]] GameWIP::Status::Types::Result<std::optional<DirectoryCursorEntry>> readDirectoryCursor(Internal::DirectoryCursorState &state) noexcept;

    /// ------------------------------------------------------------
    /// Metadata and entry mutations
    /// ------------------------------------------------------------

    /// @brief Changes read-only metadata through a backend-native entry handle.
    [[nodiscard]] GameWIP::Status::Types::Result<void> setReadOnly(const Types::Path &path, bool readOnly, Types::SymlinkPolicy symlinkPolicy) noexcept;

    /// @brief Copies portable basic metadata from one regular file to another through backend-native entry handles.
    [[nodiscard]] GameWIP::Status::Types::Result<void> copyBasicMetadata(const Types::Path &from, const Types::Path &to, Types::SymlinkPolicy symlinkPolicy) noexcept;

    /// @brief Removes one file-like entry through a backend-native entry handle.
    [[nodiscard]] GameWIP::Status::Types::Result<void> removeFile(const Types::Path &path, const Types::RemoveOptions &options) noexcept;

    /// @brief Removes one empty directory through a backend-native entry handle.
    [[nodiscard]] GameWIP::Status::Types::Result<void> removeEmptyDirectory(const Types::Path &path, const Types::RemoveOptions &options) noexcept;

    /// @brief Flushes filesystem metadata for a directory path.
    [[nodiscard]] GameWIP::Status::Types::Result<void> flushDirectory(const Types::Path &path) noexcept;

    /// @brief Returns whether a backend considers an entry hidden.
    [[nodiscard]] GameWIP::Status::Types::Result<bool> isHidden(const Types::Path &path) noexcept;
} // namespace GameWIP::FileSystem::Platform
