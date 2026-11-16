/// @file helpers.h
/// @brief Private helper declarations shared by FileSystem modules.

#pragma once

#include "filesystem/filesystem.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace GameWIP::FileSystem::Detail
{
    using IO::Types::ErrorCode;

    namespace Platform
    {
        struct EntryQueryResult;
    } // namespace Platform

    /// ------------------------------------------------------------
    /// Shared result helpers
    /// ------------------------------------------------------------

    [[nodiscard]] Types::BoolResult boolFailure(IO::Types::Status status) noexcept;
    [[nodiscard]] Types::BoolResult boolFailure(ErrorCode code) noexcept;
    [[nodiscard]] IO::Types::SizeResult sizeFailure(IO::Types::Status status) noexcept;
    [[nodiscard]] IO::Types::SizeResult sizeFailure(ErrorCode code) noexcept;
    [[nodiscard]] IO::Types::WriteResult writeFailure(IO::Types::Status status, std::size_t bytesWritten = 0) noexcept;
    [[nodiscard]] IO::Types::WriteResult writeFailure(ErrorCode code, std::size_t bytesWritten = 0) noexcept;

    /// ------------------------------------------------------------
    /// Shared request helpers
    /// ------------------------------------------------------------

    [[nodiscard]] bool validUtf8(std::string_view text) noexcept;
    [[nodiscard]] Types::Path pathFromTrustedUtf8(std::string_view utf8Path);
    [[nodiscard]] Platform::EntryQueryResult queryEntry(const Types::Path &path, Types::SymlinkPolicy symlinkPolicy);
    [[nodiscard]] bool isValidReplaceMode(Types::ReplaceMode mode) noexcept;
    [[nodiscard]] bool isValidFileShare(Types::File::Share share) noexcept;
    [[nodiscard]] bool isValidSymlinkPolicy(Types::SymlinkPolicy policy) noexcept;
    [[nodiscard]] bool includeEntryKind(Types::EntryKind kind, const Types::Directory::ListOptions &options) noexcept;
    [[nodiscard]] bool finalEntryIsSymlink(const Types::Path &path) noexcept;
    [[nodiscard]] IO::Types::Status validateParentDirectory(const Types::Path &path, bool createMissing, Types::SymlinkPolicy symlinkPolicy) noexcept;
} // namespace GameWIP::FileSystem::Detail
