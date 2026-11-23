/// @file win32_unicode.h
/// @brief Private Win32 text-conversion helpers.

#pragma once

#include <string>
#include <string_view>

namespace GameWIP::Input::Platform::Win32
{
    /// @brief Converts a Windows wide string to strict UTF-8.
    [[nodiscard]] std::string wideToUtf8(std::wstring_view text);

    /// @brief Converts strict UTF-8 to a Windows wide string.
    [[nodiscard]] std::wstring utf8ToWide(std::string_view text);
} // namespace GameWIP::Input::Platform::Win32
