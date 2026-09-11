/// @file win32_environment.cpp
/// @brief Windows environment-variable backend for the TestSupport library.

#include "test_support/internal/test_support_platform.h"
#include "test_support/internal/test_support_test_hooks.h"
#include "test_support/internal/win32_text.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdlib>
#include <new>
#include <stdexcept>
#include <string>

namespace
{
    // ------------------------------------------------------------
    // Status and validation helpers
    // ------------------------------------------------------------

    [[nodiscard]] GameWIP::TestSupport::Types::InfrastructureStatus environmentFailure(
        GameWIP::TestSupport::Types::InfrastructureError error,
        std::uint64_t nativeCode = 0) noexcept
    {
        return {.error = error, .nativeCode = nativeCode};
    }

    void validateEnvironmentName(std::string_view name)
    {
        if (name.empty() || name.contains('='))
        {
            throw std::invalid_argument("Environment variable name must be non-empty and cannot contain '='");
        }
    }
} // namespace

namespace GameWIP::TestSupport::Detail::Platform
{
    // ------------------------------------------------------------
    // Environment variable operations
    // ------------------------------------------------------------

    EnvironmentReadResult readEnvironmentVariable(std::string_view name) noexcept
    {
        EnvironmentReadResult result;
#if TEST_SUPPORT_INTERNAL_TEST_HOOKS
        if (const auto injected = ::GameWIP::TestSupport::Detail::TestHooks::consumeEnvironmentFailure(
                ::GameWIP::TestSupport::TestHooks::EnvironmentFailurePoint::Read))
        {
            result.status = environmentFailure(Types::InfrastructureError::EnvironmentFailed, *injected);
            return result;
        }
#endif
        try
        {
            validateEnvironmentName(name);
            const std::wstring nameText = GameWIP::TestSupport::Detail::Win32::utf8ToWide(name);

            // GetEnvironmentVariableW reports missing variables through GetLastError(),
            // so clear the thread error before the zero-size probe.
            SetLastError(ERROR_SUCCESS);
            const DWORD requiredSize = GetEnvironmentVariableW(nameText.c_str(), nullptr, 0);
            if (requiredSize == 0)
            {
                const DWORD readError = GetLastError();
                if (readError == ERROR_ENVVAR_NOT_FOUND)
                {
                    return result;
                }
                if (readError == ERROR_SUCCESS)
                {
                    result.value = std::string{};
                    return result;
                }
                result.status = environmentFailure(Types::InfrastructureError::EnvironmentFailed, readError);
                return result;
            }

            std::wstring value(requiredSize, L'\0');
            SetLastError(ERROR_SUCCESS);
            const DWORD copied = GetEnvironmentVariableW(nameText.c_str(), value.data(), requiredSize);
            if (copied == 0 || copied >= requiredSize)
            {
                result.status = environmentFailure(Types::InfrastructureError::EnvironmentFailed, GetLastError());
                return result;
            }

            value.resize(copied);
            result.value = GameWIP::TestSupport::Detail::Win32::wideToUtf8(value);
            return result;
        }
        catch (const std::bad_alloc &)
        {
            result.status = environmentFailure(Types::InfrastructureError::OutOfMemory);
            return result;
        }
        catch (const std::invalid_argument &)
        {
            result.status = environmentFailure(Types::InfrastructureError::InvalidArgument);
            return result;
        }
        catch (const std::length_error &)
        {
            result.status = environmentFailure(Types::InfrastructureError::InvalidArgument);
            return result;
        }
        catch (...)
        {
            result.status = environmentFailure(Types::InfrastructureError::EnvironmentFailed);
            return result;
        }
    }

    Types::InfrastructureStatus setEnvironmentVariableValue(std::string_view name, std::string_view value) noexcept
    {
#if TEST_SUPPORT_INTERNAL_TEST_HOOKS
        if (const auto injected =
                ::GameWIP::TestSupport::Detail::TestHooks::consumeEnvironmentFailure(::GameWIP::TestSupport::TestHooks::EnvironmentFailurePoint::Set))
        {
            return environmentFailure(Types::InfrastructureError::EnvironmentFailed, *injected);
        }
#endif
        try
        {
            validateEnvironmentName(name);
            const std::wstring nameWide = GameWIP::TestSupport::Detail::Win32::utf8ToWide(name);
            const std::wstring valueWide = GameWIP::TestSupport::Detail::Win32::utf8ToWide(value);
            const errno_t error = _wputenv_s(nameWide.c_str(), valueWide.c_str());
            return error == 0 ? Types::InfrastructureStatus{}
                              : environmentFailure(Types::InfrastructureError::EnvironmentFailed, static_cast<std::uint64_t>(error));
        }
        catch (const std::bad_alloc &)
        {
            return environmentFailure(Types::InfrastructureError::OutOfMemory);
        }
        catch (const std::invalid_argument &)
        {
            return environmentFailure(Types::InfrastructureError::InvalidArgument);
        }
        catch (const std::length_error &)
        {
            return environmentFailure(Types::InfrastructureError::InvalidArgument);
        }
        catch (...)
        {
            return environmentFailure(Types::InfrastructureError::EnvironmentFailed);
        }
    }

    Types::InfrastructureStatus unsetEnvironmentVariableValue(std::string_view name) noexcept
    {
#if TEST_SUPPORT_INTERNAL_TEST_HOOKS
        if (const auto injected = ::GameWIP::TestSupport::Detail::TestHooks::consumeEnvironmentFailure(
                ::GameWIP::TestSupport::TestHooks::EnvironmentFailurePoint::Unset))
        {
            return environmentFailure(Types::InfrastructureError::EnvironmentFailed, *injected);
        }
#endif
        try
        {
            validateEnvironmentName(name);
            const std::wstring nameWide = GameWIP::TestSupport::Detail::Win32::utf8ToWide(name);
            const errno_t error = _wputenv_s(nameWide.c_str(), L"");
            return error == 0 ? Types::InfrastructureStatus{}
                              : environmentFailure(Types::InfrastructureError::EnvironmentFailed, static_cast<std::uint64_t>(error));
        }
        catch (const std::bad_alloc &)
        {
            return environmentFailure(Types::InfrastructureError::OutOfMemory);
        }
        catch (const std::invalid_argument &)
        {
            return environmentFailure(Types::InfrastructureError::InvalidArgument);
        }
        catch (const std::length_error &)
        {
            return environmentFailure(Types::InfrastructureError::InvalidArgument);
        }
        catch (...)
        {
            return environmentFailure(Types::InfrastructureError::EnvironmentFailed);
        }
    }
} // namespace GameWIP::TestSupport::Detail::Platform
