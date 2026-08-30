/// @file entry.h
/// @brief Public generic filesystem-entry vocabulary and operations.

#pragma once

#include "filesystem/path.h"
#include "io/stream.h"

#include <chrono>
#include <cstdint>

namespace GameWIP::FileSystem
{
    namespace Types
    {
        /// @brief Portable wall-clock representation used for filesystem timestamps.
        /// @note Native precision may be reduced; an unrepresentable value is reported as SizeLimitExceeded.
        using FileTime = std::chrono::system_clock::time_point;

        /// @brief Kind of existing filesystem entry.
        enum class EntryKind
        {
            /// @brief A regular file.
            RegularFile,
            /// @brief A directory.
            Directory,
            /// @brief A symbolic link or equivalent link-like entry.
            Symlink,
            /// @brief An existing entry without a more specific portable kind.
            Other
        };

        /// @brief Destination replacement policy.
        enum class ReplaceMode
        {
            /// @brief Fail when the destination already exists.
            FailIfExists,
            /// @brief Replace an existing destination where supported.
            ReplaceExisting
        };

        /// @brief Symbolic-link resolution policy.
        enum class SymlinkPolicy
        {
            /// @brief Reject intermediate symlinks and do not dereference a final symlink.
            DoNotFollow,
            /// @brief Reject intermediate symlinks but follow a final symlink.
            FollowFinal,
            /// @brief Allow symlink resolution through the complete path.
            FollowAll
        };

        /// @brief Options shared by entry queries and simple metadata mutation.
        struct EntryOptions
        {
            /// @brief Symlink traversal policy used by the operation.
            SymlinkPolicy symlinkPolicy = SymlinkPolicy::DoNotFollow;
        };

        /// @brief Portable metadata for an existing filesystem entry.
        struct EntryInfo
        {
            /// @brief Portable entry kind.
            EntryKind kind = EntryKind::Other;
            /// @brief Entry size in bytes when hasSize is true.
            std::uint64_t sizeBytes = 0;
            /// @brief Whether sizeBytes is available and meaningful.
            bool hasSize = false;
            /// @brief Last-write time when hasLastWriteTime is true.
            FileTime lastWriteTime{};
            /// @brief Whether lastWriteTime is available and meaningful.
            bool hasLastWriteTime = false;
            /// @brief Portable basic read-only state.
            /// @note This is not a complete permissions, ownership, or ACL model.
            bool readOnly = false;
        };

        /// @brief Result returned by getEntryInfo().
        struct EntryInfoResult
        {
            /// @brief Operation status.
            IO::Types::Status status;
            /// @brief Entry metadata when status is successful.
            EntryInfo info{};
        };

        /// @brief Result returned by getLastWriteTime().
        struct LastWriteTimeResult
        {
            /// @brief Operation status.
            IO::Types::Status status;
            /// @brief Last-write time when status is successful.
            FileTime time{};
        };

        /// @brief Options used by removeFile() and removeEmptyDirectory().
        struct RemoveOptions
        {
            /// @brief Treat a missing target as success.
            bool succeedIfMissing = false;
            /// @brief Symlink traversal policy used to select the entry to remove.
            SymlinkPolicy symlinkPolicy = SymlinkPolicy::DoNotFollow;
        };

        /// @brief Options used by movePath().
        struct MoveOptions
        {
            /// @brief Destination replacement behavior.
            ReplaceMode replaceMode = ReplaceMode::FailIfExists;
            /// @brief Symlink traversal policy used for source resolution and destination parent traversal.
            SymlinkPolicy symlinkPolicy = SymlinkPolicy::DoNotFollow;
            /// @brief Create missing destination parent directories before moving.
            bool createParentDirectories = false;
        };
    } // namespace Types

    /// @brief Tests whether a filesystem entry exists.
    /// @param path Path to query.
    /// @param options Symlink traversal behavior.
    /// @return Successful true or false; missing entries produce successful false.
    [[nodiscard]] Types::BoolResult exists(const Types::Path &path, const Types::EntryOptions &options = {}) noexcept;

    /// @brief Returns portable metadata for an existing filesystem entry.
    /// @param path Path to query.
    /// @param options Symlink traversal behavior.
    /// @return Entry metadata, or NotFound when the entry is missing.
    [[nodiscard]] Types::EntryInfoResult getEntryInfo(const Types::Path &path, const Types::EntryOptions &options = {}) noexcept;

    /// @brief Tests whether a path exists and is a regular file.
    /// @param path Path to query.
    /// @param options Symlink traversal behavior.
    /// @return Successful true or false; missing entries produce successful false.
    [[nodiscard]] Types::BoolResult isRegularFile(const Types::Path &path, const Types::EntryOptions &options = {}) noexcept;

    /// @brief Tests whether a path exists and is a directory.
    /// @param path Path to query.
    /// @param options Symlink traversal behavior.
    /// @return Successful true or false; missing entries produce successful false.
    [[nodiscard]] Types::BoolResult isDirectory(const Types::Path &path, const Types::EntryOptions &options = {}) noexcept;

    /// @brief Tests whether a path exists and is a symbolic link or equivalent link-like entry.
    /// @param path Path to query.
    /// @param options Symlink traversal behavior.
    /// @return Successful true or false; missing entries produce successful false.
    [[nodiscard]] Types::BoolResult isSymlink(const Types::Path &path, const Types::EntryOptions &options = {}) noexcept;

    /// @brief Returns the size of an existing regular file.
    /// @param path File path to query.
    /// @param options Symlink traversal behavior.
    /// @return File size, NotFound when missing, or InvalidArgument when the resolved entry is not a regular file with portable size.
    [[nodiscard]] IO::Types::SizeResult getFileSize(const Types::Path &path, const Types::EntryOptions &options = {}) noexcept;

    /// @brief Returns the last-write time of an existing filesystem entry.
    /// @param path Path to query.
    /// @param options Symlink traversal behavior.
    /// @return Last-write time, or NotFound when the entry is missing.
    [[nodiscard]] Types::LastWriteTimeResult getLastWriteTime(const Types::Path &path, const Types::EntryOptions &options = {}) noexcept;

    /// @brief Returns the portable basic read-only state of an existing entry.
    /// @param path Path to query.
    /// @param options Symlink traversal behavior.
    /// @return Successful true or false, or NotFound when the entry is missing.
    [[nodiscard]] Types::BoolResult isReadOnly(const Types::Path &path, const Types::EntryOptions &options = {}) noexcept;

    /// @brief Changes the portable read-only state of an existing entry.
    /// @param path Path to update.
    /// @param readOnly True to request read-only state; false to request writable state.
    /// @param options Symlink traversal behavior.
    /// @return Success or a validation, lookup, permission, or metadata failure status.
    [[nodiscard]] IO::Types::Status setReadOnly(const Types::Path &path, bool readOnly, const Types::EntryOptions &options = {}) noexcept;

    /// @brief Moves or renames one filesystem entry.
    /// @param from Source path.
    /// @param to Destination path.
    /// @param options Replacement, symlink, and parent creation behavior.
    /// @return Success or a validation, lookup, permission, conflict, or move failure status.
    /// @note Cross-volume moves return MoveFailed; no copy-and-delete fallback is performed.
    /// @note Native rename success is the operation's linearization point; later namespace changes do not change the returned success.
    [[nodiscard]] IO::Types::Status movePath(const Types::Path &from, const Types::Path &to, const Types::MoveOptions &options = {}) noexcept;
} // namespace GameWIP::FileSystem
