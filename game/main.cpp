#include <exception>
#include <string_view>
#include "logger/logger.h"

using GameWIP::Logger;
using GameWIP::LogLevel;
using GameWIP::OutputMode;

namespace
{
    constexpr OutputMode defaultOutputMode = OutputMode::BOTH;
    constexpr LogLevel defaultLogLevel = LogLevel::INFO;
    constexpr std::size_t defaultQueueSize = 1024;
    constexpr std::string_view mainLogSource = "Main";

    int runGame()
    {
        Logger::log(LogLevel::INFO, mainLogSource, "GameWIP startup complete.");

        // TODO: Initialize the next engine/game system here.
        // TODO: Add the main loop here once the bootstrap stage is ready.

        Logger::log(LogLevel::INFO, mainLogSource, "GameWIP shutting down cleanly.");
        return 0;
    }
} // namespace

int main()
{
    Logger::init(defaultOutputMode, defaultLogLevel, defaultQueueSize);

    int exitCode = 0;

    try
    {
        exitCode = runGame();
    }
    catch (const std::exception &error)
    {
        Logger::log(LogLevel::FATAL, mainLogSource, error.what());
        Logger::logDBWIN(LogLevel::FATAL, mainLogSource, error.what());
        Logger::flush();
        Logger::fatalPopUp(error.what());
        exitCode = 1;
    }
    catch (...)
    {
        constexpr std::string_view unknownErrorMessage = "Unhandled non-standard exception.";
        Logger::log(LogLevel::FATAL, mainLogSource, unknownErrorMessage);
        Logger::logDBWIN(LogLevel::FATAL, mainLogSource, unknownErrorMessage);
        Logger::flush();
        Logger::fatalPopUp(unknownErrorMessage);
        exitCode = 1;
    }

    Logger::shutdown();
    return exitCode;
}