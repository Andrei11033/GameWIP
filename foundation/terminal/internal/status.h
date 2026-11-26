/// @file status.h
/// @brief Private Terminal exception-to-status mapping.

#pragma once

#include "status/status.h"

#include <cstdint>
#include <format>
#include <new>
#include <stdexcept>
#include <string_view>

namespace GameWIP::Terminal::Internal
{
    /// @brief Builds a diagnostic status without letting allocation failures escape a checked boundary.
    using Status = GameWIP::Status::Types::Status;
    using StatusCode = GameWIP::Status::Types::Code;

    [[nodiscard]] inline Status statusWithMessage(StatusCode code, std::string_view message) noexcept
    {
        return message.empty() ? Status(code) : Status::withDiagnostic(code, message);
    }

    [[nodiscard]] inline Status backendStatus(StatusCode code, std::int64_t nativeCode = 0, std::string_view message = {}) noexcept
    {
        if(nativeCode != 0)
        {
            using GameWIP::Status::Types::NativeCode;
            using GameWIP::Status::Types::NativeCodeDomain;

            return Status::withDiagnostic(code, NativeCode{NativeCodeDomain::Other, nativeCode}, message);
        }

        return statusWithMessage(code, message);
    }

    [[nodiscard]] inline Status exceptionStatus() noexcept
    {
        try
        {
            throw;
        }
        catch(const std::bad_alloc &)
        {
            return Status(StatusCode::OutOfMemory);
        }
        catch(const std::length_error &)
        {
            return Status(StatusCode::SizeLimitExceeded);
        }
        catch(const std::format_error &)
        {
            return Status(StatusCode::InvalidArgument);
        }
        catch(...)
        {
            return Status(StatusCode::Unknown);
        }
    }
} // namespace GameWIP::Terminal::Internal
