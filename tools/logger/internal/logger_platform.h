/// @file logger_platform.h
/// @brief Private platform bridge for Logger.

#pragma once

#include "io/status.h"

#include <cstddef>
#include <ctime>
#include <string>
#include <string_view>

namespace GameWIP::Logger::Detail::Platform
{
    /// @brief Best-effort process memory counters for Logger diagnostics.
    struct ProcessMemory
    {
        std::size_t workingSetBytes = 0; ///< Resident working-set bytes when available.
        std::size_t privateBytes = 0;    ///< Private committed bytes when available.
        bool available = false;          ///< Whether the platform backend returned memory counters.
    };

    /// @brief Returns the current thread's nested formatting scratch buffer.
    /// @details Public formatting templates use platform-owned storage so MinGW can replace compiler TLS with FLS.
    std::string &formatScratchForThread();

    /// @brief Releases one nested formatting scratch lease for the current thread.
    void releaseFormatScratchForThread() noexcept;

    /// @brief Writes one UTF-8 diagnostic line to the platform debugger channel.
    [[nodiscard]] IO::Types::Status writeDebugOutput(std::string_view line);

    /// @brief Shows a fatal diagnostic through the platform popup channel.
    [[nodiscard]] IO::Types::Status showFatalPopup(std::string_view message);

    /// @brief Formats local time using the platform C runtime/backend.
    [[nodiscard]] IO::Types::Status formatLocalTime(std::time_t time, std::string_view timeFormat, std::string &outText);

    /// @brief Queries process memory counters for Logger diagnostic snapshots.
    [[nodiscard]] ProcessMemory queryProcessMemory();
} // namespace GameWIP::Logger::Detail::Platform
