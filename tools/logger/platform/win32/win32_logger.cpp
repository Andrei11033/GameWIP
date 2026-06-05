/// @file win32_logger.cpp
/// @brief Windows platform backend for the Logger library.

#include "logger/internal/logger_platform.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <string>

namespace
{
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

    /// @brief Checks for embedded NUL bytes before forwarding text to null-terminated Win32 APIs.
    /// @param text UTF-8/narrow text supplied to a Win32 API wrapper.
    /// @return True when text contains an embedded NUL byte.
    bool containsNul(std::string_view text) noexcept
    {
        return text.find('\0') != std::string_view::npos;
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

    /// @brief Returns the Win32 standard handle for a logger console stream.
    HANDLE consoleHandle(GameWIP::Logger::Detail::Platform::ConsoleStream stream)
    {
        const DWORD handleId = stream == GameWIP::Logger::Detail::Platform::ConsoleStream::Stderr ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE;
        return GetStdHandle(handleId);
    }

#if defined(__MINGW32__)
    /// @brief Frees MinGW format scratch without using non-trivial C++ TLS destructors.
    void NTAPI destroyFormatScratch(void *value)
    {
        delete static_cast<std::string *>(value);
    }

    /// @brief Returns the FLS slot used for per-thread format scratch on MinGW.
    DWORD formatScratchSlot()
    {
        static const DWORD slot = FlsAlloc(destroyFormatScratch);
        return slot;
    }

    /// @brief Returns MinGW per-thread format scratch through FLS.
    /// @return Mutable per-thread scratch string.
    std::string &formatScratchForThreadFls()
    {
        const DWORD slot = formatScratchSlot();
        if (slot == FLS_OUT_OF_INDEXES)
        {
            throw std::bad_alloc();
        }

        auto *scratch = static_cast<std::string *>(FlsGetValue(slot));
        if (!scratch)
        {
            scratch = new std::string();
            if (!FlsSetValue(slot, scratch))
            {
                delete scratch;
                throw std::bad_alloc();
            }
        }
        return *scratch;
    }
#endif
} // namespace

namespace GameWIP::Logger::Detail::Platform
{
    /// @brief Returns per-thread format scratch storage for Win32 logger formatting.
    /// @return Mutable per-thread scratch string.
    std::string &formatScratchForThread()
    {
#if defined(__MINGW32__)
        return formatScratchForThreadFls();
#else
        thread_local std::string scratch;
        return scratch;
#endif
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

    /// @brief Opens a Win32 file handle with CREATE_NEW to avoid check-then-open races.
    /// @param path UTF-8/narrow path text.
    /// @param outHandle Receives the opened file handle on success.
    /// @return Structured platform error, or source None on success.
    GameWIP::Logger::Types::PlatformError openFileExclusive(std::string_view path, FileHandle &outHandle)
    {
        outHandle = {};

        if (containsNul(path))
        {
            return makePlatformError(PlatformErrorSource::File, ERROR_INVALID_NAME);
        }

        std::wstring pathText;
        unsigned long conversionError = 0;
        if (!utf8ToWide(path, pathText, conversionError))
        {
            return makePlatformError(PlatformErrorSource::File, conversionError);
        }

        HANDLE nativeHandle = CreateFileW(pathText.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (nativeHandle == INVALID_HANDLE_VALUE)
        {
            return makePlatformError(PlatformErrorSource::File, GetLastError());
        }

        outHandle.native = nativeHandle;
        return noPlatformError();
    }

    /// @brief Creates a directory tree using Win32 calls.
    /// @param path UTF-8/narrow directory path.
    /// @return Structured platform error, or source None on success.
    GameWIP::Logger::Types::PlatformError createDirectories(std::string_view path)
    {
        if (path.empty())
        {
            return makePlatformError(PlatformErrorSource::File, ERROR_PATH_NOT_FOUND);
        }
        if (containsNul(path))
        {
            return makePlatformError(PlatformErrorSource::File, ERROR_INVALID_NAME);
        }

        std::wstring normalized;
        unsigned long conversionError = 0;
        if (!utf8ToWide(path, normalized, conversionError))
        {
            return makePlatformError(PlatformErrorSource::File, conversionError);
        }

        std::replace(normalized.begin(), normalized.end(), L'/', L'\\');

        std::size_t start = 0;
        if (normalized.size() >= 3 && normalized[1] == L':' && normalized[2] == L'\\')
        {
            start = 3;
        }
        else if (normalized.size() >= 2 && normalized[0] == L'\\' && normalized[1] == L'\\')
        {
            start = normalized.find(L'\\', 2);
            if (start == std::wstring::npos)
            {
                return makePlatformError(PlatformErrorSource::File, ERROR_BAD_PATHNAME);
            }
            start = normalized.find(L'\\', start + 1);
            if (start == std::wstring::npos)
            {
                return makePlatformError(PlatformErrorSource::File, ERROR_BAD_PATHNAME);
            }
            ++start;
        }

        for (std::size_t position = normalized.find(L'\\', start); position != std::wstring::npos; position = normalized.find(L'\\', position + 1))
        {
            const std::wstring partial = normalized.substr(0, position);
            if (!partial.empty() && CreateDirectoryW(partial.c_str(), nullptr) == 0)
            {
                const DWORD error = GetLastError();
                if (error != ERROR_ALREADY_EXISTS)
                {
                    return makePlatformError(PlatformErrorSource::File, error);
                }
            }
        }

        if (CreateDirectoryW(normalized.c_str(), nullptr) == 0)
        {
            const DWORD error = GetLastError();
            if (error != ERROR_ALREADY_EXISTS)
            {
                return makePlatformError(PlatformErrorSource::File, error);
            }
        }

        const DWORD attributes = GetFileAttributesW(normalized.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES)
        {
            return makePlatformError(PlatformErrorSource::File, GetLastError());
        }
        if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
        {
            return makePlatformError(PlatformErrorSource::File, ERROR_DIRECTORY);
        }

        return noPlatformError();
    }

    /// @brief Writes all bytes to a Win32 file handle.
    /// @param handle File handle opened by openFileExclusive.
    /// @param text Bytes to write.
    /// @return Structured platform error, or source None on success.
    GameWIP::Logger::Types::PlatformError writeFile(FileHandle handle, std::string_view text)
    {
        if (!isFileOpen(handle))
        {
            return makePlatformError(PlatformErrorSource::File, ERROR_INVALID_HANDLE);
        }

        const char *cursor = text.data();
        std::size_t remaining = text.size();
        while (remaining > 0)
        {
            const DWORD chunkSize = remaining > static_cast<std::size_t>(std::numeric_limits<DWORD>::max()) ? std::numeric_limits<DWORD>::max()
                                                                                                            : static_cast<DWORD>(remaining);
            DWORD written = 0;
            if (WriteFile(static_cast<HANDLE>(handle.native), cursor, chunkSize, &written, nullptr) == 0)
            {
                return makePlatformError(PlatformErrorSource::File, GetLastError());
            }
            if (written == 0)
            {
                return makePlatformError(PlatformErrorSource::File, ERROR_WRITE_FAULT);
            }

            cursor += written;
            remaining -= written;
        }

        return noPlatformError();
    }

    /// @brief Flushes a Win32 file handle.
    /// @param handle File handle opened by openFileExclusive.
    /// @return Structured platform error, or source None on success.
    GameWIP::Logger::Types::PlatformError flushFile(FileHandle handle)
    {
        if (!isFileOpen(handle))
        {
            return makePlatformError(PlatformErrorSource::File, ERROR_INVALID_HANDLE);
        }

        if (FlushFileBuffers(static_cast<HANDLE>(handle.native)) == 0)
        {
            return makePlatformError(PlatformErrorSource::File, GetLastError());
        }
        return noPlatformError();
    }

    /// @brief Closes a Win32 file handle.
    /// @param handle File handle opened by openFileExclusive.
    void closeFile(FileHandle handle)
    {
        if (isFileOpen(handle))
        {
            CloseHandle(static_cast<HANDLE>(handle.native));
        }
    }

    /// @brief Checks whether a Win32 file handle is valid.
    /// @param handle File handle to inspect.
    /// @return True when native is neither null nor INVALID_HANDLE_VALUE.
    bool isFileOpen(FileHandle handle)
    {
        return handle.native != nullptr && handle.native != INVALID_HANDLE_VALUE;
    }

    /// @brief Checks whether ANSI color can be safely written to a Win32 console stream.
    bool supportsAnsiColor(ConsoleStream stream)
    {
        const HANDLE handle = consoleHandle(stream);
        if (handle == nullptr || handle == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        DWORD mode = 0;
        if (GetConsoleMode(handle, &mode) == 0)
        {
            return false;
        }

        if ((mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0)
        {
            return true;
        }

        const DWORD updatedMode = mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        if (SetConsoleMode(handle, updatedMode) == 0)
        {
            return false;
        }

        return true;
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
