/// @file win32_logger.cpp
/// @brief Windows debugger output, fatal popup, local-time, process-memory, and formatting-scratch backend.

#include "logger/internal/logger_platform.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>

#include <cstring>
#include <deque>
#include <limits>
#include <new>
#include <string>

namespace
{
    struct FormatScratchStorage
    {
        std::deque<std::string> buffers;
        std::size_t depth = 0;
    };

    [[nodiscard]] GameWIP::IO::Types::Status utf8ToWide(std::string_view text, std::wstring &outText)
    {
        outText.clear();
        if (text.empty())
        {
            return {};
        }
        if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        {
            return GameWIP::IO::makeStatus(GameWIP::IO::Types::ErrorCode::SizeLimitExceeded, ERROR_INVALID_PARAMETER);
        }

        const int sourceLength = static_cast<int>(text.size());
        constexpr DWORD flags = MB_ERR_INVALID_CHARS;
        const int wideLength = MultiByteToWideChar(CP_UTF8, flags, text.data(), sourceLength, nullptr, 0);
        if (wideLength <= 0)
        {
            return GameWIP::IO::makeStatus(GameWIP::IO::Types::ErrorCode::EncodingFailed, GetLastError());
        }

        try
        {
            outText.resize(static_cast<std::size_t>(wideLength));
        }
        catch (const std::bad_alloc &)
        {
            return GameWIP::IO::makeStatus(GameWIP::IO::Types::ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return GameWIP::IO::makeStatus(GameWIP::IO::Types::ErrorCode::Unknown);
        }

        if (MultiByteToWideChar(CP_UTF8, flags, text.data(), sourceLength, outText.data(), wideLength) != wideLength)
        {
            return GameWIP::IO::makeStatus(GameWIP::IO::Types::ErrorCode::EncodingFailed, GetLastError());
        }
        return {};
    }

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
    void NTAPI destroyFormatScratch(void *value)
    {
        delete static_cast<FormatScratchStorage *>(value);
    }

    DWORD formatScratchSlot()
    {
        static const DWORD slot = FlsAlloc(destroyFormatScratch);
        return slot;
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
            storage.buffers.pop_back();
        }
    }

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
            if (output.find(L'\0') != std::wstring::npos)
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
            if (messageText.find(L'\0') != std::wstring::npos)
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
            std::memcpy(formatBuffer, timeFormat.data(), timeFormat.size());
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
