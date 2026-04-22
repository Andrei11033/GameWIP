#include <iostream>
#include <optional>
#include <string_view>
#include "logger/logger.h"

using GameWIP::Logger;
using GameWIP::LogLevel;
using GameWIP::OutputMode;

namespace
{
    std::optional<OutputMode> parseOutputMode(std::string_view value)
    {
        if (value == "none")
        {
            return OutputMode::NONE;
        }

        if (value == "console")
        {
            return OutputMode::CONSOLE;
        }

        if (value == "file")
        {
            return OutputMode::FILE;
        }

        if (value == "both")
        {
            return OutputMode::BOTH;
        }

        return std::nullopt;
    }

    std::optional<LogLevel> parseLogLevel(std::string_view value)
    {
        if (value == "info")
        {
            return LogLevel::INFO;
        }

        if (value == "warn")
        {
            return LogLevel::WARN;
        }

        if (value == "error")
        {
            return LogLevel::ERR;
        }

        if (value == "fatal")
        {
            return LogLevel::FATAL;
        }

        return std::nullopt;
    }
} // namespace

int main(int argc, char *argv[])
{
    OutputMode mode = OutputMode::BOTH;
    LogLevel level = LogLevel::INFO;

    if (argc > 1)
    {
        std::optional<OutputMode> parsedMode = parseOutputMode(argv[1]);

        if (!parsedMode.has_value())
        {
            std::cerr << "Usage: GameWIP.exe [none|console|file|both] [info|warn|error|fatal]" << std::endl;
            return 1;
        }

        mode = parsedMode.value();
    }

    if (argc > 2)
    {
        std::optional<LogLevel> parsedLevel = parseLogLevel(argv[2]);

        if (!parsedLevel.has_value())
        {
            std::cerr << "Usage: GameWIP.exe [none|console|file|both] [info|warn|error|fatal]" << std::endl;
            return 1;
        }

        level = parsedLevel.value();
    }

    if (argc > 3)
    {
        std::cerr << "Usage: GameWIP.exe [none|console|file|both] [info|warn|error|fatal]" << std::endl;
        return 1;
    }

    Logger::init(mode, level);

    Logger::log(LogLevel::INFO, "Main", "Info test message");
    Logger::log(LogLevel::WARN, "Main", "Warn test message");
    Logger::log(LogLevel::ERR, "Main", "Error test message");
    Logger::log(LogLevel::FATAL, "Main", "Fatal test message");
    Logger::shutdown();
    Logger::log(LogLevel::INFO, "Main", "This message should not be logged because the logger is shutdown.");
    Logger::logDBWIN(LogLevel::INFO, "Main", "This message should appear in the Windows debug output.");
    return 0;
}