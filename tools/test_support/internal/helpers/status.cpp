/// @file status.cpp
/// @brief Shared private infrastructure-status helpers for TestSupport.

#include "test_support/internal/status.h"

#include <optional>
#include <system_error>

namespace GameWIP::TestSupport::Internal
{
    /// ------------------------------------------------------------
    /// Infrastructure status construction
    /// ------------------------------------------------------------

    using GameWIP::Status::Types::Code;
    using GameWIP::Status::Types::NativeCode;
    using GameWIP::Status::Types::NativeCodeDomain;
    using GameWIP::Status::Types::Status;

    Status failureStatus(Code code, std::optional<NativeCode> nativeCode) noexcept
    {
        return nativeCode.has_value() ? Status::withDiagnostic(code, *nativeCode) : Status(code);
    }

    std::optional<NativeCode> nativeCode(const std::error_code &error) noexcept
    {
        if(!error)
        {
            return std::nullopt;
        }

        NativeCodeDomain domain = NativeCodeDomain::Other;
        if(&error.category() == &std::system_category())
        {
#if defined(_WIN32)
            domain = NativeCodeDomain::Win32Error;
#else
            domain = NativeCodeDomain::Errno;
#endif
        }
        else if(&error.category() == &std::generic_category())
        {
            domain = NativeCodeDomain::Errno;
        }

        return NativeCode{domain, error.value()};
    }

    std::optional<NativeCode> errnoCode(int value) noexcept
    {
        return value == 0 ? std::nullopt : std::optional<NativeCode>(NativeCode{NativeCodeDomain::Errno, value});
    }
} // namespace GameWIP::TestSupport::Internal
