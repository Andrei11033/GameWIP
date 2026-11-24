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

    [[nodiscard]] GameWIP::TestSupport::Types::InfrastructureStatus writeEnvironmentVariable(std::string_view name, std::string_view value) noexcept
    {
        try
        {
            validateEnvironmentName(name);

            const std::wstring nameWide = GameWIP::TestSupport::Internal::Win32::utf8ToWide(name);
            const std::wstring valueWide = GameWIP::TestSupport::Internal::Win32::utf8ToWide(value);
            const errno_t error = _wputenv_s(nameWide.c_str(), valueWide.c_str());
            return error == 0 ? GameWIP::TestSupport::Types::InfrastructureStatus{}
                              : failureStatus(GameWIP::TestSupport::Types::InfrastructureError::EnvironmentFailed, static_cast<std::uint64_t>(error));
        }
        catch(const std::bad_alloc &)
        {
            return failureStatus(GameWIP::TestSupport::Types::InfrastructureError::OutOfMemory);
        }
        catch(const std::invalid_argument &)
        {
            return failureStatus(GameWIP::TestSupport::Types::InfrastructureError::InvalidArgument);
        }
        catch(const std::length_error &)
        {
            return failureStatus(GameWIP::TestSupport::Types::InfrastructureError::InvalidArgument);
        }
        catch(...)
        {
            return failureStatus(GameWIP::TestSupport::Types::InfrastructureError::EnvironmentFailed);
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
        EnvironmentReadResult result;
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
                    return result;
                }

                if(readError == ERROR_SUCCESS)
                {
                    result.value = std::string{};
                    return result;
                }

                result.status = Internal::failureStatus(Types::InfrastructureError::EnvironmentFailed, readError);
                return result;
            }

            std::wstring value(requiredSize, L'\0');
            SetLastError(ERROR_SUCCESS);
            const DWORD copied = GetEnvironmentVariableW(nameText.c_str(), value.data(), requiredSize);

            if(copied == 0 || copied >= requiredSize)
            {
                result.status = Internal::failureStatus(Types::InfrastructureError::EnvironmentFailed, GetLastError());
                return result;
            }

            value.resize(copied);
            result.value = GameWIP::TestSupport::Internal::Win32::wideToUtf8(value);
            return result;
        }
        catch(const std::bad_alloc &)
        {
            result.status = Internal::failureStatus(Types::InfrastructureError::OutOfMemory);
            return result;
        }
        catch(const std::invalid_argument &)
        {
            result.status = Internal::failureStatus(Types::InfrastructureError::InvalidArgument);
            return result;
        }
        catch(const std::length_error &)
        {
            result.status = Internal::failureStatus(Types::InfrastructureError::InvalidArgument);
            return result;
        }
        catch(...)
        {
            result.status = Internal::failureStatus(Types::InfrastructureError::EnvironmentFailed);
            return result;
        }
    }

    Types::InfrastructureStatus setEnvironmentVariableValue(std::string_view name, std::string_view value) noexcept
    {
        return writeEnvironmentVariable(name, value);
    }

    Types::InfrastructureStatus unsetEnvironmentVariableValue(std::string_view name) noexcept
    {
        return writeEnvironmentVariable(name, std::string_view{});
    }
} // namespace GameWIP::TestSupport::Platform
