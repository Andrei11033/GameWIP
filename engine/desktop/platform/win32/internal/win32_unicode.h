/// @file win32_unicode.h
/// @brief Private Win32 text-conversion helpers.

#pragma once

#include "io/status.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <string_view>

namespace GameWIP::Desktop::Detail::Platform
{
    /// @brief Maps a native text-conversion code to the portable encoding failure vocabulary.
    [[nodiscard]] IO::Types::ErrorCode unicodeConversionError(DWORD nativeCode, IO::Types::ErrorCode malformedEncodingFallback) noexcept;

    /// @brief Converts strict UTF-8 to UTF-16 while returning the native conversion code.
    [[nodiscard]] bool utf8ToUtf16(std::string_view text, std::wstring &output, DWORD &nativeCode);

    /// @brief Converts UTF-16 to strict UTF-8 while returning the native conversion code.
    [[nodiscard]] bool utf16ToUtf8(std::wstring_view text, std::string &output, DWORD &nativeCode);
} // namespace GameWIP::Desktop::Detail::Platform
