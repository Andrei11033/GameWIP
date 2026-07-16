/// @file win32_environment.cpp
/// @brief Windows environment-variable backend for the TestSupport library.

#include "test_support/internal/test_support_platform.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{
    /// @brief Converts public UTF-8 environment text to UTF-16 without lossy substitution.
    [[nodiscard]] std::wstring utf8ToWide(std::string_view text)
    {
        if (text.empty())
        {
            return {};
        }
        if (text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        {
            throw std::length_error("Environment text exceeds the Win32 conversion limit");
        }
        if (text.find('\0') != std::string_view::npos)
        {
            throw std::invalid_argument("Environment text contains an embedded null");
        }

        const int inputSize = static_cast<int>(text.size());
        const int wideSize = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), inputSize, nullptr, 0);
        if (wideSize <= 0)
        {
            throw std::invalid_argument("Environment text is not valid UTF-8");
        }

        std::wstring output(static_cast<std::size_t>(wideSize), L'\0');
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), inputSize, output.data(), wideSize) != wideSize)
        {
            throw std::runtime_error("Win32 environment text conversion failed");
        }
        return output;
    }

    /// @brief Converts Win32 UTF-16 environment text to UTF-8.
    [[nodiscard]] std::string wideToUtf8(std::wstring_view text)
    {
        if (text.empty())
        {
            return {};
        }
        if (text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        {
            throw std::length_error("Environment value exceeds the UTF-8 conversion limit");
        }

        const int inputSize = static_cast<int>(text.size());
        const int utf8Size = WideCharToMultiByte(CP_UTF8, 0, text.data(), inputSize, nullptr, 0, nullptr, nullptr);
        if (utf8Size <= 0)
        {
            throw std::runtime_error("Win32 environment value conversion failed");
        }

        std::string output(static_cast<std::size_t>(utf8Size), '\0');
        if (WideCharToMultiByte(CP_UTF8, 0, text.data(), inputSize, output.data(), utf8Size, nullptr, nullptr) != utf8Size)
        {
            throw std::runtime_error("Win32 environment value conversion failed");
        }
        return output;
    }

    /// @brief Enforces the shared key syntax before either Win32 or CRT environment access.
    /// @note Windows compares environment names case-insensitively; no normalization is needed for the native calls.
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
    std::optional<std::string> readEnvironmentVariable(std::string_view name)
    {
        validateEnvironmentName(name);
        const std::wstring nameText = utf8ToWide(name);
        SetLastError(ERROR_SUCCESS);
        const DWORD requiredSize = GetEnvironmentVariableW(nameText.c_str(), nullptr, 0);
        if (requiredSize == 0)
        {
            return GetLastError() == ERROR_ENVVAR_NOT_FOUND ? std::nullopt : std::optional<std::string>{std::string{}};
        }

        std::wstring value(requiredSize, L'\0');
        const DWORD copied = GetEnvironmentVariableW(nameText.c_str(), value.data(), requiredSize);
        if (copied == 0 || copied >= requiredSize)
        {
            return std::nullopt;
        }

        value.resize(copied);
        return wideToUtf8(value);
    }

    void setEnvironmentVariableValue(std::string_view name, std::string_view value)
    {
        // `_wputenv_s(name, L"")` removes the entry. The public guard documentation exposes this
        // Win32/CRT limitation instead of pretending an empty and missing process value are distinct.
        validateEnvironmentName(name);
        const std::wstring nameWide = utf8ToWide(name);
        const std::wstring valueWide = utf8ToWide(value);
        if (_wputenv_s(nameWide.c_str(), valueWide.c_str()) != 0)
        {
            throw std::runtime_error("Could not set the environment variable");
        }
    }

    void unsetEnvironmentVariableValue(std::string_view name)
    {
        validateEnvironmentName(name);
        const std::wstring nameWide = utf8ToWide(name);
        if (_wputenv_s(nameWide.c_str(), L"") != 0)
        {
            throw std::runtime_error("Could not unset the environment variable");
        }
    }
} // namespace GameWIP::TestSupport::Detail::Platform
