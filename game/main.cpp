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
using GameWIP::Input::makeKeyboardKey;
using GameWIP::Platform::Win32::DisplayMode;
using GameWIP::Platform::Win32::MonitorInfo;
using GameWIP::Platform::Win32::Window;
using GameWIP::Platform::Win32::WindowDescription;
using GameWIP::Platform::Win32::WindowEvent;
using GameWIP::Platform::Win32::WindowEventType;
using GameWIP::Platform::Win32::WindowMode;
using GameWIP::Platform::Win32::WindowResult;

namespace KeyboardControlCode = GameWIP::Input::KeyboardControlCode;

namespace
{
    constexpr OutputMode defaultOutputMode = OutputMode::BOTH;
    constexpr LogLevel defaultLogLevel = LogLevel::INFO;
    constexpr std::size_t defaultQueueSize = 1024;
    constexpr std::string_view mainLogSource = "Main";
    constexpr int defaultWindowWidth = 1280;
    constexpr int defaultWindowHeight = 720;
    constexpr auto closeWindowControl = makeKeyboardKey(KeyboardControlCode::Escape);

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
        case WindowResult::InvalidDisplayMode:
            return "InvalidDisplayMode";
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

    /// @brief Converts a window mode to readable log text.
    /// @param mode Window mode to convert.
    /// @return Static text for the mode.
    std::string_view toString(WindowMode mode)
    {
        switch (mode)
        {
        case WindowMode::Windowed:
            return "Windowed";
        case WindowMode::BorderlessFullscreen:
            return "BorderlessFullscreen";
        case WindowMode::Fullscreen:
            return "Fullscreen";
        default:
            return "Unknown";
        }
    }

    /// @brief Converts a window event type to readable log text.
    /// @param type Event type to convert.
    /// @return Static text for the event type.
    std::string_view toString(WindowEventType type)
    {
        switch (type)
        {
        case WindowEventType::CloseRequested:
            return "CloseRequested";
        case WindowEventType::Destroyed:
            return "Destroyed";
        case WindowEventType::Resized:
            return "Resized";
        case WindowEventType::Moved:
            return "Moved";
        case WindowEventType::Focused:
            return "Focused";
        case WindowEventType::LostFocus:
            return "LostFocus";
        case WindowEventType::Minimized:
            return "Minimized";
        case WindowEventType::Maximized:
            return "Maximized";
        case WindowEventType::Restored:
            return "Restored";
        case WindowEventType::ModeChanged:
            return "ModeChanged";
        case WindowEventType::MonitorChanged:
            return "MonitorChanged";
        case WindowEventType::DisplayChanged:
            return "DisplayChanged";
        case WindowEventType::DpiChanged:
            return "DpiChanged";
        case WindowEventType::CursorEntered:
            return "CursorEntered";
        case WindowEventType::CursorLeft:
            return "CursorLeft";
        case WindowEventType::FileDropped:
            return "FileDropped";
        case WindowEventType::Suspended:
            return "Suspended";
        case WindowEventType::Resumed:
            return "Resumed";
        case WindowEventType::Occluded:
            return "Occluded";
        case WindowEventType::Visible:
            return "Visible";
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

    /// @brief Logs and clears a passive window-handler error if one was recorded.
    /// @param window Window storing async handler errors.
    void logWindowAsyncError(Window &window)
    {
        if (!window.hasAsyncError())
        {
            return;
        }

        Logger::log(
            LogLevel::ERR,
            mainLogSource,
            std::format(
                "Async window handler failed with result {} and Win32 error {}.",
                toString(window.getLastAsyncResult()),
                window.getLastAsyncWin32Error()));
        window.clearAsyncError();
    }

    /// @brief Logs one queued window event.
    /// @param event Event to log.
    void logWindowEvent(const WindowEvent &event)
    {
        switch (event.type)
        {
        case WindowEventType::Resized:
            Logger::log(LogLevel::INFO, mainLogSource, std::format("Window event: Resized {}x{}.", event.width, event.height));
            break;
        case WindowEventType::Moved:
            Logger::log(LogLevel::INFO, mainLogSource, std::format("Window event: Moved {},{}.", event.x, event.y));
            break;
        case WindowEventType::DpiChanged:
            Logger::log(LogLevel::INFO, mainLogSource, std::format("Window event: DpiChanged dpi={} client={}x{}.", event.dpi, event.width, event.height));
            break;
        case WindowEventType::ModeChanged:
            Logger::log(LogLevel::INFO, mainLogSource, std::format("Window event: ModeChanged {} client={}x{}.", toString(event.mode), event.width, event.height));
            break;
        case WindowEventType::FileDropped:
            Logger::log(LogLevel::INFO, mainLogSource, std::format("Window event: FileDropped {}.", event.filePath));
            break;
        default:
            Logger::log(LogLevel::INFO, mainLogSource, std::format("Window event: {}.", toString(event.type)));
            break;
        }
    }

    /// @brief Drains and logs all queued window events.
    /// @param window Window to drain.
    void logWindowEvents(Window &window)
    {
        WindowEvent event{};
        while (window.popEvent(event))
        {
            logWindowEvent(event);
        }
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

        while (!window.shouldClose())
        {
            input.advanceFrame();
            window.pollEvents(input);
            logWindowAsyncError(window);

            if (input.wasButtonPressed(closeWindowControl))
            {
                Logger::log(LogLevel::INFO, mainLogSource, "Escape key pressed. Requesting window close.");
                window.requestClose();
            }

            logWindowEvents(window);

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