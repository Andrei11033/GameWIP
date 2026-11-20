/// @file backend.h
/// @brief Private platform operations used by Logger core.

#pragma once

#include "io/status.h"
#include "logger/internal/format_scratch.h"

#include <cstddef>
#include <cstdint>
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

    /// @brief Returns or creates the current thread's formatting scratch storage.
    [[nodiscard]] FormatScratchStorage &formatScratchStorageForThread();

    /// @brief Returns current-thread scratch storage without creating it.
    [[nodiscard]] FormatScratchStorage *findFormatScratchStorageForThread() noexcept;

    /// @brief Writes one UTF-8 diagnostic line to the platform debugger channel.
    [[nodiscard]] IO::Types::Status writeDebugOutput(std::string_view line);

    /// @brief Shows a fatal diagnostic through the platform popup channel.
    [[nodiscard]] IO::Types::Status showFatalPopup(std::string_view message);

    /// @brief Formats local time using the platform C runtime/backend.
    [[nodiscard]] IO::Types::Status formatLocalTime(std::time_t time, std::string_view timeFormat, std::string &outText);

    /// @brief Queries process memory counters for Logger diagnostic snapshots.
    [[nodiscard]] ProcessMemory queryProcessMemory();

    /// @brief Returns the current process identifier.
    [[nodiscard]] std::uint32_t currentProcessId() noexcept;
} // namespace GameWIP::Logger::Detail::Platform
