/// @file logger_platform.h
/// @brief Internal platform abstraction used by the GameWIP Logger library.

#pragma once

#include "logger/logger.h"

#include <ctime>
#include <string>
#include <string_view>

namespace GameWIP::Logger::Detail::Platform
{
    /// @brief Opaque platform file handle used by the logger file sink.
    struct FileHandle
    {
        void *native = nullptr;
    };

    /// @brief Process memory values reported by the platform, independent of logger-only accounting.
    struct ProcessMemory
    {
        /// @brief Current process working-set bytes.
        std::size_t workingSetBytes = 0;
        /// @brief Current process private bytes.
        std::size_t privateBytes = 0;
        /// @brief True when the platform query succeeded.
        bool available = false;
    };

    /// @brief Standard console stream queried for ANSI-color support.
    enum class ConsoleStream
    {
        Stdout,
        Stderr
    };

    /// @brief Writes a fully formatted log line to the platform debug output.
    /// @param line Log line, including any desired trailing newline.
    /// @return Platform error details, or source None on success.
    GameWIP::Logger::Types::PlatformError writeDebugOutput(std::string_view line);

    /// @brief Displays a fatal popup through the platform UI.
    /// @param message Message to display.
    /// @return Platform error details, or source None on success.
    GameWIP::Logger::Types::PlatformError showFatalPopup(std::string_view message);

    /// @brief Formats a local-time value using the platform thread-safe localtime API.
    /// @param time Time value to format.
    /// @param timeFormat strftime-compatible format string.
    /// @param outText Receives the formatted text on success.
    /// @return Platform error details, or source None on success.
    GameWIP::Logger::Types::PlatformError formatLocalTime(std::time_t time, std::string_view timeFormat, std::string &outText);

    /// @brief Opens a new file for writing while allowing other processes to read it.
    /// @details The logger owns the writer handle. The Win32 backend requests reader sharing,
    /// but does not request delete/rename sharing.
    /// @param path UTF-8/narrow path text.
    /// @param outHandle Receives the opened platform file handle on success.
    /// @return Success or native platform failure.
    GameWIP::Logger::Types::PlatformError openFileExclusive(std::string_view path, FileHandle &outHandle);

    /// @brief Creates a directory tree if it does not already exist.
    /// @param path UTF-8/narrow directory path.
    /// @return Success or native platform failure.
    GameWIP::Logger::Types::PlatformError createDirectories(std::string_view path);

    /// @brief Writes all bytes to an open file handle.
    /// @param handle File handle opened by openFileExclusive.
    /// @param text Bytes to write.
    /// @return Success or native platform failure.
    GameWIP::Logger::Types::PlatformError writeFile(FileHandle handle, std::string_view text);

    /// @brief Flushes an open file handle to the OS.
    /// @param handle File handle opened by openFileExclusive.
    /// @return Success or native platform failure.
    GameWIP::Logger::Types::PlatformError flushFile(FileHandle handle);

    /// @brief Closes an open file handle.
    /// @param handle File handle opened by openFileExclusive.
    void closeFile(FileHandle handle);

    /// @brief Returns whether a platform file handle is valid/open.
    /// @param handle File handle to inspect.
    /// @return True when native points to an open platform handle.
    bool isFileOpen(FileHandle handle);

    /// @brief Returns true when ANSI color should be emitted to the selected console stream.
    /// @param stream Console stream to query.
    /// @return True for interactive ANSI-capable consoles; false for redirection or unsupported terminals.
    bool supportsAnsiColor(ConsoleStream stream);

    /// @brief Queries process-level memory counters from the platform.
    /// @return Current process memory values, or available false on failure.
    ProcessMemory queryProcessMemory();

    /// @brief Returns reusable per-thread format storage through the platform backend.
    /// @return Mutable per-thread scratch string.
    std::string &formatScratchForThread();
}
