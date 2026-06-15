#pragma once

/// @file filesystem_platform.h
/// @brief Internal platform abstraction used by the FileSystem library.

#include "filesystem/filesystem.h"

namespace GameWIP::FileSystem::Detail::Platform
{
    /// @brief Result returned by backend entry metadata queries.
    struct EntryQueryResult
    {
        /// @brief Success or portable/backend-native failure status.
        IO::Types::Status status;
        /// @brief Entry metadata when status is successful.
        Types::EntryInfo info{};
    };

    /// @brief Queries one existing entry using backend-native filesystem semantics.
    /// @details FollowAll follows normal platform path resolution. DoNotFollow and FollowFinal must reject intermediate symlinks
    /// during handle-relative traversal rather than checking a path and reopening it later.
    /// @param path Entry path to inspect.
    /// @param symlinkPolicy Symlink policy requested by the public operation.
    /// @return Entry metadata, InvalidArgument for invalid policy, or a portable/backend-native failure status.
    [[nodiscard]] EntryQueryResult queryEntry(const Types::Path &path, Types::SymlinkPolicy symlinkPolicy) noexcept;
} // namespace GameWIP::FileSystem::Detail::Platform
