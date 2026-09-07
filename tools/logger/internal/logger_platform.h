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
    struct ProcessMemory
    {
        std::size_t workingSetBytes = 0;
        std::size_t privateBytes = 0;
        bool available = false;
    };

    std::string &formatScratchForThread();
    void releaseFormatScratchForThread() noexcept;

    [[nodiscard]] IO::Types::Status writeDebugOutput(std::string_view line);
    [[nodiscard]] IO::Types::Status showFatalPopup(std::string_view message);
    [[nodiscard]] IO::Types::Status formatLocalTime(std::time_t time, std::string_view timeFormat, std::string &outText);
    [[nodiscard]] ProcessMemory queryProcessMemory();
} // namespace GameWIP::Logger::Detail::Platform
