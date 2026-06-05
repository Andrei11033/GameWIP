/// @file win32_environment.cpp
/// @brief Windows environment-variable backend for the GameWIP TestSupport library.

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
#include <string>

namespace
{
    [[nodiscard]] std::wstring utf8ToWide(std::string_view text)
    {
        if (text.empty())
        {
            return {};
        }
        if (text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        {
            return L"?";
        }

        const int inputSize = static_cast<int>(text.size());
        const int wideSize = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), inputSize, nullptr, 0);
        if (wideSize <= 0)
        {
            std::wstring fallback;
            fallback.reserve(text.size());
            for (char ch : text)
            {
                const unsigned char value = static_cast<unsigned char>(ch);
                fallback.push_back(value >= 0x20 && value < 0x80 ? static_cast<wchar_t>(value) : L'?');
            }
            return fallback;
        }

        std::wstring output(static_cast<std::size_t>(wideSize), L'\0');
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), inputSize, output.data(), wideSize) != wideSize)
        {
            return L"?";
        }
        return output;
    }

    [[nodiscard]] std::string wideToUtf8(std::wstring_view text)
    {
        if (text.empty())
        {
            return {};
        }
        if (text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        {
            return {};
        }

        const int inputSize = static_cast<int>(text.size());
        const int utf8Size = WideCharToMultiByte(CP_UTF8, 0, text.data(), inputSize, nullptr, 0, nullptr, nullptr);
        if (utf8Size <= 0)
        {
            return {};
        }

        std::string output(static_cast<std::size_t>(utf8Size), '\0');
        if (WideCharToMultiByte(CP_UTF8, 0, text.data(), inputSize, output.data(), utf8Size, nullptr, nullptr) != utf8Size)
        {
            return {};
        }
        return output;
    }
} // namespace

namespace GameWIP::TestSupport::Detail::Platform
{
    std::optional<std::string> readEnvironmentVariable(std::string_view name)
    {
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
        const std::wstring nameWide = utf8ToWide(name);
        const std::wstring valueWide = utf8ToWide(value);
        _wputenv_s(nameWide.c_str(), valueWide.c_str());
        SetEnvironmentVariableW(nameWide.c_str(), valueWide.c_str());
    }

    void unsetEnvironmentVariableValue(std::string_view name)
    {
        const std::wstring nameWide = utf8ToWide(name);
        _wputenv_s(nameWide.c_str(), L"");
        SetEnvironmentVariableW(nameWide.c_str(), nullptr);
    }
} // namespace GameWIP::TestSupport::Detail::Platform
