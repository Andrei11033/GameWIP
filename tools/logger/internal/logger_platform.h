/// @file logger_platform.h
/// @brief Internal platform boundary for debugger output, fatal UI, local-time conversion, process memory, and format scratch.

#pragma once

#include "logger/logger.h"

#include <ctime>
#include <string>
#include <string_view>

namespace GameWIP::Logger::Detail::Platform
{
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

    /// @brief Queries process-level memory counters from the platform.
    /// @return Current process memory values, or available false on failure.
    ProcessMemory queryProcessMemory();

    /// @brief Returns reusable per-thread format storage through the platform backend.
    /// @return Mutable per-thread scratch string.
    std::string &formatScratchForThread();

    /// @brief Releases the most recently acquired per-thread format scratch lease.
    void releaseFormatScratchForThread() noexcept;
} // namespace GameWIP::Logger::Detail::Platform
