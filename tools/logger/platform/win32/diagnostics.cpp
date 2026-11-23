/// @file diagnostics.cpp
/// @brief Win32 debugger output and fatal popup.

#include "logger/platform/backend.h"
#include "unicode/unicode.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <new>
#include <string>
#include <vector>

namespace
{
    /// @brief Converts validated UTF-8 text to a native UTF-16 string for Win32 APIs.
    [[nodiscard]] GameWIP::IO::Types::Status utf8ToWide(std::string_view text, std::wstring &outText)
    {
        outText.clear();

        if(text.empty())
        {
            return {};
        }

        // Measure the UTF-16 output before allocating conversion buffers.
        const auto measurement = GameWIP::Unicode::Utf8::measureToUtf16(text);
        if(measurement.outcome == GameWIP::Unicode::Types::MeasureOutcome::SizeLimitExceeded)
        {
            return GameWIP::IO::makeStatus(GameWIP::IO::Types::ErrorCode::SizeLimitExceeded, ERROR_INSUFFICIENT_BUFFER);
        }

        if(measurement.outcome != GameWIP::Unicode::Types::MeasureOutcome::Measured)
        {
            return GameWIP::IO::makeStatus(GameWIP::IO::Types::ErrorCode::EncodingFailed, ERROR_NO_UNICODE_TRANSLATION);
        }

        std::vector<char16_t> converted(measurement.requiredCodeUnits);
        const auto conversion = GameWIP::Unicode::Utf8::convertToUtf16(text, converted);
        if(conversion.outcome == GameWIP::Unicode::Types::ConversionOutcome::DestinationTooSmall)
        {
            return GameWIP::IO::makeStatus(GameWIP::IO::Types::ErrorCode::SizeLimitExceeded, ERROR_INSUFFICIENT_BUFFER);
        }

        if(conversion.outcome != GameWIP::Unicode::Types::ConversionOutcome::Converted)
        {
            return GameWIP::IO::makeStatus(GameWIP::IO::Types::ErrorCode::EncodingFailed, ERROR_NO_UNICODE_TRANSLATION);
        }

        try
        {
            outText.resize(conversion.codeUnitsWritten);
        }
        catch(const std::bad_alloc &)
        {
            return GameWIP::IO::makeStatus(GameWIP::IO::Types::ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return GameWIP::IO::makeStatus(GameWIP::IO::Types::ErrorCode::Unknown);
        }

        // Copy the converted code units into the Windows string type.
        for(std::size_t index = 0; index < conversion.codeUnitsWritten; ++index)
        {
            outText[index] = static_cast<wchar_t>(converted[index]);
        }

        return {};
    }

    /// @brief Makes embedded NULs visible before text is passed to NUL-terminated Win32 APIs.
    [[nodiscard]] std::wstring escapeEmbeddedNuls(std::wstring_view text)
    {
        std::wstring result;
        result.reserve(text.size());

        for(wchar_t value : text)
        {
            if(value == L'\0')
            {
                result += L"\\0";
            }
            else
            {
                result.push_back(value);
            }
        }

        return result;
    }

    /// @brief Prepares text for NUL-terminated Win32 diagnostic APIs.
    [[nodiscard]] GameWIP::IO::Types::Status prepareNativeDiagnosticText(std::string_view text, std::wstring &outText)
    {
        GameWIP::IO::Types::Status status = utf8ToWide(text, outText);
        if(!status.ok())
        {
            return status;
        }

        if(outText.contains(L'\0'))
        {
            outText = escapeEmbeddedNuls(outText);
        }
        return {};
    }
} // namespace

namespace GameWIP::Logger::Platform
{
    /// ------------------------------------------------------------
    /// Platform diagnostics
    /// ------------------------------------------------------------

    IO::Types::Status writeDebugOutput(std::string_view line)
    {
        try
        {
            std::wstring output;
            const IO::Types::Status status = prepareNativeDiagnosticText(line, output);
            if(!status.ok())
            {
                return status;
            }

            OutputDebugStringW(output.c_str());
            return {};
        }
        catch(const std::bad_alloc &)
        {
            return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return IO::makeStatus(IO::Types::ErrorCode::NativeFailure, GetLastError());
        }
    }

    IO::Types::Status showFatalPopup(std::string_view message)
    {
        try
        {
            std::wstring messageText;
            const IO::Types::Status status = prepareNativeDiagnosticText(message, messageText);
            if(!status.ok())
            {
                return status;
            }

            if(MessageBoxW(nullptr, messageText.c_str(), L"Fatal Error", MB_ICONERROR | MB_OK) == 0)
            {
                return IO::makeStatus(IO::Types::ErrorCode::NativeFailure, GetLastError());
            }
            return {};
        }
        catch(const std::bad_alloc &)
        {
            return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            return IO::makeStatus(IO::Types::ErrorCode::NativeFailure, GetLastError());
        }
    }
} // namespace GameWIP::Logger::Platform
