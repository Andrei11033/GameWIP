/// @file win32_unicode.cpp
/// @brief Strict Unicode conversion bridge for Win32 Desktop APIs.

#include "desktop/platform/win32/internal/win32_window_backend.h"
#include "unicode/unicode.h"

#include <vector>

namespace GameWIP::Desktop::Detail::Platform
{
    IO::Types::ErrorCode unicodeConversionError(DWORD nativeCode, IO::Types::ErrorCode malformedEncodingFallback) noexcept
    {
        switch (nativeCode)
        {
        case ERROR_INVALID_PARAMETER:
            return IO::Types::ErrorCode::InvalidArgument;
        case ERROR_INSUFFICIENT_BUFFER:
        case ERROR_ARITHMETIC_OVERFLOW:
            return IO::Types::ErrorCode::SizeLimitExceeded;
        default:
            return malformedEncodingFallback;
        }
    }

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
        {
            return true;
        }

        // U+0000 is valid Unicode, but every native caller of this bridge passes the result to
        // NUL-terminated Win32 text APIs and therefore cannot preserve an embedded NUL.
        if (text.contains('\0'))
        {
            nativeCode = ERROR_INVALID_PARAMETER;
            return false;
        }

        const auto measurement = GameWIP::Unicode::Utf8::measureToUtf16(text);
        if (measurement.outcome == GameWIP::Unicode::Types::MeasureOutcome::SizeLimitExceeded)
        {
            nativeCode = ERROR_INSUFFICIENT_BUFFER;
            return false;
        }
        if (measurement.outcome != GameWIP::Unicode::Types::MeasureOutcome::Measured)
        {
            nativeCode = ERROR_NO_UNICODE_TRANSLATION;
            return false;
        }

        std::vector<char16_t> converted(measurement.requiredCodeUnits);
        const GameWIP::Unicode::Types::Utf8::ToUtf16Result conversion = GameWIP::Unicode::Utf8::convertToUtf16(text, converted);
        if (conversion.outcome != GameWIP::Unicode::Types::ConversionOutcome::Converted)
        {
            nativeCode = nativeCodeForConversion(conversion.outcome);
            return false;
        }

        output.resize(conversion.codeUnitsWritten);
        for (std::size_t index = 0; index < conversion.codeUnitsWritten; ++index)
        {
            output[index] = static_cast<wchar_t>(converted[index]);
        }
        return true;
    }

    bool utf16ToUtf8(std::wstring_view text, std::string &output, DWORD &nativeCode)
    {
        output.clear();
        nativeCode = ERROR_SUCCESS;
        if (text.empty())
        {
            return true;
        }

        std::vector<char16_t> source(text.size());
        for (std::size_t index = 0; index < text.size(); ++index)
        {
            source[index] = static_cast<char16_t>(text[index]);
        }

        const auto measurement = GameWIP::Unicode::Utf16::measureToUtf8(source);
        if (measurement.outcome == GameWIP::Unicode::Types::MeasureOutcome::SizeLimitExceeded)
        {
            nativeCode = ERROR_INSUFFICIENT_BUFFER;
            return false;
        }
        if (measurement.outcome != GameWIP::Unicode::Types::MeasureOutcome::Measured)
        {
            nativeCode = ERROR_NO_UNICODE_TRANSLATION;
            return false;
        }

        output.resize(measurement.requiredBytes);
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
