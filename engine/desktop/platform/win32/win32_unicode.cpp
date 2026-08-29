/// @file win32_unicode.cpp
/// @brief Strict Unicode conversion bridge for Win32 Window APIs.

#include "desktop/platform/win32/internal/win32_window_backend.h"
#include "base/checked_arithmetic.h"

#include "unicode/unicode.h"

#include <vector>

namespace GameWIP::Desktop::Detail::Platform
{
    namespace
    {
        [[nodiscard]] DWORD nativeCodeForConversion(GameWIP::Unicode::Types::ConversionOutcome outcome) noexcept
        {
            using Outcome = GameWIP::Unicode::Types::ConversionOutcome;
            return outcome == Outcome::DestinationTooSmall ? ERROR_INSUFFICIENT_BUFFER : ERROR_NO_UNICODE_TRANSLATION;
        }
    } // namespace

    bool utf8ToUtf16(std::string_view text, std::wstring &output, DWORD &nativeCode)
    {
        output.clear();
        nativeCode = ERROR_SUCCESS;
        if (text.empty())
            return true;

        // U+0000 is valid Unicode, but every native caller of this bridge passes the result to
        // NUL-terminated Win32 text APIs and therefore cannot preserve an embedded NUL.
        if (text.find('\0') != std::string_view::npos)
        {
            nativeCode = ERROR_INVALID_PARAMETER;
            return false;
        }

        // A valid UTF-8 string never needs more UTF-16 code units than source bytes, so this
        // capacity lets Unicode validate and convert in one pass instead of pre-validating or
        // measuring and then scanning the same source again.
        std::vector<char16_t> converted(text.size());
        const GameWIP::Unicode::Types::Utf8::ToUtf16Result conversion = GameWIP::Unicode::Utf8::convertToUtf16(text, converted);
        if (conversion.outcome != GameWIP::Unicode::Types::ConversionOutcome::Converted)
        {
            nativeCode = nativeCodeForConversion(conversion.outcome);
            return false;
        }

        output.resize(conversion.codeUnitsWritten);
        for (std::size_t index = 0; index < conversion.codeUnitsWritten; ++index)
            output[index] = static_cast<wchar_t>(converted[index]);
        return true;
    }

    bool utf16ToUtf8(std::wstring_view text, std::string &output, DWORD &nativeCode)
    {
        output.clear();
        nativeCode = ERROR_SUCCESS;
        if (text.empty())
            return true;

        // One UTF-16 code unit needs at most three UTF-8 bytes. A surrogate pair uses four bytes
        // for two code units, so 3 * source length is a sufficient caller-owned destination.
        if (GameWIP::Base::wouldMultiplyOverflow(text.size(), std::size_t{3}))
        {
            nativeCode = ERROR_ARITHMETIC_OVERFLOW;
            return false;
        }

        std::vector<char16_t> source(text.size());
        for (std::size_t index = 0; index < text.size(); ++index)
            source[index] = static_cast<char16_t>(text[index]);

        output.resize(text.size() * 3U);
        const GameWIP::Unicode::Types::Utf16::ToUtf8Result conversion = GameWIP::Unicode::Utf16::convertToUtf8(source, output);
        if (conversion.outcome != GameWIP::Unicode::Types::ConversionOutcome::Converted)
        {
            nativeCode = nativeCodeForConversion(conversion.outcome);
            output.clear();
            return false;
        }
        output.resize(conversion.bytesWritten);
        return true;
    }
} // namespace GameWIP::Desktop::Detail::Platform
