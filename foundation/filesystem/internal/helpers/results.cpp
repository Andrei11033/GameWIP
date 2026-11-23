/// @file results.cpp
/// @brief Shared FileSystem failure-result helpers.

#include "filesystem/internal/helpers.h"

#include <utility>

namespace GameWIP::FileSystem::Internal
{
    /// ------------------------------------------------------------
    /// Predicate and size failures
    /// ------------------------------------------------------------

    Types::BoolResult boolFailure(IO::Types::Status status) noexcept
    {
        return {.status = std::move(status), .value = false};
    }

    Types::BoolResult boolFailure(ErrorCode code) noexcept
    {
        return {.status = IO::makeStatus(code), .value = false};
    }

    IO::Types::SizeResult sizeFailure(IO::Types::Status status) noexcept
    {
        return {.status = std::move(status), .sizeBytes = 0};
    }

    IO::Types::SizeResult sizeFailure(ErrorCode code) noexcept
    {
        return {.status = IO::makeStatus(code), .sizeBytes = 0};
    }

    /// ------------------------------------------------------------
    /// Write failures
    /// ------------------------------------------------------------

    IO::Types::WriteResult writeFailure(IO::Types::Status status, std::size_t bytesWritten) noexcept
    {
        return {.status = std::move(status), .bytesWritten = bytesWritten};
    }

    IO::Types::WriteResult writeFailure(ErrorCode code, std::size_t bytesWritten) noexcept
    {
        return {.status = IO::makeStatus(code), .bytesWritten = bytesWritten};
    }
} // namespace GameWIP::FileSystem::Internal
