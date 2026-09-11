/// @file win32_logger.cpp
/// @brief Windows debugger output, fatal popup, local-time, process-memory, and formatting-scratch backend.

#include "logger/internal/logger_platform.h"
#include "unicode/unicode.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <deque>
#include <new>
#include <string>
#include <vector>

namespace
{
    // ------------------------------------------------------------
    // Format scratch storage
    // ------------------------------------------------------------

    /// @brief Reentrant per-thread formatting storage for nested Logger formatting calls.
    struct FormatScratchStorage
    {
        std::deque<std::string> buffers;
        std::size_t depth = 0;
    };

    // ------------------------------------------------------------
    // UTF-8 conversion
    // ------------------------------------------------------------

    /// @brief Converts validated UTF-8 text to a native UTF-16 string for Win32 APIs.
    [[nodiscard]] GameWIP::IO::Types::Status utf8ToWide(std::string_view text, std::wstring &outText)
    {
        outText.clear();

        if (text.empty())
        {
            return {};
        }

        // A valid UTF-8 string never needs more UTF-16 code units than source bytes, so
        // this keeps report/debug conversion to one Unicode pass without pre-measuring.
        std::vector<char16_t> converted(text.size());
        const auto conversion = GameWIP::Unicode::Utf8::convertToUtf16(text, converted);
        if (conversion.outcome == GameWIP::Unicode::Types::ConversionOutcome::DestinationTooSmall)
        {
            return GameWIP::IO::makeStatus(GameWIP::IO::Types::ErrorCode::SizeLimitExceeded, ERROR_INSUFFICIENT_BUFFER);
        }
        if (conversion.outcome != GameWIP::Unicode::Types::ConversionOutcome::Converted)
        {
            return GameWIP::IO::makeStatus(GameWIP::IO::Types::ErrorCode::EncodingFailed, ERROR_NO_UNICODE_TRANSLATION);
        }

        try
        {
            outText.resize(conversion.codeUnitsWritten);
        }
        catch (const std::bad_alloc &)
        {
            return GameWIP::IO::makeStatus(GameWIP::IO::Types::ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return GameWIP::IO::makeStatus(GameWIP::IO::Types::ErrorCode::Unknown);
        }

        for (std::size_t index = 0; index < conversion.codeUnitsWritten; ++index)
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

        for (wchar_t value : text)
        {
            if (value == L'\0')
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

#if defined(__MINGW32__)
    // ------------------------------------------------------------
    // MinGW FLS scratch fallback
    // ------------------------------------------------------------

    void NTAPI destroyFormatScratch(void *value)
    {
        delete static_cast<FormatScratchStorage *>(value);
    }

    /// @brief Owns the process-wide FLS index while its callback code remains loaded.
    class FormatScratchSlot final
    {
    public:
        FormatScratchSlot() noexcept
            : value_(FlsAlloc(destroyFormatScratch))
        {
        }

        ~FormatScratchSlot() noexcept
        {
            if (value_ != FLS_OUT_OF_INDEXES)
            {
                static_cast<void>(FlsFree(value_));
            }
        }

        FormatScratchSlot(const FormatScratchSlot &) = delete;
        FormatScratchSlot &operator=(const FormatScratchSlot &) = delete;

        [[nodiscard]] DWORD value() const noexcept
        {
            return value_;
        }

    private:
        DWORD value_ = FLS_OUT_OF_INDEXES;
    };

    DWORD formatScratchSlot()
    {
        static FormatScratchSlot slot;
        return slot.value();
    }

    FormatScratchStorage &formatScratchStorageForThreadFls()
    {
        const DWORD slot = formatScratchSlot();
        if (slot == FLS_OUT_OF_INDEXES)
        {
            throw std::bad_alloc();
        }

        auto *storage = static_cast<FormatScratchStorage *>(FlsGetValue(slot));
        if (!storage)
        {
            storage = new FormatScratchStorage();
            if (!FlsSetValue(slot, storage))
            {
                delete storage;
                throw std::bad_alloc();
            }
        }

        return *storage;
    }
#endif

    FormatScratchStorage &formatScratchStorageForThread()
    {
#if defined(__MINGW32__)
        return formatScratchStorageForThreadFls();
#else
        thread_local FormatScratchStorage storage;
        return storage;
#endif
    }
} // namespace

namespace GameWIP::Logger::Detail::Platform
{
    std::string &formatScratchForThread()
    {
        FormatScratchStorage &storage = formatScratchStorageForThread();

        if (storage.buffers.size() == storage.depth)
        {
            storage.buffers.emplace_back();
        }

        std::string &scratch = storage.buffers[storage.depth++];
        scratch.clear();

        return scratch;
    }

    void releaseFormatScratchForThread() noexcept
    {
        FormatScratchStorage &storage = formatScratchStorageForThread();
        if (storage.depth == 0)
        {
            return;
        }

        --storage.depth;

        if (storage.depth > 0 && storage.depth + 1 == storage.buffers.size())
        {
            // Keep the outermost buffer hot for the common case, but release extra
            // nested buffers once nested formatting unwinds.
            storage.buffers.pop_back();
        }
    }

    // ------------------------------------------------------------
    // Platform diagnostics
    // ------------------------------------------------------------

    IO::Types::Status writeDebugOutput(std::string_view line)
    {
        try
        {
            std::wstring output;
            IO::Types::Status status = utf8ToWide(line, output);
            if (!status.ok())
            {
                return status;
            }

            if (output.contains(L'\0'))
            {
                output = escapeEmbeddedNuls(output);
            }

            OutputDebugStringW(output.c_str());
            return {};
        }
        catch (const std::bad_alloc &)
        {
            return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return IO::makeStatus(IO::Types::ErrorCode::NativeFailure, GetLastError());
        }
    }

    IO::Types::Status showFatalPopup(std::string_view message)
    {
        try
        {
            std::wstring messageText;
            IO::Types::Status status = utf8ToWide(message, messageText);
            if (!status.ok())
            {
                return status;
            }

            if (messageText.contains(L'\0'))
            {
                messageText = escapeEmbeddedNuls(messageText);
            }

            if (MessageBoxW(nullptr, messageText.c_str(), L"Fatal Error", MB_ICONERROR | MB_OK) == 0)
            {
                return IO::makeStatus(IO::Types::ErrorCode::NativeFailure, GetLastError());
            }

            return {};
        }
        catch (const std::bad_alloc &)
        {
            return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return IO::makeStatus(IO::Types::ErrorCode::NativeFailure, GetLastError());
        }
    }

    // ------------------------------------------------------------
    // Time and memory diagnostics
    // ------------------------------------------------------------

    IO::Types::Status formatLocalTime(std::time_t time, std::string_view timeFormat, std::string &outText)
    {
        outText.clear();
        std::tm timeInfo{};
        const int localTimeResult = localtime_s(&timeInfo, &time);
        if (localTimeResult != 0)
        {
            return IO::makeStatus(IO::Types::ErrorCode::NativeFailure, localTimeResult);
        }

        if (timeFormat.size() >= 64)
        {
            return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument, ERROR_INVALID_DATA);
        }

        char formatBuffer[64]{};
        if (!timeFormat.empty())
        {
            std::ranges::copy(timeFormat, formatBuffer);
        }

        char timeBuffer[64]{};
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
        // Runtime strftime formats are intentional here. The format is length-bounded,
        // null-terminated, and has no variadic arguments whose types could mismatch it.
        const std::size_t written = std::strftime(timeBuffer, sizeof(timeBuffer), formatBuffer, &timeInfo);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        if (written == 0)
        {
            return IO::makeStatus(IO::Types::ErrorCode::NativeFailure, ERROR_INVALID_DATA);
        }

        try
        {
            outText.assign(timeBuffer, written);
            return {};
        }
        catch (const std::bad_alloc &)
        {
            outText.clear();
            return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            outText.clear();
            return IO::makeStatus(IO::Types::ErrorCode::Unknown);
        }
    }

    ProcessMemory queryProcessMemory()
    {
        PROCESS_MEMORY_COUNTERS_EX counters{};
        counters.cb = sizeof(counters);
        if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters), sizeof(counters)) == 0)
        {
            return {};
        }
        return {static_cast<std::size_t>(counters.WorkingSetSize), static_cast<std::size_t>(counters.PrivateUsage), true};
    }
} // namespace GameWIP::Logger::Detail::Platform
