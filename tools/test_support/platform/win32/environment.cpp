/// @file environment.cpp
/// @brief Windows environment-variable backend for the TestSupport library.

#include "test_support/platform/backend.h"
#include "test_support/internal/status.h"
#include "test_support/platform/win32/internal/text.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
    /// ------------------------------------------------------------
    /// Environment helpers
    /// ------------------------------------------------------------

    using GameWIP::TestSupport::Internal::failureStatus;

    void validateEnvironmentName(std::string_view name)
    {
        if(name.empty() || name.contains('='))
        {
            throw std::invalid_argument("Environment variable name must be non-empty and cannot contain '='");
        }
    }

    /// ------------------------------------------------------------
    /// Environment mutation
    /// ------------------------------------------------------------

    [[nodiscard]] GameWIP::Status::Types::Result<void> writeEnvironmentVariable(std::string_view name, std::string_view value) noexcept
    {
        try
        {
            validateEnvironmentName(name);

            const std::wstring nameWide = GameWIP::TestSupport::Internal::Win32::utf8ToWide(name);
            const std::wstring valueWide = GameWIP::TestSupport::Internal::Win32::utf8ToWide(value);
            const errno_t error = _wputenv_s(nameWide.c_str(), valueWide.c_str());

            if(error != 0)
            {
                using GameWIP::Status::Types::NativeCode;
                using GameWIP::Status::Types::NativeCodeDomain;
                return GameWIP::Status::Types::Result<void>::failure(
                    failureStatus(GameWIP::Status::Types::Code::EnvironmentFailed, NativeCode{NativeCodeDomain::Errno, error}));
            }

            return GameWIP::Status::Types::Result<void>::success();
        }
        catch(const std::bad_alloc &)
        {
            return GameWIP::Status::Types::Result<void>::failure(failureStatus(GameWIP::Status::Types::Code::OutOfMemory));
        }
        catch(const std::invalid_argument &)
        {
            return GameWIP::Status::Types::Result<void>::failure(failureStatus(GameWIP::Status::Types::Code::InvalidArgument));
        }
        catch(const std::length_error &)
        {
            return GameWIP::Status::Types::Result<void>::failure(failureStatus(GameWIP::Status::Types::Code::InvalidArgument));
        }
        catch(...)
        {
            return GameWIP::Status::Types::Result<void>::failure(failureStatus(GameWIP::Status::Types::Code::EnvironmentFailed));
        }
    }
} // namespace

namespace GameWIP::TestSupport::Platform
{
    /// ------------------------------------------------------------
    /// Environment variable operations
    /// ------------------------------------------------------------

    EnvironmentReadResult readEnvironmentVariable(std::string_view name) noexcept
    {
        try
        {
            validateEnvironmentName(name);

            const std::wstring nameText = GameWIP::TestSupport::Internal::Win32::utf8ToWide(name);

            // GetEnvironmentVariableW reports missing variables through GetLastError(),
            // so clear the thread error before the zero-size probe.
            SetLastError(ERROR_SUCCESS);
            const DWORD requiredSize = GetEnvironmentVariableW(nameText.c_str(), nullptr, 0);

            if(requiredSize == 0)
            {
                const DWORD readError = GetLastError();

                if(readError == ERROR_ENVVAR_NOT_FOUND)
                {
                    return EnvironmentReadResult::success(std::nullopt);
                }

                if(readError == ERROR_SUCCESS)
                {
                    return EnvironmentReadResult::success(std::string{});
                }

                using GameWIP::Status::Types::NativeCode;
                using GameWIP::Status::Types::NativeCodeDomain;
                return EnvironmentReadResult::failure(Internal::failureStatus(
                    GameWIP::Status::Types::Code::EnvironmentFailed,
                    NativeCode{NativeCodeDomain::Win32Error, readError}));
            }

            std::wstring value(requiredSize, L'\0');
            SetLastError(ERROR_SUCCESS);
            const DWORD copied = GetEnvironmentVariableW(nameText.c_str(), value.data(), requiredSize);

            if(copied == 0 || copied >= requiredSize)
            {
                using GameWIP::Status::Types::NativeCode;
                using GameWIP::Status::Types::NativeCodeDomain;
                return EnvironmentReadResult::failure(Internal::failureStatus(
                    GameWIP::Status::Types::Code::EnvironmentFailed,
                    NativeCode{NativeCodeDomain::Win32Error, GetLastError()}));
            }

            value.resize(copied);
            return EnvironmentReadResult::success(GameWIP::TestSupport::Internal::Win32::wideToUtf8(value));
        }
        catch(const std::bad_alloc &)
        {
            return EnvironmentReadResult::failure(Internal::failureStatus(GameWIP::Status::Types::Code::OutOfMemory));
        }
        catch(const std::invalid_argument &)
        {
            return EnvironmentReadResult::failure(Internal::failureStatus(GameWIP::Status::Types::Code::InvalidArgument));
        }
        catch(const std::length_error &)
        {
            return EnvironmentReadResult::failure(Internal::failureStatus(GameWIP::Status::Types::Code::InvalidArgument));
        }
        catch(...)
        {
            return EnvironmentReadResult::failure(Internal::failureStatus(GameWIP::Status::Types::Code::EnvironmentFailed));
        }
    }

    Types::Result<void> setEnvironmentVariableValue(std::string_view name, std::string_view value) noexcept
    {
        return writeEnvironmentVariable(name, value);
    }

    Types::Result<void> unsetEnvironmentVariableValue(std::string_view name) noexcept
    {
        return writeEnvironmentVariable(name, std::string_view{});
    }
} // namespace GameWIP::TestSupport::Platform
