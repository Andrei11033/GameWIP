/// @file terminal_status.h
/// @brief Private Terminal exception-to-status mapping.

#pragma once

#include "io/status.h"

#include <format>
#include <new>
#include <stdexcept>

namespace GameWIP::Terminal::Detail
{
    [[nodiscard]] inline IO::Types::Status exceptionStatus() noexcept
    {
        try
        {
            throw;
        }
        catch (const std::bad_alloc &)
        {
            return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
        }
        catch (const std::length_error &)
        {
            return IO::makeStatus(IO::Types::ErrorCode::SizeLimitExceeded);
        }
        catch (const std::format_error &)
        {
            return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
        }
        catch (...)
        {
            return IO::makeStatus(IO::Types::ErrorCode::Unknown);
        }
    }
} // namespace GameWIP::Terminal::Detail
