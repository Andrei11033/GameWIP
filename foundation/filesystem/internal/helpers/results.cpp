/// @file results.cpp
/// @brief Shared FileSystem failure-result helpers.

#include "filesystem/internal/helpers.h"

#include <utility>

namespace GameWIP::FileSystem::Internal
{
    /// ------------------------------------------------------------
    /// Predicate and size failures
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Result<bool> boolFailure(GameWIP::Status::Types::Status status) noexcept
    {
        return GameWIP::Status::Types::Result<bool>::failure(std::move(status));
    }

    GameWIP::Status::Types::Result<bool> boolFailure(ErrorCode code) noexcept
    {
        return boolFailure(GameWIP::Status::Types::Status(code));
    }

    GameWIP::Status::Types::Result<std::uint64_t> sizeFailure(GameWIP::Status::Types::Status status) noexcept
    {
        return GameWIP::Status::Types::Result<std::uint64_t>::failure(std::move(status));
    }

    GameWIP::Status::Types::Result<std::uint64_t> sizeFailure(ErrorCode code) noexcept
    {
        return sizeFailure(GameWIP::Status::Types::Status(code));
    }

    /// ------------------------------------------------------------
    /// Write failures
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Result<IO::Types::WriteProgress>
    writeFailure(GameWIP::Status::Types::Status status, std::size_t bytesWritten) noexcept
    {
        return GameWIP::Status::Types::Result<IO::Types::WriteProgress>::failure(
            std::move(status), IO::Types::WriteProgress{.bytesWritten = bytesWritten});
    }

    GameWIP::Status::Types::Result<IO::Types::WriteProgress> writeFailure(ErrorCode code, std::size_t bytesWritten) noexcept
    {
        return writeFailure(GameWIP::Status::Types::Status(code), bytesWritten);
    }
} // namespace GameWIP::FileSystem::Internal
