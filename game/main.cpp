#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <format>
#include <string_view>
#include <thread>
#include <vector>
#include <tracy/Tracy.hpp>

#include "logger/logger.h"
#include "debug/assert/assert.h"
#include "input/input.h"
#include "window/window.h"
#include "window_manager/window_manager.h"

namespace
{
    namespace KeyboardControlCode = GameWIP::Input::KeyboardControlCode;
    using GameWIP::Logger;
    using GameWIP::LogLevel;
    using GameWIP::OutputMode;
    using GameWIP::Input::InputState;
    using GameWIP::Input::makeKeyboardKey;
    using DisplayMode = GameWIP::Window::DisplayMode;
    using MonitorInfo = GameWIP::Window::MonitorInfo;
    using GameWIP::Window;
    using WindowDescription = GameWIP::Window::Description;
    using WindowEvent = GameWIP::Window::Event;
    using WindowEventType = GameWIP::Window::EventType;
    using WindowMode = GameWIP::Window::Mode;
    using WindowResult = GameWIP::Window::Result;
    using WindowRole = GameWIP::Window::Role;
    using GameWIP::WindowManager;

    // Configuration

    constexpr OutputMode defaultOutputMode = OutputMode::BOTH;                        // Default logger output destination.
    constexpr LogLevel defaultLogLevel = LogLevel::INFO;                              // Default logger minimum level.
    constexpr std::size_t defaultQueueSize = 1024;                                    // Default logger queue size.
    constexpr std::string_view mainLogSource = "Main";                                // Source tag for main logs.
    constexpr int defaultWindowWidth = 1280;                                          // Initial client-area width.
    constexpr int defaultWindowHeight = 720;                                          // Initial client-area height.
    constexpr int minimumClientWidth = 120;                                           // Smallest allowed client-area width.
    constexpr int minimumClientHeight = 120;                                          // Smallest allowed client-area height.
    constexpr int fallbackMaxClientWidth = 1920;                                      // Fallback maximum client-area width.
    constexpr int fallbackMaxClientHeight = 1080;                                     // Fallback maximum client-area height.
    constexpr std::chrono::milliseconds frameSleepDuration{100};                      // Temporary frame pacing until real timing exists.
    constexpr auto closeWindowControl = makeKeyboardKey(KeyboardControlCode::Escape); // Input control used to request shutdown.

#ifdef GAMEWIP_ENABLE_TOOLS
#ifdef GAMEWIP_OPEN_TOOL_WINDOWS_AT_STARTUP
    constexpr bool openToolWindowsAtStartup = true;
#else
    constexpr bool openToolWindowsAtStartup = false;
#endif
    constexpr int defaultToolWindowWidth = 640;
    constexpr int defaultToolWindowHeight = 480;
    constexpr int defaultDebugWindowWidth = 640;
    constexpr int defaultDebugWindowHeight = 360;
#endif

    // Tracy helpers

#ifdef TRACY_ENABLE
    /// @brief Configures one-time Tracy metadata and plots for main-loop captures.
    void configureTracySession()
    {
        constexpr std::string_view appInfo = "GameWIP main.cpp instrumentation: startup, window events, frame timing, and shutdown.";
        TracyAppInfo(appInfo.data(), appInfo.size());
        TracyPlotConfig("Frame time (ms)", tracy::PlotFormatType::Number, false, true, tracy::Color::Orange);
        TracyPlotConfig("Window events/frame", tracy::PlotFormatType::Number, true, true, tracy::Color::DodgerBlue);
        TracyMessageLC("GameWIP Tracy session configured.", tracy::Color::SeaGreen);
    }
#endif

    // Window description helpers

    /// @brief Builds the default window description for the game.
    /// @return Default platform window description.
    WindowDescription makeDefaultWindowDescription()
    {
        return WindowDescription{
            .title = "GameWIP",
            .width = defaultWindowWidth,
            .height = defaultWindowHeight,
            .mode = WindowMode::Windowed,
            .role = WindowRole::MainGame,
            .resizable = true};
    }

#ifdef GAMEWIP_ENABLE_TOOLS
    WindowDescription makeToolWindowDescription()
    {
        return WindowDescription{
            .title = "GameWIP Tools",
            .width = defaultToolWindowWidth,
            .height = defaultToolWindowHeight,
            .mode = WindowMode::Windowed,
            .role = WindowRole::Tool,
            .resizable = true};
    }

    WindowDescription makeDebugWindowDescription()
    {
        return WindowDescription{
            .title = "GameWIP Debug",
            .width = defaultDebugWindowWidth,
            .height = defaultDebugWindowHeight,
            .mode = WindowMode::Windowed,
            .role = WindowRole::Debug,
            .resizable = true};
    }
#endif

    // Formatting helpers

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
        case WindowResult::PlatformCallFailed:
            return "PlatformCallFailed";
        case WindowResult::MissingWindowedPlacement:
            return "MissingWindowedPlacement";
        case WindowResult::MissingDisplayMode:
            return "MissingDisplayMode";
        case WindowResult::ModeChangeFailed:
            return "ModeChangeFailed";
        case WindowResult::OperationNotAllowed:
            return "OperationNotAllowed";
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

    /// @brief Converts a window role to readable log text.
    /// @param role Window role to convert.
    /// @return Static text for the role.
    std::string_view toString(WindowRole role)
    {
        switch (role)
        {
        case WindowRole::MainGame:
            return "MainGame";
        case WindowRole::SecondaryGameView:
            return "SecondaryGameView";
        case WindowRole::Tool:
            return "Tool";
        case WindowRole::Debug:
            return "Debug";
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

    // Logging helpers

    /// @brief Logs a failed window operation with its result and platform error code.
    /// @param operation Operation that failed.
    /// @param window Window storing the last platform error.
    /// @param result Result code returned by the operation.
    void logWindowFailure(std::string_view operation, const Window &window, WindowResult result)
    {
        Logger::log(
            LogLevel::ERR,
            mainLogSource,
            std::format(
                "{} failed for {} window with result {} and platform error {}.",
                operation,
                toString(window.getRole()),
                toString(result),
                window.getLastPlatformError()));
    }

    /// @brief Logs a failed window-system operation when no window object can provide a platform error.
    /// @param operation Operation that failed.
    /// @param result Result code returned by the operation.
    void logWindowFailure(std::string_view operation, WindowResult result)
    {
        Logger::log(
            LogLevel::ERR,
            mainLogSource,
            std::format("{} failed with result {}.", operation, toString(result)));
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
                "Async {} window handler failed with result {} and platform error {}.",
                toString(window.getRole()),
                toString(window.getLastAsyncResult()),
                window.getLastAsyncPlatformError()));
        window.clearAsyncError();
    }

    // Window setup helpers

    /// @brief Creates the main game window.
    /// @param windows Window system that will own the native window.
    /// @param outWindow Receives the main window on success.
    /// @return Result code from the create operation.
    WindowResult createMainWindow(WindowManager &windows, Window *&outWindow)
    {
        ZoneScopedNC("Create platform window", tracy::Color::SeaGreen);
        Logger::log(LogLevel::INFO, mainLogSource, "Creating platform window.");
        return windows.createWindow(makeDefaultWindowDescription(), outWindow);
    }

#ifdef GAMEWIP_ENABLE_TOOLS
    void createOptionalStartupToolWindows(WindowManager &windows)
    {
        if (!openToolWindowsAtStartup)
        {
            return;
        }

        Window *toolWindow = nullptr;
        WindowResult toolResult = windows.createWindow(makeToolWindowDescription(), toolWindow);
        if (toolResult != WindowResult::Success)
        {
            logWindowFailure("Creating tool window", toolResult);
        }

        Window *debugWindow = nullptr;
        WindowResult debugResult = windows.createWindow(makeDebugWindowDescription(), debugWindow);
        if (debugResult != WindowResult::Success)
        {
            logWindowFailure("Creating debug window", debugResult);
        }
    }
#endif

    /// @brief Applies startup client-size constraints using monitor/display information when available.
    /// @param window Window to configure.
    void applyStartupClientSizeConstraints(Window &window)
    {
        {
            ZoneScopedNC("Apply minimum client-size constraint", tracy::Color::DarkSeaGreen);
            window.setMinClientSize(minimumClientWidth, minimumClientHeight);
            Logger::log(LogLevel::INFO, mainLogSource, std::format("Applied minimum client-size constraint: min={}x{}.", minimumClientWidth, minimumClientHeight));
        }

        {
            ZoneScopedNC("Query monitor and display state", tracy::Color::RoyalBlue);

            std::vector<MonitorInfo> monitors;
            if (WindowResult result = window.getMonitors(monitors); result != WindowResult::Success)
            {
                logWindowFailure("Querying monitors", window, result);
            }

            ZoneValue(static_cast<std::uint64_t>(monitors.size()));
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
                {
                    ZoneScopedNC("Query current display mode", tracy::Color::CornflowerBlue);
                    if (WindowResult displayResult = window.getCurrentDisplayMode(displayMode); displayResult == WindowResult::Success)
                    {
                        Logger::log(LogLevel::INFO, mainLogSource, std::format("Current display mode: {}", formatDisplayMode(displayMode)));
                    }
                    else
                    {
                        logWindowFailure("Querying current display mode", window, displayResult);
                    }
                }

                std::vector<DisplayMode> supportedModes;
                {
                    ZoneScopedNC("Query supported display modes", tracy::Color::DodgerBlue);
                    if (WindowResult displayModesResult = window.getDisplayModes(currentMonitor, supportedModes); displayModesResult != WindowResult::Success)
                    {
                        logWindowFailure("Querying supported display modes", window, displayModesResult);
                    }
                }

                ZoneValue(static_cast<std::uint64_t>(supportedModes.size()));
                Logger::log(LogLevel::INFO, mainLogSource, std::format("Supported display modes: {} entries.", supportedModes.size()));

                int maxWidth = fallbackMaxClientWidth;
                int maxHeight = fallbackMaxClientHeight;
                for (const DisplayMode &mode : supportedModes)
                {
                    maxWidth = std::max(maxWidth, mode.width);
                    maxHeight = std::max(maxHeight, mode.height);
                }

                {
                    ZoneScopedNC("Apply maximum client-size constraint", tracy::Color::DarkSeaGreen);
                    window.setMaxClientSize(maxWidth, maxHeight);
                    Logger::log(LogLevel::INFO, mainLogSource, std::format("Applied client-size constraints: min={}x{} max={}x{}.", minimumClientWidth, minimumClientHeight, maxWidth, maxHeight));
                }

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

                {
                    ZoneScopedNC("Apply fallback maximum client-size constraint", tracy::Color::DarkSeaGreen);
                    window.setMaxClientSize(fallbackMaxClientWidth, fallbackMaxClientHeight);
                    Logger::log(
                        LogLevel::INFO,
                        mainLogSource,
                        std::format(
                            "Applied client-size constraints: min={}x{} max={}x{} (fallback, no current monitor detected).",
                            minimumClientWidth,
                            minimumClientHeight,
                            fallbackMaxClientWidth,
                            fallbackMaxClientHeight));
                }
            }
        }
    }

    // Window event logging

    /// @brief Logs one queued window event.
    /// @param window Window that produced the event.
    /// @param event Event to log.
    void logWindowEvent(const Window &window, const WindowEvent &event)
    {
        switch (event.type)
        {
        case WindowEventType::Resized:
            Logger::log(LogLevel::INFO, mainLogSource, std::format("{} window event: Resized {}x{}.", toString(window.getRole()), event.width, event.height));
            break;
        case WindowEventType::Moved:
            Logger::log(LogLevel::INFO, mainLogSource, std::format("{} window event: Moved {},{}.", toString(window.getRole()), event.x, event.y));
            break;
        case WindowEventType::DpiChanged:
            Logger::log(LogLevel::INFO, mainLogSource, std::format("{} window event: DpiChanged dpi={} client={}x{}.", toString(window.getRole()), event.dpi, event.width, event.height));
            break;
        case WindowEventType::ModeChanged:
            Logger::log(LogLevel::INFO, mainLogSource, std::format("{} window event: ModeChanged {} client={}x{}.", toString(window.getRole()), toString(event.mode), event.width, event.height));
            break;
        case WindowEventType::FileDropped:
            Logger::log(LogLevel::INFO, mainLogSource, std::format("{} window event: FileDropped {}.", toString(window.getRole()), event.filePath));
            break;
        default:
            Logger::log(LogLevel::INFO, mainLogSource, std::format("{} window event: {}.", toString(window.getRole()), toString(event.type)));
            break;
        }
    }

    /// @brief Drains and logs all queued window events.
    /// @param window Window to drain.
    /// @return Number of events drained.
    std::size_t logWindowEvents(Window &window)
    {
        std::size_t eventCount = 0;
        WindowEvent event{};
        while (window.popEvent(event))
        {
            logWindowEvent(window, event);
            ++eventCount;
        }

        return eventCount;
    }

    // Main-loop helpers

    /// @brief Advances all active input states to the next frame.
    /// @param gameInput Gameplay input state.
    /// @param toolInput Optional tool-window input state.
    void advanceInputFrame(InputState &gameInput, InputState *toolInput)
    {
        ZoneScopedNC("Advance input frame", tracy::Color::MediumSeaGreen);

        gameInput.advanceFrame();
        if (toolInput != nullptr)
        {
            toolInput->advanceFrame();
        }
    }

    /// @brief Logs any passive window-message errors recorded during polling.
    /// @param windows Window system to inspect.
    void logWindowAsyncErrors(WindowManager &windows)
    {
        std::vector<Window *> activeWindows;
        windows.getWindows(activeWindows);

        for (Window *window : activeWindows)
        {
            logWindowAsyncError(*window);
        }
    }

    /// @brief Polls the platform window system and logs asynchronous handler errors.
    /// @param windows Window system to poll.
    /// @param gameInput Gameplay input state updated by window messages.
    /// @param toolInput Optional tool-window input state updated by window messages.
    void pollWindowEvents(WindowManager &windows, InputState &gameInput, InputState *toolInput)
    {
        ZoneScopedNC("Poll window events", tracy::Color::DodgerBlue);

        windows.pollEvents(gameInput, toolInput);
        logWindowAsyncErrors(windows);
    }

    /// @brief Handles gameplay input that requests a clean shutdown.
    /// @param mainWindow Main game window.
    /// @param gameInput Gameplay input state to query.
    void handleCloseInput(Window *mainWindow, const InputState &gameInput)
    {
        ZoneScopedNC("Handle close input", tracy::Color::YellowGreen);

        if (mainWindow == nullptr || !gameInput.wasButtonPressed(closeWindowControl))
        {
            return;
        }

        Logger::log(LogLevel::INFO, mainLogSource, "Escape key pressed. Requesting main window close.");
        TracyMessageLC("Escape close requested.", tracy::Color::Orange);
        mainWindow->requestClose();
    }

    /// @brief Logs and drains queued events for every active window.
    /// @param windows Window system to inspect.
    /// @return Total number of events drained.
    std::size_t logWindowEvents(WindowManager &windows)
    {
        ZoneScopedNC("Log window events", tracy::Color::SlateBlue);

        std::size_t eventCount = 0;
        std::vector<Window *> activeWindows;
        windows.getWindows(activeWindows);

        for (Window *window : activeWindows)
        {
            eventCount += logWindowEvents(*window);
        }

        ZoneValue(static_cast<std::uint64_t>(eventCount));
        return eventCount;
    }

    /// @brief Applies temporary frame pacing until a real timing system exists.
    void sleepFrame()
    {
        ZoneScopedNC("Frame sleep", tracy::Color::Gray);
        std::this_thread::sleep_for(frameSleepDuration);
    }

#ifdef TRACY_ENABLE
    /// @brief Emits Tracy frame timing for one completed frame.
    /// @param frameStart Timestamp captured at the start of the frame.
    void plotFrameTime(std::chrono::steady_clock::time_point frameStart)
    {
        const auto frameEnd = std::chrono::steady_clock::now();
        const double frameMilliseconds = std::chrono::duration<double, std::milli>(frameEnd - frameStart).count();
        TracyPlot("Frame time (ms)", frameMilliseconds);
    }
#endif

    /// @brief Runs one main-loop frame.
    /// @param windows Window system that owns native windows.
    /// @param mainWindow Main game window, if one exists.
    /// @param gameInput Gameplay input state.
    /// @param toolInput Optional tool-window input state.
    void runMainFrame(WindowManager &windows, Window *mainWindow, InputState &gameInput, InputState *toolInput)
    {
#ifdef TRACY_ENABLE
        const auto frameStart = std::chrono::steady_clock::now();
#endif
        ZoneScopedNC("Main frame", tracy::Color::RoyalBlue);

        advanceInputFrame(gameInput, toolInput);
        pollWindowEvents(windows, gameInput, toolInput);
        handleCloseInput(mainWindow, gameInput);

        const std::size_t windowEventCount = logWindowEvents(windows);
        TracyPlot("Window events/frame", static_cast<double>(windowEventCount));

        sleepFrame();

#ifdef TRACY_ENABLE
        plotFrameTime(frameStart);
#endif
        FrameMarkNamed("GameWIP frame");
    }

    /// @brief Destroys all active windows during shutdown.
    /// @param windows Window system that owns the windows.
    void destroyAllWindows(WindowManager &windows)
    {
        ZoneScopedNC("Shutdown window", tracy::Color::OrangeRed);
        TracyMessageLC("Window close requested.", tracy::Color::Orange);
        Logger::log(LogLevel::INFO, mainLogSource, "Window close requested.");

        std::vector<Window *> activeWindows;
        windows.getWindows(activeWindows);
        for (Window *window : activeWindows)
        {
            if (WindowResult result = windows.destroyWindow(*window); result != WindowResult::Success)
            {
                logWindowFailure("Destroying platform window", result);
            }
        }
    }

    // Game loop

    /// @brief The main game loop. Initializes subsystems, runs the game, and handles shutdown.
    /// @return Returns 0 on clean shutdown, non-zero on error.
    int runGame()
    {
        ZoneScopedNC("Run game", tracy::Color::SteelBlue);
        TracyMessageLC("GameWIP startup.", tracy::Color::SeaGreen);
        Logger::log(LogLevel::INFO, mainLogSource, "GameWIP starting up.");

        InputState gameInput;
#ifdef GAMEWIP_ENABLE_TOOLS
        InputState toolInput;
        InputState *toolInputState = &toolInput;
#else
        InputState *toolInputState = nullptr;
#endif
        WindowManager windows;
        Window *mainWindow = nullptr;

        WindowResult createResult = createMainWindow(windows, mainWindow);
        if (createResult != WindowResult::Success)
        {
            logWindowFailure("Creating platform window", createResult);
            return 1;
        }
        if (mainWindow == nullptr)
        {
            logWindowFailure("Creating platform window", WindowResult::NotCreated);
            return 1;
        }
        TracyMessageLC("platform window created.", tracy::Color::SeaGreen);

        applyStartupClientSizeConstraints(*mainWindow);

#ifdef GAMEWIP_ENABLE_TOOLS
        createOptionalStartupToolWindows(windows);
#endif

        while (!windows.shouldQuit())
        {
            runMainFrame(windows, mainWindow, gameInput, toolInputState);
        }

        destroyAllWindows(windows);

        TracyMessageLC("GameWIP shutdown clean.", tracy::Color::SeaGreen);
        Logger::log(LogLevel::INFO, mainLogSource, "GameWIP shutting down cleanly.");
        return 0;
    }
}

/// @brief Program entry point.
/// @return Process exit code.
int main()
{
    Logger::init(defaultOutputMode, defaultLogLevel, defaultQueueSize);

    tracy::SetThreadName("Main");
#ifdef TRACY_ENABLE
    configureTracySession();
#endif

    int exitCode = 0;

    try
    {
        exitCode = runGame();
        GAMEWIP_ASSERT_MSG(exitCode == 0, "Game exited with non-zero exit code.");
    }
    catch (const std::exception &error)
    {
        const std::string_view errorMessage = error.what();
        TracyMessageLC("Fatal std::exception caught.", tracy::Color::Red);
        TracyMessageC(errorMessage.data(), errorMessage.size(), tracy::Color::Red);
        Logger::log(LogLevel::FATAL, mainLogSource, errorMessage);
        Logger::logDebugOutput(LogLevel::FATAL, mainLogSource, errorMessage);
        Logger::flush();
        Logger::showFatalPopup(errorMessage);
        exitCode = 1;
    }
    catch (...)
    {
        constexpr std::string_view unknownErrorMessage = "Unhandled non-standard exception.";
        TracyMessageLC("Fatal non-standard exception caught.", tracy::Color::Red);
        Logger::log(LogLevel::FATAL, mainLogSource, unknownErrorMessage);
        Logger::logDebugOutput(LogLevel::FATAL, mainLogSource, unknownErrorMessage);
        Logger::flush();
        Logger::showFatalPopup(unknownErrorMessage);
        exitCode = 1;
    }

    Logger::shutdown();
    return exitCode;
}
