/// @file status.h
/// @brief Private Terminal exception-to-status mapping.

#pragma once

#include "io/status.h"

#include <format>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>

namespace GameWIP::Terminal::Internal
{
    /// @brief Builds a diagnostic status without letting allocation failures escape a checked boundary.
    [[nodiscard]] inline IO::Types::Status statusWithMessage(IO::Types::ErrorCode code, std::string_view message) noexcept
    {
        if(message.empty())
        {
            return IO::makeStatus(code);
        }

        try
        {
            return IO::makeStatus(code, 0, std::string(message));
        }
        catch(...)
        {
            return IO::makeStatus(code);
        }
    }

    [[nodiscard]] inline IO::Types::Status exceptionStatus() noexcept
    {
        try
        {
            throw;
        }
        catch(const std::bad_alloc &)
        {
            return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
        }
        catch(const std::length_error &)
        {
            return IO::makeStatus(IO::Types::ErrorCode::SizeLimitExceeded);
        }
        catch(const std::format_error &)
        {
            return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
        }
        catch(...)
        {
            return IO::makeStatus(IO::Types::ErrorCode::Unknown);
        }
    }
} // namespace GameWIP::Terminal::Internal
