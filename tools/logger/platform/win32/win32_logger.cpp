/// @file win32_logger.cpp
/// @brief Windows debugger output, fatal popup, local-time, process-memory, and nesting-safe formatting-scratch backend.

#include "logger/internal/logger_platform.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>

#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <new>
#include <string>

namespace
{
    /// @brief Nesting-safe per-thread storage used by public formatted Logger overloads.
    struct FormatScratchStorage
    {
        std::deque<std::string> buffers;
        std::size_t depth = 0;
    };

    /// @brief Short local alias for the public platform error type.
    using PlatformError = GameWIP::Logger::Types::PlatformError;
    /// @brief Short local alias for the public platform error source enum.
    using PlatformErrorSource = GameWIP::Logger::Types::PlatformErrorSource;

    /// @brief Creates a success platform-error value.
    /// @return Platform error with source None and native code zero.
    PlatformError noPlatformError()
    {
        return {};
    }

    /// @brief Creates a platform-error value from a Win32/native code.
    /// @param source Platform operation family that failed.
    /// @param nativeCode Native platform error code.
    /// @return Structured platform error.
    PlatformError makePlatformError(PlatformErrorSource source, std::uint64_t nativeCode)
    {
        return PlatformError{source, nativeCode};
    }

    /// @brief Converts UTF-8 logger text to UTF-16 for Win32 W APIs.
    /// @param text UTF-8 text from the logger.
    /// @param outText Receives UTF-16 text on success.
    /// @param outError Receives GetLastError-style failure details.
    /// @return True when conversion succeeded.
    bool utf8ToWide(std::string_view text, std::wstring &outText, unsigned long &outError)
    {
        outText.clear();
        outError = 0;
        if (text.empty())
        {
            return true;
        }

        if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        {
            outError = ERROR_INVALID_PARAMETER;
            return false;
        }

        const int sourceLength = static_cast<int>(text.size());
        constexpr unsigned long conversionFlags = MB_ERR_INVALID_CHARS;
        const int wideLength = MultiByteToWideChar(CP_UTF8, conversionFlags, text.data(), sourceLength, nullptr, 0);
        if (wideLength <= 0)
        {
            outError = GetLastError();
            return false;
        }

        outText.resize(static_cast<std::size_t>(wideLength));
        if (MultiByteToWideChar(CP_UTF8, conversionFlags, text.data(), sourceLength, outText.data(), wideLength) != wideLength)
        {
            outError = GetLastError();
            return false;
        }

        outError = 0;
        return true;
    }

    /// @brief Escapes embedded UTF-16 NULs before passing text to null-terminated Win32 UI/debug APIs.
    /// @param text Converted UTF-16 text that may contain embedded NUL characters.
    /// @return Text with embedded NULs replaced by a visible \0 sequence.
    std::wstring escapeEmbeddedNuls(std::wstring_view text)
    {
        std::wstring result;
        result.reserve(text.size());

        for (const wchar_t value : text)
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
    /// @brief Frees MinGW format scratch without using non-trivial C++ TLS destructors.
    void NTAPI destroyFormatScratch(void *value)
    {
        delete static_cast<FormatScratchStorage *>(value);
    }

    /// @brief Returns the FLS slot used for per-thread format scratch on MinGW.
    DWORD formatScratchSlot()
    {
        static const DWORD slot = FlsAlloc(destroyFormatScratch);
        return slot;
    }

    /// @brief Returns MinGW per-thread format scratch through FLS.
    /// @return Mutable per-thread scratch string.
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

    /// @brief Returns process-lifetime access to the current thread's formatting storage.
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
    /// @brief Returns per-thread format scratch storage for Win32 logger formatting.
    /// @return Mutable per-thread scratch string.
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
            storage.buffers.pop_back();
        }
    }

    /// @brief Writes a formatted log line to the Win32 debugger output stream.
    /// @param line UTF-8 log line, including caller-chosen trailing newline.
    /// @return Structured platform error, or source None on success.
    GameWIP::Logger::Types::PlatformError writeDebugOutput(std::string_view line)
    {
        std::wstring output;
        unsigned long error = 0;
        if (!utf8ToWide(line, output, error))
        {
            return makePlatformError(PlatformErrorSource::DebugOutput, error);
        }

        if (output.find(L'\0') != std::wstring::npos)
        {
            output = escapeEmbeddedNuls(output);
        }

        OutputDebugStringW(output.c_str());
        return noPlatformError();
    }

    /// @brief Displays a fatal error message using the Win32 Unicode message box API.
    /// @param message UTF-8 message text.
    /// @return Structured platform error, or source None on success.
    GameWIP::Logger::Types::PlatformError showFatalPopup(std::string_view message)
    {
        std::wstring messageText;
        unsigned long error = 0;
        if (!utf8ToWide(message, messageText, error))
        {
            return makePlatformError(PlatformErrorSource::FatalPopup, error);
        }

        if (messageText.find(L'\0') != std::wstring::npos)
        {
            messageText = escapeEmbeddedNuls(messageText);
        }

        if (MessageBoxW(nullptr, messageText.c_str(), L"Fatal Error", MB_ICONERROR | MB_OK) == 0)
        {
            return makePlatformError(PlatformErrorSource::FatalPopup, GetLastError());
        }

        return noPlatformError();
    }

    /// @brief Formats local time using Win32/MSVC-safe localtime_s and strftime.
    /// @param time Time value to convert.
    /// @param timeFormat strftime-compatible format string.
    /// @param outText Receives formatted local-time text.
    /// @return Structured platform error, or source None on success.
    GameWIP::Logger::Types::PlatformError formatLocalTime(std::time_t time, std::string_view timeFormat, std::string &outText)
    {
        outText.clear();

        std::tm timeInfo{};
        const int localTimeResult = localtime_s(&timeInfo, &time);
        if (localTimeResult != 0)
        {
            return makePlatformError(PlatformErrorSource::TimeConversion, static_cast<unsigned long>(localTimeResult));
        }

        if (timeFormat.size() >= 64)
        {
            return makePlatformError(PlatformErrorSource::TimeConversion, ERROR_INVALID_DATA);
        }

        char formatBuffer[64]{};
        if (!timeFormat.empty())
        {
            std::memcpy(formatBuffer, timeFormat.data(), timeFormat.size());
        }

        char timeBuffer[64]{};
        const std::size_t written = std::strftime(timeBuffer, sizeof(timeBuffer), formatBuffer, &timeInfo);
        if (written == 0)
        {
            return makePlatformError(PlatformErrorSource::TimeConversion, ERROR_INVALID_DATA);
        }

        outText.assign(timeBuffer, written);
        return noPlatformError();
    }

    /// @brief Queries process-level memory counters from Win32.
    /// @return Current process memory values, or available false on failure.
    ProcessMemory queryProcessMemory()
    {
        PROCESS_MEMORY_COUNTERS_EX counters{};
        counters.cb = sizeof(counters);
        if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters), sizeof(counters)) == 0)
        {
            return {};
        }

        ProcessMemory memory;
        memory.workingSetBytes = static_cast<std::size_t>(counters.WorkingSetSize);
        memory.privateBytes = static_cast<std::size_t>(counters.PrivateUsage);
        memory.available = true;
        return memory;
    }
} // namespace GameWIP::Logger::Detail::Platform
