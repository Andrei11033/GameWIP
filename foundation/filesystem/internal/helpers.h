/// @file helpers.h
/// @brief Private helper declarations shared by FileSystem modules.

#pragma once

#include "filesystem/filesystem.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace GameWIP::FileSystem::Internal
{
    using ErrorCode = GameWIP::Status::Types::Code;

    /// ------------------------------------------------------------
    /// Shared result helpers
    /// ------------------------------------------------------------

    [[nodiscard]] GameWIP::Status::Types::Result<bool> boolFailure(GameWIP::Status::Types::Status status) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Result<bool> boolFailure(ErrorCode code) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Result<std::uint64_t> sizeFailure(GameWIP::Status::Types::Status status) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Result<std::uint64_t> sizeFailure(ErrorCode code) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Result<IO::Types::WriteProgress>
    writeFailure(GameWIP::Status::Types::Status status, std::size_t bytesWritten = 0) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Result<IO::Types::WriteProgress> writeFailure(ErrorCode code, std::size_t bytesWritten = 0) noexcept;

    /// ------------------------------------------------------------
    /// Shared request helpers
    /// ------------------------------------------------------------

    [[nodiscard]] bool validUtf8(std::string_view text) noexcept;
    [[nodiscard]] Types::Path pathFromTrustedUtf8(std::string_view utf8Path);
    [[nodiscard]] GameWIP::Status::Types::Result<Types::EntryInfo>
    queryEntry(const Types::Path &path, Types::SymlinkPolicy symlinkPolicy);
    [[nodiscard]] bool isValidReplaceMode(Types::ReplaceMode mode) noexcept;
    [[nodiscard]] bool isValidFileShare(Types::File::Share share) noexcept;
    [[nodiscard]] bool isValidSymlinkPolicy(Types::SymlinkPolicy policy) noexcept;
    [[nodiscard]] bool includeEntryKind(Types::EntryKind kind, const Types::Directory::ListOptions &options) noexcept;
    [[nodiscard]] bool finalEntryIsSymlink(const Types::Path &path) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Result<void>
    validateParentDirectory(const Types::Path &path, bool createMissing, Types::SymlinkPolicy symlinkPolicy) noexcept;
} // namespace GameWIP::FileSystem::Internal
