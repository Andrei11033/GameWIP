#include <algorithm>
#include <chrono>
#include <exception>
#include <format>
#include <string_view>
#include <thread>
#include <vector>

#include "logger/logger.h"
#include "debug/assert/assert.h"
#include "input/input.h"
#include "platform/win32/window.h"

using GameWIP::Logger;
using GameWIP::LogLevel;
using GameWIP::OutputMode;
using GameWIP::Input::InputState;
using GameWIP::Input::Key;
using GameWIP::Platform::Win32::CursorMode;
using GameWIP::Platform::Win32::DisplayMode;
using GameWIP::Platform::Win32::MonitorInfo;
using GameWIP::Platform::Win32::Window;
using GameWIP::Platform::Win32::WindowDescription;
using GameWIP::Platform::Win32::WindowMode;
using GameWIP::Platform::Win32::WindowResult;

namespace
{
    constexpr OutputMode defaultOutputMode = OutputMode::BOTH;
    constexpr LogLevel defaultLogLevel = LogLevel::INFO;
    constexpr std::size_t defaultQueueSize = 1024;
    constexpr std::string_view mainLogSource = "Main";
    constexpr int defaultWindowWidth = 1280;
    constexpr int defaultWindowHeight = 720;

    /// @brief Converts a window result to readable log text.
    /// @param result Result code to convert.
    /// @return Static text for the result code.
    std::string_view toString(WindowResult result)
    {
        switch (result)
        {
        case WindowResult::Success:
            return "Success";
        case WindowResult::NotCreated:
            return "NotCreated";
        case WindowResult::InvalidDescription:
            return "InvalidDescription";
        case WindowResult::InvalidSize:
            return "InvalidSize";
        case WindowResult::InvalidMonitor:
            return "InvalidMonitor";
        case WindowResult::Win32CallFailed:
            return "Win32CallFailed";
        case WindowResult::MissingWindowedPlacement:
            return "MissingWindowedPlacement";
        case WindowResult::MissingDisplayMode:
            return "MissingDisplayMode";
        case WindowResult::ModeChangeFailed:
            return "ModeChangeFailed";
        default:
            return "Unknown";
        }
    }

    /// @brief Builds a string describing a monitor rectangle.
    /// @param left Rectangle left coordinate.
    /// @param top Rectangle top coordinate.
    /// @param right Rectangle right coordinate.
    /// @param bottom Rectangle bottom coordinate.
    /// @return Human-readable rectangle string.
    std::string formatRect(int left, int top, int right, int bottom)
    {
        return std::format("{}x{}+{}+{}", right - left, bottom - top, left, top);
    }

    /// @brief Builds a string describing one monitor.
    /// @param index Display index used in logs.
    /// @param monitor Monitor data to format.
    /// @return Human-readable monitor string.
    std::string formatMonitorInfo(int index, const MonitorInfo &monitor)
    {
        return std::format(
            "Monitor {}: {} primary={} workArea={} monitorArea={}",
            index,
            monitor.deviceName.empty() ? "(unnamed)" : monitor.deviceName,
            monitor.isPrimary ? "true" : "false",
            formatRect(monitor.workArea.left, monitor.workArea.top, monitor.workArea.right, monitor.workArea.bottom),
            formatRect(monitor.monitorArea.left, monitor.monitorArea.top, monitor.monitorArea.right, monitor.monitorArea.bottom));
    }

    /// @brief Builds a string describing one display mode.
    /// @param mode Display mode to format.
    /// @return Human-readable display mode string.
    std::string formatDisplayMode(const DisplayMode &mode)
    {
        return std::format("{}x{} {}Hz {}bpp", mode.width, mode.height, mode.refreshRate, mode.bitsPerPixel);
    }

    /// @brief Logs a failed window operation with its result and Win32 error code.
    /// @param operation Operation that failed.
    /// @param window Window storing the last Win32 error.
    /// @param result Result code returned by the operation.
    void logWindowFailure(std::string_view operation, const Window &window, WindowResult result)
    {
        Logger::log(
            LogLevel::ERR,
            mainLogSource,
            std::format(
                "{} failed with result {} and Win32 error {}.",
                operation,
                toString(result),
                window.getLastWin32Error()));
    }

    /// @brief The main game loop. Initializes subsystems, runs the game, and handles shutdown.
    /// @return Returns 0 on clean shutdown, non-zero on error.
    int runGame()
    {
        Logger::log(LogLevel::INFO, mainLogSource, "GameWIP starting up.");

        InputState input;
        Window window;

        WindowDescription windowDescription{
            .title = "GameWIP",
            .width = defaultWindowWidth,
            .height = defaultWindowHeight,
            .mode = WindowMode::Windowed,
            .resizable = true};

        Logger::log(LogLevel::INFO, mainLogSource, "Creating Win32 window.");
        WindowResult createResult = window.create(windowDescription);
        if (createResult != WindowResult::Success)
        {
            logWindowFailure("Creating Win32 window", window, createResult);
            return 1;
        }

        constexpr int minimumClientWidth = 120;
        constexpr int minimumClientHeight = 120;
        window.setMinClientSize(minimumClientWidth, minimumClientHeight);
        Logger::log(LogLevel::INFO, mainLogSource, std::format("Applied minimum client-size constraint: min={}x{}.", minimumClientWidth, minimumClientHeight));

        std::vector<MonitorInfo> monitors;
        if (WindowResult result = window.getMonitors(monitors); result != WindowResult::Success)
        {
            logWindowFailure("Querying monitors", window, result);
        }

        Logger::log(LogLevel::INFO, mainLogSource, std::format("Detected {} monitors.", monitors.size()));
        for (std::size_t i = 0; i < monitors.size(); ++i)
        {
            Logger::log(LogLevel::INFO, mainLogSource, formatMonitorInfo(static_cast<int>(i), monitors[i]));
        }

        MonitorInfo currentMonitor{};
        WindowResult currentMonitorResult = window.getCurrentMonitor(currentMonitor);
        if (currentMonitorResult == WindowResult::Success && currentMonitor.handle != nullptr)
        {
            Logger::log(LogLevel::INFO, mainLogSource, std::format("Current monitor: {}", currentMonitor.deviceName.empty() ? "(unnamed)" : currentMonitor.deviceName));

            DisplayMode displayMode{};
            if (WindowResult displayResult = window.getCurrentDisplayMode(displayMode); displayResult == WindowResult::Success)
            {
                Logger::log(LogLevel::INFO, mainLogSource, std::format("Current display mode: {}", formatDisplayMode(displayMode)));
            }
            else
            {
                logWindowFailure("Querying current display mode", window, displayResult);
            }

            std::vector<DisplayMode> supportedModes;
            if (WindowResult displayModesResult = window.getDisplayModes(currentMonitor, supportedModes); displayModesResult != WindowResult::Success)
            {
                logWindowFailure("Querying supported display modes", window, displayModesResult);
            }

            Logger::log(LogLevel::INFO, mainLogSource, std::format("Supported display modes: {} entries.", supportedModes.size()));

            int maxWidth = 1920;
            int maxHeight = 1080;
            for (const auto &mode : supportedModes)
            {
                maxWidth = std::max(maxWidth, mode.width);
                maxHeight = std::max(maxHeight, mode.height);
            }

            window.setMaxClientSize(maxWidth, maxHeight);
            Logger::log(LogLevel::INFO, mainLogSource, std::format("Applied client-size constraints: min={}x{} max={}x{}.", minimumClientWidth, minimumClientHeight, maxWidth, maxHeight));

            if (!supportedModes.empty())
            {
                Logger::log(LogLevel::INFO, mainLogSource, std::format("First supported mode: {}", formatDisplayMode(supportedModes.front())));
            }
        }
        else
        {
            if (currentMonitorResult != WindowResult::Success)
            {
                logWindowFailure("Querying current monitor", window, currentMonitorResult);
            }

            window.setMaxClientSize(1920, 1080);
            Logger::log(LogLevel::INFO, mainLogSource, std::format("Applied client-size constraints: min={}x{} max=1920x1080 (fallback, no current monitor detected).", minimumClientWidth, minimumClientHeight));
        }

        int lastMode = 0;
        int resolutionToggle = 0;
        bool cursorVisible = true;
        bool cursorConfined = false;

        while (!window.shouldClose())
        {
            input.advanceFrame();
            window.pollEvents(input);

            WindowResult titleResult = window.setTitle(std::format("GameWIP - Client Size: {}x{}", window.getClientWidth(), window.getClientHeight()));
            if (titleResult != WindowResult::Success)
            {
                logWindowFailure("Setting window title", window, titleResult);
            }

            if (input.wasKeyPressed(Key::Escape))
            {
                Logger::log(LogLevel::INFO, mainLogSource, "Escape key pressed. Requesting window close.");
                window.requestClose();
            }

            if (input.wasKeyPressed(Key::Enter))
            {
                lastMode++;
                switch (lastMode % 3)
                {
                case 0:
                    if (WindowResult result = window.setMode(WindowMode::Windowed); result != WindowResult::Success)
                    {
                        logWindowFailure("Switching to windowed mode", window, result);
                    }
                    break;
                case 1:
                    if (WindowResult result = window.setMode(WindowMode::BorderlessFullscreen); result != WindowResult::Success)
                    {
                        logWindowFailure("Switching to borderless fullscreen mode", window, result);
                    }
                    break;
                case 2:
                    if (WindowResult result = window.setMode(WindowMode::Fullscreen); result != WindowResult::Success)
                    {
                        logWindowFailure("Switching to fullscreen mode", window, result);
                    }
                    break;
                default:
                    break;
                }
            }

            if (input.wasKeyPressed(Key::Q))
            {
                cursorVisible = !cursorVisible;
                window.setCursorVisible(cursorVisible);

                Logger::log(
                    LogLevel::INFO,
                    mainLogSource,
                    cursorVisible ? "Cursor shown." : "Cursor hidden.");
            }

            if (input.wasKeyPressed(Key::E))
            {
                cursorConfined = !cursorConfined;
                window.setCursorConfined(cursorConfined);

                Logger::log(
                    LogLevel::INFO,
                    mainLogSource,
                    cursorConfined ? "Cursor confined." : "Cursor released.");
            }

            if (input.wasKeyPressed(Key::Space))
            {
                resolutionToggle++;
                switch (resolutionToggle % 3)
                {
                case 0:
                    if (WindowResult result = window.setClientSize(defaultWindowWidth, defaultWindowHeight); result != WindowResult::Success)
                    {
                        logWindowFailure("Setting default client size", window, result);
                    }
                    break;
                case 1:
                    if (WindowResult result = window.setClientSize(800, 600); result != WindowResult::Success)
                    {
                        logWindowFailure("Setting 800x600 client size", window, result);
                    }
                    break;
                case 2:
                    if (WindowResult result = window.setClientSize(1024, 768); result != WindowResult::Success)
                    {
                        logWindowFailure("Setting 1024x768 client size", window, result);
                    }
                    break;
                default:
                    break;
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        Logger::log(LogLevel::INFO, mainLogSource, "Window close requested.");
        if (WindowResult result = window.destroy(); result != WindowResult::Success)
        {
            logWindowFailure("Destroying Win32 window", window, result);
        }

        Logger::log(LogLevel::INFO, mainLogSource, "GameWIP shutting down cleanly.");
        return 0;
    }
}

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
