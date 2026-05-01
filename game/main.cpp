#include <chrono>
#include <exception>
#include <string_view>
#include <thread>

#include "logger/logger.h"
#include "debug/assert/assert.h"
#include "input/input.h"
#include "platform/win32/window.h"

using GameWIP::Logger;
using GameWIP::LogLevel;
using GameWIP::OutputMode;
using GameWIP::Input::InputState;
using GameWIP::Input::Key;
using GameWIP::Platform::Win32::Window;
using GameWIP::Platform::Win32::WindowDescription;

namespace
{
    constexpr OutputMode defaultOutputMode = OutputMode::BOTH;
    constexpr LogLevel defaultLogLevel = LogLevel::INFO;
    constexpr std::size_t defaultQueueSize = 1024;
    constexpr std::string_view mainLogSource = "Main";
    constexpr int defaultWindowWidth = 1280;
    constexpr int defaultWindowHeight = 720;

    int runGame()
    {
        Logger::log(LogLevel::INFO, mainLogSource, "GameWIP startup complete.");

        InputState input;
        Window window;

        const WindowDescription windowDescription{
            "GameWIP",
            defaultWindowWidth,
            defaultWindowHeight};

        Logger::log(LogLevel::INFO, mainLogSource, "Creating Win32 window.");
        if (!window.create(windowDescription))
        {
            Logger::log(LogLevel::ERR, mainLogSource, "Failed to create Win32 window.");
            return 1;
        }

        while (!window.shouldClose())
        {
            input.advanceFrame();
            window.pollEvents(input);
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }

        Logger::log(LogLevel::INFO, mainLogSource, "Window close requested.");
        window.destroy();

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
        GAMEWIP_ASSERT_MSG(exitCode == 0, "Game exited with non-zero exit code.");
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
