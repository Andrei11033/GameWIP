/// @file results.h
/// @brief Private Desktop status and result helpers.

#pragma once

#include "status/result.h"

#include <cstdint>
#include <string_view>
#include <utility>

namespace GameWIP::Desktop::Internal
{
    using GameWIP::Status::Types::Status;
    using StatusCode = GameWIP::Status::Types::Code;
    using GameWIP::Status::Types::NativeCode;
    using GameWIP::Status::Types::NativeCodeDomain;

    [[nodiscard]] inline Status statusFor(StatusCode code, NativeCode nativeCode, std::string_view message = {}) noexcept
    {
        return Status::withDiagnostic(code, nativeCode, message);
    }

    [[nodiscard]] inline Status statusFor(
        StatusCode code,
        std::int64_t nativeCode,
        NativeCodeDomain nativeCodeDomain,
        std::string_view message = {}) noexcept
    {
        return statusFor(code, NativeCode{nativeCodeDomain, nativeCode}, message);
    }

    [[nodiscard]] inline Status statusFor(StatusCode code, std::int64_t nativeCode = 0, std::string_view message = {}) noexcept
    {
        if(nativeCode != 0)
        {
            return Status::withDiagnostic(code, NativeCode{NativeCodeDomain::Other, nativeCode}, message);
        }
        if(!message.empty())
        {
            return Status::withDiagnostic(code, message);
        }
        return Status(code);
    }

    [[nodiscard]] inline GameWIP::Status::Types::Result<void> result(Status status) noexcept
    {
        return status.ok() ? GameWIP::Status::Types::Result<void>::success() : GameWIP::Status::Types::Result<void>::failure(std::move(status));
    }

    template <typename Value> [[nodiscard]] GameWIP::Status::Types::Result<Value> result(Status status, Value value)
    {
        return status.ok() ? GameWIP::Status::Types::Result<Value>::success(std::move(value))
                           : GameWIP::Status::Types::Result<Value>::failure(std::move(status), std::move(value));
    }

    template <typename Value> [[nodiscard]] GameWIP::Status::Types::Result<Value> resultWithPartial(Status status, Value value, bool retainPartial)
    {
        if(status.ok())
        {
            return GameWIP::Status::Types::Result<Value>::success(std::move(value));
        }
        return retainPartial ? GameWIP::Status::Types::Result<Value>::failure(std::move(status), std::move(value))
                             : GameWIP::Status::Types::Result<Value>::failure(std::move(status));
    }

} // namespace GameWIP::Desktop::Internal
