/// @file directory.h
/// @brief Public directory enumeration and mutation contracts for GameWIP FileSystem.

#pragma once

#include "filesystem/entry.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace GameWIP::FileSystem
{
    /// @brief Sentinel entry limit meaning no caller-imposed listing or tree-removal limit.
    /// @note Backend, container, address-space, and resource limits still apply.
    inline constexpr std::uint64_t kNoEntryLimit = std::numeric_limits<std::uint64_t>::max();

    namespace Detail
    {
        struct DirectoryCursorState;
    } // namespace Detail

    namespace Types
    {
        /// @brief Directory creation, enumeration, and tree-removal values.
        namespace Directory
        {
            /// @brief Options used by createDirectory() and createDirectories().
            struct CreateOptions
            {
                /// @brief Treat an already-existing directory as success.
                bool succeedIfAlreadyExists = true;
                /// @brief Symlink traversal policy used while resolving the path.
                SymlinkPolicy symlinkPolicy = SymlinkPolicy::DoNotFollow;
            };

            /// @brief Options used by listDirectory().
            struct ListOptions
            {
                /// @brief Include regular files.
                bool includeFiles = true;
                /// @brief Include directories.
                bool includeDirectories = true;
                /// @brief Include symbolic links or equivalent link-like entries.
                bool includeSymlinks = true;
                /// @brief Include existing entries without another portable kind.
                bool includeOther = true;
                /// @brief Include entries considered hidden by the backend.
                bool includeHidden = true;
                /// @brief Symlink policy used when obtaining child metadata.
                SymlinkPolicy symlinkPolicy = SymlinkPolicy::DoNotFollow;
                /// @brief Maximum number of returned entries, or kNoEntryLimit for no caller limit.
                std::uint64_t maxEntries = kNoEntryLimit;
            };

            /// @brief One direct child returned by listDirectory().
            struct Entry
            {
                /// @brief Supplied parent path joined with the child name.
                /// @note A relative parent produces a relative child path; this field is not necessarily absolute.
                Path path;
                /// @brief Portable metadata for the child.
                EntryInfo info{};
            };

            /// @brief Result returned by listDirectory().
            /// @details SizeLimitExceeded may return entries collected before the limit was reached. The result owns all
            /// accepted entries, so retained storage is proportional to their count and path sizes.
            struct ListResult
            {
                /// @brief Operation status.
                IO::Types::Status status;
                /// @brief Direct children collected before completion or failure.
                std::vector<Entry> entries;
            };

            /// @brief Result returned by one DirectoryCursor step.
            /// @details Successful exhaustion is represented by hasEntry=false. A failed result does not contain an entry.
            struct CursorNextResult
            {
                /// @brief Operation status.
                IO::Types::Status status;
                /// @brief Direct child returned when hasEntry is true.
                Entry entry;
                /// @brief Whether this step returned one accepted child.
                bool hasEntry = false;
            };
        } // namespace Directory

        namespace Directory
        {
            /// @brief Options used by removeDirectoryTree().
            struct RemoveTreeOptions
            {
                /// @brief Treat a missing target as success.
                bool succeedIfMissing = false;
                /// @brief Symlink traversal policy used only for the initial path.
                SymlinkPolicy symlinkPolicy = SymlinkPolicy::DoNotFollow;
                /// @brief Maximum number of removed entries, or kNoEntryLimit for no caller limit.
                std::uint64_t maxEntries = kNoEntryLimit;
            };

            /// @brief Result returned by removeDirectoryTree().
            /// @details SizeLimitExceeded or another failure may report entries removed before the failure.
            struct RemoveTreeResult
            {
                /// @brief Operation status.
                IO::Types::Status status;
                /// @brief Number of entries removed before completion or failure.
                std::uint64_t removedEntries = 0;
            };
        } // namespace Directory
    } // namespace Types

    /// @name Directory enumeration
    /// @{

    /// @brief Move-only, bounded-memory cursor over direct directory children.
    /// @details The cursor owns backend enumeration state and applies the same filters, symlink policy, native ordering,
    /// and maximum-entry contract as listDirectory(). Retained memory is independent of the number of sibling entries.
    /// A cursor object is not safe for concurrent calls.
    class DirectoryCursor final
    {
    public:
        /// @brief Creates a closed cursor.
        DirectoryCursor() noexcept;
        /// @brief Directory cursors are not copy-constructible.
        DirectoryCursor(const DirectoryCursor &) = delete;
        /// @brief Directory cursors are not copy-assignable.
        DirectoryCursor &operator=(const DirectoryCursor &) = delete;
        /// @brief Move-constructs a cursor and leaves the source closed.
        DirectoryCursor(DirectoryCursor &&other) noexcept;
        /// @brief Move-assigns a cursor, closing any enumeration previously owned by this object.
        DirectoryCursor &operator=(DirectoryCursor &&other) noexcept;
        /// @brief Closes an active enumeration without throwing.
        ~DirectoryCursor() noexcept;

        /// @brief Opens a direct-child enumeration. Failure leaves this cursor closed.
        /// @param path Directory path to enumerate.
        /// @param options Filtering, symlink, hidden-entry, and entry-limit behavior.
        /// @return Success, AlreadyOpen, or a validation/open failure status.
        [[nodiscard]] IO::Types::Status open(const Types::Path &path, const Types::Directory::ListOptions &options = {}) noexcept;
        /// @brief Returns whether this object owns an active enumeration.
        [[nodiscard]] bool isOpen() const noexcept;
        /// @brief Returns the next accepted child or successful exhaustion.
        /// @return One entry, successful exhaustion, NotOpen, SizeLimitExceeded, or an enumeration failure.
        [[nodiscard]] Types::Directory::CursorNextResult next() noexcept;
        /// @brief Closes the enumeration. Repeated close calls succeed.
        [[nodiscard]] IO::Types::Status close() noexcept;

    private:
        std::unique_ptr<Detail::DirectoryCursorState> state_;
        Types::Directory::ListOptions options_{};
        std::uint64_t emittedEntries_ = 0;
        bool limitReached_ = false;
    };

    /// @}

    // ------------------------------------------------------------
    // Directory operations
    // ------------------------------------------------------------

    /// @name Directory operations
    /// @{

    /// @brief Creates one directory level.
    /// @param path Directory path to create.
    /// @param options Existing-directory and symlink traversal behavior.
    /// @return Success or a validation, conflict, permission, or directory-creation failure status.
    [[nodiscard]] IO::Types::Status createDirectory(const Types::Path &path, const Types::Directory::CreateOptions &options = {}) noexcept;

    /// @brief Creates a directory and any missing parent directories.
    /// @param path Directory path to create.
    /// @param options Existing-directory and symlink traversal behavior.
    /// @return Success or a validation, conflict, permission, or directory-creation failure status.
    [[nodiscard]] IO::Types::Status createDirectories(const Types::Path &path, const Types::Directory::CreateOptions &options = {}) noexcept;

    /// @brief Lists direct children of a directory in backend/native order.
    /// @param path Directory path to enumerate.
    /// @param options Filtering, symlink, hidden-entry, and entry-limit behavior.
    /// @return Collected child entries and final status.
    [[nodiscard]] Types::Directory::ListResult listDirectory(const Types::Path &path, const Types::Directory::ListOptions &options = {}) noexcept;

    /// @brief Removes one empty directory.
    /// @param path Directory path to remove.
    /// @param options Missing-target and symlink behavior.
    /// @return Success, DirectoryNotEmpty, or another validation, lookup, type, permission, or removal failure status.
    [[nodiscard]] IO::Types::Status removeEmptyDirectory(const Types::Path &path, const Types::RemoveOptions &options = {}) noexcept;

    /// @brief Recursively removes a directory tree without following discovered symlinked directories.
    /// @param path Root directory path to remove.
    /// @param options Missing-target, initial symlink, and entry-limit behavior.
    /// @return Final status and number of entries removed before completion or failure.
    [[nodiscard]] Types::Directory::RemoveTreeResult removeDirectoryTree(
        const Types::Path &path,
        const Types::Directory::RemoveTreeOptions &options = {}) noexcept;

    /// @}
} // namespace GameWIP::FileSystem
