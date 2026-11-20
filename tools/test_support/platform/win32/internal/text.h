/// @file text.h
/// @brief Shared private text helpers for the TestSupport Win32 backend.

#pragma once

#include <string>
#include <string_view>

namespace GameWIP::TestSupport::Detail::Platform::Win32
{
    /// ------------------------------------------------------------
    /// UTF-8 and UTF-16 conversion
    /// ------------------------------------------------------------

    [[nodiscard]] std::wstring utf8ToWide(std::string_view text);
    [[nodiscard]] std::string wideToUtf8(std::wstring_view text);
} // namespace GameWIP::TestSupport::Detail::Platform::Win32
