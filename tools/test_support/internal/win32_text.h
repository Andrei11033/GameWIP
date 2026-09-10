/// @file win32_text.h
/// @brief Shared UTF-8 and UTF-16 conversion helpers for TestSupport's Win32 backends.

#pragma once

#include "unicode/unicode.h"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace GameWIP::TestSupport::Detail::Win32
{
    // ------------------------------------------------------------
    // UTF-8 to UTF-16 conversion
    // ------------------------------------------------------------

    /// Converts validated UTF-8 input to the UTF-16 representation used by Win32 APIs.
    [[nodiscard]] inline std::wstring utf8ToWide(std::string_view text)
    {
        if (text.find('\0') != std::string_view::npos)
        {
            throw std::invalid_argument("Win32 text contains an embedded null");
        }
        if (text.empty())
        {
            return {};
        }

        const auto measurement = GameWIP::Unicode::Utf8::measureToUtf16(text);
        if (measurement.outcome == GameWIP::Unicode::Types::MeasureOutcome::SizeLimitExceeded)
        {
            throw std::length_error("Win32 text exceeds the Unicode conversion limit");
        }
        if (measurement.outcome != GameWIP::Unicode::Types::MeasureOutcome::Measured)
        {
            throw std::invalid_argument("Win32 text is not valid UTF-8");
        }

        std::vector<char16_t> converted(measurement.requiredCodeUnits);
        const auto conversion = GameWIP::Unicode::Utf8::convertToUtf16(text, converted);
        if (conversion.outcome != GameWIP::Unicode::Types::ConversionOutcome::Converted)
        {
            throw std::runtime_error("Win32 UTF-8 conversion failed");
        }

        std::wstring output(conversion.codeUnitsWritten, L'\0');
        for (std::size_t index = 0; index < conversion.codeUnitsWritten; ++index)
        {
            output[index] = static_cast<wchar_t>(converted[index]);
        }
        return output;
    }

    // ------------------------------------------------------------
    // UTF-16 to UTF-8 conversion
    // ------------------------------------------------------------

    /// Converts UTF-16 text returned by Win32 APIs to the library's UTF-8 representation.
    [[nodiscard]] inline std::string wideToUtf8(std::wstring_view text)
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
            throw std::length_error("Win32 text exceeds the Unicode conversion limit");
        }
        if (measurement.outcome != GameWIP::Unicode::Types::MeasureOutcome::Measured)
        {
            throw std::runtime_error("Win32 text is not valid UTF-16");
        }

        std::string output(measurement.requiredBytes, '\0');
        const auto conversion = GameWIP::Unicode::Utf16::convertToUtf8(source, output);
        if (conversion.outcome != GameWIP::Unicode::Types::ConversionOutcome::Converted)
        {
            throw std::runtime_error("Win32 UTF-16 conversion failed");
        }
        return output;
    }
} // namespace GameWIP::TestSupport::Detail::Win32
