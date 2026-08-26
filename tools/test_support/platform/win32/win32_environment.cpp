/// @file win32_environment.cpp
/// @brief Windows environment-variable backend for the TestSupport library.

#include "test_support/internal/test_support_platform.h"
#include "test_support/internal/test_support_test_hooks.h"
#include "unicode/unicode.h"

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
#include <vector>

namespace
{
    [[nodiscard]] GameWIP::TestSupport::Types::InfrastructureStatus environmentFailure(
        GameWIP::TestSupport::Types::InfrastructureError error,
        std::uint64_t nativeCode = 0) noexcept
    {
        return {.error = error, .nativeCode = nativeCode};
    }

    [[nodiscard]] std::wstring utf8ToWide(std::string_view text)
    {
        if (text.empty())
        {
            return {};
        }
        if (text.find('\0') != std::string_view::npos)
        {
            throw std::invalid_argument("Environment text contains an embedded null");
        }

        const auto measurement = GameWIP::Unicode::Utf8::measureToUtf16(text);
        if (measurement.outcome == GameWIP::Unicode::Types::MeasureOutcome::SizeLimitExceeded)
        {
            throw std::length_error("Environment text exceeds the Unicode conversion limit");
        }
        if (measurement.outcome != GameWIP::Unicode::Types::MeasureOutcome::Measured)
        {
            throw std::invalid_argument("Environment text is not valid UTF-8");
        }

        std::vector<char16_t> converted(measurement.requiredCodeUnits);
        const auto conversion = GameWIP::Unicode::Utf8::convertToUtf16(text, converted);
        if (conversion.outcome != GameWIP::Unicode::Types::ConversionOutcome::Converted)
        {
            throw std::runtime_error("Unicode environment text conversion failed");
        }

        std::wstring output(conversion.codeUnitsWritten, L'\0');
        for (std::size_t index = 0; index < conversion.codeUnitsWritten; ++index)
        {
            output[index] = static_cast<wchar_t>(converted[index]);
        }
        return output;
    }

    [[nodiscard]] std::string wideToUtf8(std::wstring_view text)
    {
        if (text.empty())
        {
            return {};
        }
        std::vector<char16_t> source(text.size());
        for (std::size_t index = 0; index < text.size(); ++index)
        {
            source[index] = static_cast<char16_t>(text[index]);
        }

        const auto measurement = GameWIP::Unicode::Utf16::measureToUtf8(source);
        if (measurement.outcome == GameWIP::Unicode::Types::MeasureOutcome::SizeLimitExceeded)
        {
            throw std::length_error("Environment value exceeds the Unicode conversion limit");
        }
        if (measurement.outcome != GameWIP::Unicode::Types::MeasureOutcome::Measured)
        {
            throw std::runtime_error("Environment value is not valid UTF-16");
        }

        std::string output(measurement.requiredBytes, '\0');
        const auto conversion = GameWIP::Unicode::Utf16::convertToUtf8(source, output);
        if (conversion.outcome != GameWIP::Unicode::Types::ConversionOutcome::Converted)
        {
            throw std::runtime_error("Unicode environment value conversion failed");
        }
        return output;
    }

    void validateEnvironmentName(std::string_view name)
    {
        if (name.empty() || name.find('=') != std::string_view::npos)
        {
            throw std::invalid_argument("Environment variable name must be non-empty and cannot contain '='");
        }
    }
} // namespace

namespace GameWIP::TestSupport::Detail::Platform
{
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
            const std::wstring nameText = utf8ToWide(name);
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
            result.value = wideToUtf8(value);
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
            const std::wstring nameWide = utf8ToWide(name);
            const std::wstring valueWide = utf8ToWide(value);
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
            const std::wstring nameWide = utf8ToWide(name);
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
