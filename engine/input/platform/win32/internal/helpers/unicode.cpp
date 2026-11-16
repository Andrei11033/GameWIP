/// @file win32_unicode.cpp
/// @brief UTF-8 and Windows wide-string conversion helpers.

#include "input/platform/win32/internal/win32_unicode.h"

#include "unicode/unicode.h"

#include <span>
#include <string>
#include <string_view>

namespace GameWIP::Input::Platform::Win32::Detail
{
    /// ------------------------------------------------------------
    /// Windows text conversion
    /// ------------------------------------------------------------

    [[nodiscard]] std::string wideToUtf8(std::wstring_view text)
    {
        if(text.empty())
        {
            return {};
        }

        // Copy Win32 UTF-16 code units into Unicode's portable input type.
        std::u16string codeUnits;
        codeUnits.reserve(text.size());
        for(const wchar_t codeUnit : text)
        {
            codeUnits.push_back(static_cast<char16_t>(codeUnit));
        }

        // Measure first so invalid or partially consumed input is rejected before allocation.
        const auto measured = GameWIP::Unicode::Utf16::measureToUtf8(codeUnits);
        if(measured.outcome != GameWIP::Unicode::Types::MeasureOutcome::Measured || measured.sourceCodeUnitsProcessed != codeUnits.size())
        {
            return {};
        }

        // Convert into the exact output size and confirm the full input was consumed.
        std::string result(measured.requiredBytes, '\0');
        const auto converted = GameWIP::Unicode::Utf16::convertToUtf8(codeUnits, std::span<char>(result.data(), result.size()));
        return converted.outcome == GameWIP::Unicode::Types::ConversionOutcome::Converted && converted.sourceCodeUnitsConsumed == codeUnits.size()
                   ? result
                   : std::string{};
    }

    [[nodiscard]] std::wstring utf8ToWide(std::string_view text)
    {
        if(text.empty())
        {
            return {};
        }

        // Measure the complete UTF-8 text before allocating UTF-16 storage.
        const auto measured = GameWIP::Unicode::Utf8::measureToUtf16(text);
        if(measured.outcome != GameWIP::Unicode::Types::MeasureOutcome::Measured || measured.sourceBytesProcessed != text.size())
        {
            return {};
        }

        // Convert and reject partial input before exposing it to a Win32 caller.
        std::u16string codeUnits(measured.requiredCodeUnits, u'\0');
        const auto converted = GameWIP::Unicode::Utf8::convertToUtf16(text, std::span<char16_t>(codeUnits.data(), codeUnits.size()));
        if(converted.outcome != GameWIP::Unicode::Types::ConversionOutcome::Converted || converted.sourceBytesConsumed != text.size())
        {
            return {};
        }

        // Restore the native wide-string representation.
        std::wstring result;
        result.reserve(codeUnits.size());
        for(const char16_t codeUnit : codeUnits)
        {
            result.push_back(static_cast<wchar_t>(codeUnit));
        }
        return result;
    }
} // namespace GameWIP::Input::Platform::Win32::Detail
