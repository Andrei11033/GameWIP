#pragma once

/// @file filesystem_platform.h
/// @brief Internal platform abstraction used by the FileSystem library.

#include "filesystem/filesystem.h"

#include <cstdint>
#include <memory>
#include <vector>

#ifndef INTERNAL_FILESYSTEM_TEST_HOOKS
#define INTERNAL_FILESYSTEM_TEST_HOOKS 0
#endif

namespace GameWIP::FileSystem::Detail
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

        void *nativeHandle = nullptr;
        Types::FileAccess access = Types::FileAccess::ReadWrite;
        IO::Types::FlushMode flushOnClose = IO::Types::FlushMode::None;
        std::shared_ptr<std::uint32_t> activeLocks;
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

        void *nativeHandle = nullptr;
        std::shared_ptr<std::uint32_t> activeLocks;
        bool active = false;
        bool exclusive = false;
    };

    /// Owns one backend directory enumeration and the handles that prevent path-component replacement races.
    struct DirectoryCursorState
    {
        DirectoryCursorState() = default;
        DirectoryCursorState(const DirectoryCursorState &) = delete;
        DirectoryCursorState &operator=(const DirectoryCursorState &) = delete;
        DirectoryCursorState(DirectoryCursorState &&) = delete;
        DirectoryCursorState &operator=(DirectoryCursorState &&) = delete;
        ~DirectoryCursorState() noexcept;

        Types::Path directoryPath;
        Types::DirectoryEntry bufferedEntry;
        std::vector<void *> stableHandles;
        void *nativeFindHandle = nullptr;
        Types::SymlinkPolicy symlinkPolicy = Types::SymlinkPolicy::DoNotFollow;
        bool bufferedEntryHidden = false;
        bool hasBufferedEntry = false;
        bool finished = false;
    };
} // namespace GameWIP::FileSystem::Detail

namespace GameWIP::FileSystem::Detail::Platform
{
#if INTERNAL_FILESYSTEM_TEST_HOOKS
    namespace TestHooks
    {
        /// Forces native file-lock release attempts to fail until disabled.
        void setFileUnlockFailure(bool enabled) noexcept;
        /// Restores FileSystem platform test hooks to their default state.
        void reset() noexcept;
    } // namespace TestHooks
#endif

    /// @brief Result returned by backend entry metadata queries.
    struct EntryQueryResult
    {
        /// @brief Success or portable/backend-native failure status.
        IO::Types::Status status;
        /// @brief Entry metadata when status is successful.
        Types::EntryInfo info{};
    };

    /// @brief Backend result for non-blocking native lock acquisition.
    struct NativeLockResult
    {
        /// @brief Operation status. WouldBlock is represented by outcome, not an error.
        IO::Types::Status status;
        /// @brief Whether the native lock was acquired.
        Types::LockOutcome outcome = Types::LockOutcome::WouldBlock;
        /// @brief Native lock state when outcome is Acquired.
        std::unique_ptr<Detail::FileLockState> state;
    };

    /// Cursor-open result; state is present only on success.
    struct DirectoryCursorOpenResult
    {
        IO::Types::Status status;
        std::unique_ptr<Detail::DirectoryCursorState> state;
    };

    /// One cursor step; successful exhaustion is represented by hasEntry=false.
    struct DirectoryCursorNextResult
    {
        IO::Types::Status status;
        Types::DirectoryEntry entry;
        bool hidden = false;
        bool hasEntry = false;
    };

    /// @brief Queries one existing entry using backend-native filesystem semantics.
    /// @details FollowAll follows normal platform path resolution. DoNotFollow and FollowFinal must reject intermediate symlinks
    /// during handle-relative traversal rather than checking a path and reopening it later.
    /// @param path Entry path to inspect.
    /// @param symlinkPolicy Symlink policy requested by the public operation.
    /// @return Entry metadata, InvalidArgument for invalid policy, or a portable/backend-native failure status.
    [[nodiscard]] EntryQueryResult queryEntry(const Types::Path &path, Types::SymlinkPolicy symlinkPolicy) noexcept;

    /// @brief Opens a read-only file handle.
    /// @param state Receives newly opened native state on success. Failure leaves it unchanged.
    /// @param path File path to open.
    /// @param options Sharing and symlink-resolution behavior.
    /// @return Success, or a validation/open failure status.
    [[nodiscard]] IO::Types::Status openReader(
        std::unique_ptr<Detail::FileState> &state,
        const Types::Path &path,
        const Types::FileReaderOpenOptions &options) noexcept;

    /// @brief Opens a write-only file handle.
    /// @param state Receives newly opened native state on success. Failure leaves it unchanged.
    /// @param path File path to open.
    /// @param options Creation, append, sharing, symlink, and close-flush behavior.
    /// @return Success, or a validation/open failure status.
    [[nodiscard]] IO::Types::Status openWriter(
        std::unique_ptr<Detail::FileState> &state,
        const Types::Path &path,
        const Types::FileWriterOpenOptions &options) noexcept;

    /// @brief Opens a read/write, read-only, or write-only file handle.
    /// @param state Receives newly opened native state on success. Failure leaves it unchanged.
    /// @param path File path to open.
    /// @param options Access, creation, sharing, initial-position, symlink, and close-flush behavior.
    /// @return Success, or a validation/open/seek failure status.
    [[nodiscard]] IO::Types::Status openFile(
        std::unique_ptr<Detail::FileState> &state,
        const Types::Path &path,
        const Types::FileOpenOptions &options) noexcept;

    /// @brief Reads from an open native file handle.
    [[nodiscard]] IO::Types::ReadResult readFile(Detail::FileState &state, std::span<std::byte> destination) noexcept;

    /// @brief Writes to an open native file handle.
    [[nodiscard]] IO::Types::WriteResult writeFile(Detail::FileState &state, std::span<const std::byte> bytes) noexcept;

    /// @brief Flushes an open native file handle.
    [[nodiscard]] IO::Types::Status flushFile(Detail::FileState &state, IO::Types::FlushMode mode) noexcept;

    /// @brief Closes an open native file handle. ResourceBusy is returned while active locks remain.
    [[nodiscard]] IO::Types::Status closeFile(Detail::FileState &state) noexcept;

    /// @brief Returns the current file position for a seekable native handle.
    [[nodiscard]] IO::Types::PositionResult filePosition(const Detail::FileState &state) noexcept;

    /// @brief Returns the current file size for an open native handle.
    [[nodiscard]] IO::Types::SizeResult fileSize(const Detail::FileState &state) noexcept;

    /// @brief Moves the current file position for a seekable native handle.
    [[nodiscard]] IO::Types::Status seekFile(Detail::FileState &state, std::int64_t offset, IO::Types::SeekOrigin origin) noexcept;

    /// @brief Resizes an open writable native file handle.
    [[nodiscard]] IO::Types::Status resizeFile(Detail::FileState &state, std::uint64_t sizeBytes) noexcept;

    /// @brief Attempts to acquire a non-blocking whole-file lock from an open native handle.
    [[nodiscard]] NativeLockResult tryLockFile(Detail::FileState &state, Types::FileLockMode mode) noexcept;

    /// @brief Unlocks a whole-file lock. Failure leaves the lock active.
    [[nodiscard]] IO::Types::Status unlockFile(Detail::FileLockState &state) noexcept;

    /// @brief Moves or replaces one path with the platform-native rename primitive.
    [[nodiscard]] IO::Types::Status movePath(
        const Types::Path &from,
        const Types::Path &to,
        Types::ReplaceMode replaceMode,
        Types::SymlinkPolicy symlinkPolicy) noexcept;

    /// @brief Creates one directory level using backend-native path traversal.
    [[nodiscard]] IO::Types::Status createDirectory(const Types::Path &path, const Types::CreateDirectoryOptions &options) noexcept;

    /// @brief Creates a directory and any missing parents using backend-native path traversal.
    [[nodiscard]] IO::Types::Status createDirectories(const Types::Path &path, const Types::CreateDirectoryOptions &options) noexcept;

    /// @brief Lists direct directory children using backend-native path traversal.
    [[nodiscard]] Types::ListDirectoryResult listDirectory(const Types::Path &path, const Types::ListDirectoryOptions &options) noexcept;

    /// Opens a streaming cursor over direct children while stabilizing strict-policy path components.
    [[nodiscard]] DirectoryCursorOpenResult openDirectoryCursor(const Types::Path &path, Types::SymlinkPolicy symlinkPolicy) noexcept;

    /// Opens a nested cursor relative to its active parent, retaining only the new directory handle.
    /// The parent cursor must outlive the returned cursor.
    [[nodiscard]] DirectoryCursorOpenResult openChildDirectoryCursor(
        const Detail::DirectoryCursorState &parent,
        const Types::Path &path,
        Types::SymlinkPolicy symlinkPolicy) noexcept;

    /// Reads one direct child without collecting siblings; exhaustion remains successful.
    [[nodiscard]] DirectoryCursorNextResult readDirectoryCursor(Detail::DirectoryCursorState &state) noexcept;

    /// @brief Changes read-only metadata through a backend-native entry handle.
    [[nodiscard]] IO::Types::Status setReadOnly(const Types::Path &path, bool readOnly, Types::SymlinkPolicy symlinkPolicy) noexcept;

    /// @brief Copies portable basic metadata from one regular file to another through backend-native entry handles.
    [[nodiscard]] IO::Types::Status copyBasicMetadata(const Types::Path &from, const Types::Path &to, Types::SymlinkPolicy symlinkPolicy) noexcept;

    /// @brief Removes one file-like entry through a backend-native entry handle.
    [[nodiscard]] IO::Types::Status removeFile(const Types::Path &path, const Types::RemoveOptions &options) noexcept;

    /// @brief Removes one empty directory through a backend-native entry handle.
    [[nodiscard]] IO::Types::Status removeEmptyDirectory(const Types::Path &path, const Types::RemoveOptions &options) noexcept;

    /// @brief Flushes filesystem metadata for a directory path.
    [[nodiscard]] IO::Types::Status flushDirectory(const Types::Path &path) noexcept;

    /// @brief Returns whether a backend considers an entry hidden.
    [[nodiscard]] Types::BoolResult isHidden(const Types::Path &path) noexcept;
} // namespace GameWIP::FileSystem::Detail::Platform
