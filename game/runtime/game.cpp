/// @file game.cpp
/// @brief Runtime sequencing for the executable-owned game session.

#include "runtime/game.h"

#include "runtime/diagnostics/display_report.h"
#include "runtime/diagnostics/runtime_diagnostics.h"
#include "runtime/lifecycle/logger_lifecycle.h"
#include "runtime/logging.h"
#include "runtime/options/runtime_options.h"
#include "runtime/profiling.h"
#include "runtime/window/window_loop.h"

#include "logger/logging.h"
#include "logger/message_format.h"
#include "desktop/window.h"

#include <gamewip/version.h>

#if defined(_WIN32)
#include "input/native/desktop_win32.h"
#endif

#include <chrono>
#include <cstdlib>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    namespace Color = GameWIP::Profiling::Color;
    namespace Logger = GameWIP::Logger;
    namespace GameLogging = GameWIP::Game::Logging;
    namespace Runtime = GameWIP::Game::Runtime;
    namespace Diagnostics = GameWIP::Game::RuntimeDiagnostics;
    using LogArea = GameLogging::Area;

    // ------------------------------------------------------------
    // Session state
    // ------------------------------------------------------------

    /// @brief Window label shared by runtime lifecycle messages.
    inline constexpr std::string_view kWindowName = "Borderless fullscreen window";

    /// @brief Input system and its optional Win32 window attachment for one runtime session.
    struct RuntimeInput
    {
        // Input session
        GameWIP::Input::System system;
        GameWIP::Input::Types::Frame frame;
#if defined(_WIN32)
        // Win32 window attachment
        GameWIP::Input::Platform::Win32::Attachment attachment;
#endif

        /// @brief Reports health and closes the Input resources owned by this session.
        [[nodiscard]] bool finish()
        {
#if defined(_WIN32)
            return Diagnostics::finishInputSystem(system, attachment);
#else
            return Diagnostics::finishInputSystem(system);
#endif
        }
    };

    // ------------------------------------------------------------
    // Window lifecycle
    // ------------------------------------------------------------

    /// @brief Describes the initial fullscreen window and optional display target.
    [[nodiscard]] GameWIP::Desktop::Types::Description runtimeWindowDescription(
        std::optional<GameWIP::Desktop::Types::Display::MonitorId> targetMonitor)
    {
        GameWIP::Desktop::Types::Description description;
        description.title = "GameWIP borderless fullscreen (Alt+F4 to exit)";
        description.initialWindowMode = targetMonitor.has_value() ? GameWIP::Desktop::Types::WindowModes::borderlessFullscreen(*targetMonitor)
                                                                  : GameWIP::Desktop::Types::WindowModes::borderlessFullscreen();
        description.activation.visible = true;
        description.activation.requestFocus = true;
        return description;
    }

    /// @brief Opens the runtime window inside its profiling zone.
    [[nodiscard]] GameWIP::IO::Types::Status openRuntimeWindow(
        GameWIP::Desktop::Window &window,
        std::optional<GameWIP::Desktop::Types::Display::MonitorId> targetMonitor)
    {
        GAMEWIP_PROFILE_ZONE("Open borderless fullscreen window", Color::kInitialization);
        return window.open(runtimeWindowDescription(targetMonitor));
    }

    /// @brief Closes the runtime window inside its profiling zone.
    [[nodiscard]] GameWIP::IO::Types::Status closeRuntimeWindow(GameWIP::Desktop::Window &window)
    {
        GAMEWIP_PROFILE_ZONE("Close borderless fullscreen window", Color::kShutdown);

        const auto status = window.close();
        if(status.ok())
        {
            Logger::info(LogArea::Window, "Window stopped");
            GameWIP::Profiling::message("Borderless fullscreen window closed");
        }
        return status;
    }

    /// @brief Logs a completed runtime session with its duration and stop reason.
    void logRuntimeStopped(std::string_view stopReason, double elapsedSeconds)
    {
        Logger::info(LogArea::Runtime, Logger::record().field("stop_reason", stopReason), "Stopped after {:.1f}s", Logger::number(elapsedSeconds));
        GameWIP::Profiling::message(std::format("Runtime stopped after {:.1f}s", elapsedSeconds));
    }

    // ------------------------------------------------------------
    // Runtime option reporting
    // ------------------------------------------------------------

    [[nodiscard]] std::string inputBackendSelection(const Runtime::Options &options)
    {
        using Backend = GameWIP::Input::Types::BackendKind;
        if(!options.inputBackendsConfigured)
        {
            return {};
        }

        if(options.inputBackendCount == 0)
        {
            return "none";
        }

        std::string selection;
        for(std::size_t index = 0; index < options.inputBackendCount; ++index)
        {
            std::string_view name;
            switch(options.inputBackends[index])
            {
            case Backend::Win32RawInput:
                name = "rawInput";
                break;
            case Backend::Win32Hid:
                name = "hid";
                break;
            case Backend::XInput:
                name = "xinput";
                break;
            case Backend::GameInput:
                name = "gameInput";
                break;
            case Backend::DirectInput:
                name = "directInput";
                break;
            case Backend::Win32WindowMessage:
                name = "windowMessages";
                break;
            case Backend::LinuxEvdev:
            case Backend::LinuxHidraw:
            case Backend::Synthetic:
            case Backend::OpenXr:
                name = "unsupported";
                break;
            }

            if(!selection.empty())
            {
                selection += ',';
            }
            selection.append(name);
        }
        return selection;
    }

    /// @brief Logs the selected runtime settings at the configured detail level.
    void logRuntimeOptions(const Runtime::Options &options)
    {
        const double loopIntervalMilliseconds = std::chrono::duration<double, std::milli>{options.eventLoopInterval}.count();
        const double loopRateHz = 1000.0 / loopIntervalMilliseconds;

        // Trace records selected values; Debug lists only enabled options.
        if(Logger::shouldLog(Logger::Types::Level::Trace, LogArea::Runtime))
        {
            Logger::Record record = Logger::record();
            record.field("event_loop_interval_ms", loopIntervalMilliseconds)
                .field("event_loop_rate_hz", loopRateHz)
                .field("display_selection", options.displayIndex.has_value() ? "requested_index" : "operating_system_default")
                .field("window_mode", "borderless_fullscreen");
            if(options.windowEvents)
            {
                record.field("window_events", true);
            }

            if(options.inputDevices)
            {
                record.field("input_devices_trace", true);
            }

            if(options.inputDebug)
            {
                record.field("input_debug", true);
            }

            if(options.inputBackendsConfigured)
            {
                record.field("input_backends", inputBackendSelection(options));
            }

            if(options.displayIndex.has_value())
            {
                record.field("display_index", *options.displayIndex);
            }
            Logger::trace(LogArea::Runtime, record, "Runtime configuration");
            return;
        }

        if(!Logger::shouldLog(Logger::Types::Level::Debug, LogArea::Runtime))
        {
            return;
        }

        std::string displaySelection = "OS";
        if(options.displayIndex.has_value())
        {
            displaySelection = std::format("index {}", *options.displayIndex);
        }

        // Build the ordinary summary from active diagnostics and explicit selections.
        std::string enabledOptions;
        const auto appendEnabledOption = [&enabledOptions](bool enabled, std::string_view name)
        {
            if(enabled)
            {
                enabledOptions.append("; ");
                enabledOptions.append(name);
                enabledOptions.append(" on");
            }
        };
        appendEnabledOption(options.windowEvents, "window events");
        appendEnabledOption(options.inputDevices, "input device trace");
        appendEnabledOption(options.inputDebug, "input debug");
        if(options.inputBackendsConfigured)
        {
            enabledOptions.append("; input backends ");
            enabledOptions.append(inputBackendSelection(options));
        }

        Logger::debug(
            LogArea::Runtime,
            "Runtime options: event loop {:.1f}Hz; display={}{}",
            Logger::number(loopRateHz),
            Logger::code(displaySelection),
            enabledOptions);
    }

    // ------------------------------------------------------------
    // Session execution
    // ------------------------------------------------------------

    /// @brief Runs the session's display, Input, window, and cleanup sequence.
    [[nodiscard]] int runSession(const Runtime::Options &options)
    {
        // Display inspection must precede window creation so it logs the pre-fullscreen topology.
        const auto monitors = GameWIP::Game::inspectDisplays();
        if(!monitors.ok())
        {
            Diagnostics::reportFailure(LogArea::Display, "Failed to enumerate displays", monitors.status);
            return EXIT_FAILURE;
        }

        std::optional<GameWIP::Desktop::Types::Display::MonitorId> targetMonitor;
        if(options.displayIndex.has_value())
        {
            if(*options.displayIndex < monitors->size())
            {
                targetMonitor = (*monitors)[*options.displayIndex].id;
                Logger::info(
                    LogArea::Display,
                    Logger::record()
                        .field("display_index", *options.displayIndex)
                        .field("monitor_id", targetMonitor->value)
                        .field("display_name", (*monitors)[*options.displayIndex].name),
                    "Using requested display");
            }
            else
            {
                Logger::warn(
                    LogArea::Display,
                    Logger::record()
                        .field("requested_display_index", *options.displayIndex)
                        .field("connected_display_count", monitors->size())
                        .field("fallback", "operating_system_default"),
                    "Requested display index is unavailable; using the operating-system default");
            }
        }

        // Open the Input session.
        RuntimeInput input;
        GameWIP::Input::Types::Configuration inputConfiguration;
        inputConfiguration.captureRawReports = options.inputDebug && Logger::shouldLog(Logger::Types::Level::Trace, LogArea::Input);
#if defined(_WIN32)
        if(options.inputBackendsConfigured)
        {
            std::vector<GameWIP::Input::Types::BackendKind> enabledBackends;
            enabledBackends.reserve(options.inputBackendCount);
            for(std::size_t index = 0; index < options.inputBackendCount; ++index)
            {
                enabledBackends.push_back(options.inputBackends[index]);
            }
            inputConfiguration.enabledBackends = std::move(enabledBackends);
        }
#endif
        const GameWIP::IO::Types::Status inputOpenStatus = input.system.open(inputConfiguration);
        if(!inputOpenStatus.ok())
        {
            Diagnostics::reportFailure(LogArea::Input, "Failed to open input system", inputOpenStatus);
            return EXIT_FAILURE;
        }
        Logger::info(LogArea::Input, "Input started");

        // Create the runtime Window.
        GameWIP::Desktop::Window window;
        const GameWIP::IO::Types::Status openStatus = openRuntimeWindow(window, targetMonitor);
        if(!openStatus.ok())
        {
            Diagnostics::reportFailure(LogArea::Window, std::format("{} failed to open", kWindowName), openStatus);
            static_cast<void>(input.finish());
            return EXIT_FAILURE;
        }

        GameWIP::Profiling::message("Borderless fullscreen window opened");
        Diagnostics::logWindowSnapshot(window);

#if defined(_WIN32)
        // Attach Input to the Window.
        const GameWIP::IO::Types::Status inputAttachStatus = GameWIP::Input::Platform::Win32::attachToWindow(input.system, input.attachment, window);
        if(!inputAttachStatus.ok())
        {
            Diagnostics::reportFailure(LogArea::Input, "Failed to attach Input to Desktop Window", inputAttachStatus);
            static_cast<void>(input.finish());

            const GameWIP::IO::Types::Status cleanupStatus = closeRuntimeWindow(window);
            if(!cleanupStatus.ok())
            {
                Diagnostics::reportFailure(LogArea::Window, "Failed to close window after input attach failure", cleanupStatus);
            }
            return EXIT_FAILURE;
        }

        Logger::info(LogArea::Input, "Input linked to window");
        Diagnostics::logInputBackends(input.attachment);
#endif

        // Capture initial Input state and report connected devices.
        const GameWIP::Input::Types::CaptureResult initialCapture = input.system.capture(input.frame);
        if(!initialCapture.status.ok())
        {
            Diagnostics::reportWarning(LogArea::Input, "Initial Input capture failed", initialCapture.status);
        }
        Diagnostics::logInputDevices(input.system, options.inputDevices);

        Logger::info(LogArea::Runtime, "Runtime started; press Alt+F4 to exit");
        GameWIP::Profiling::message("Runtime started; press Alt+F4 to exit");

        // Run the event loop and report its outcome.
        const auto runStart = std::chrono::steady_clock::now();
        Runtime::EventLoopSummary loopSummary;
        if(!Runtime::pumpWindowEvents(
               window,
               input.system,
               input.frame,
               options.eventLoopInterval,
               options.windowEvents,
               options.inputDevices,
               options.inputDebug,
               loopSummary))
        {
            const double eventLoopSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - runStart).count();
            Runtime::logWindowEventLoopSummary(window, loopSummary, eventLoopSeconds, "pump_failure");
            static_cast<void>(input.finish());

            const GameWIP::IO::Types::Status closeStatus = closeRuntimeWindow(window);
            if(!closeStatus.ok())
            {
                Diagnostics::reportFailure(LogArea::Window, "Failed to close window after event-pump failure", closeStatus);
            }
            const double elapsedSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - runStart).count();
            logRuntimeStopped("event_pump_failure", elapsedSeconds);
            return EXIT_FAILURE;
        }

        const double eventLoopSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - runStart).count();
        Runtime::logWindowEventLoopSummary(window, loopSummary, eventLoopSeconds, "close_request");
        const bool inputClosed = input.finish();

        // Detach and close Input before destroying the Window that forwards its native messages.
        const GameWIP::IO::Types::Status closeStatus = closeRuntimeWindow(window);
        if(!closeStatus.ok())
        {
            Diagnostics::reportFailure(LogArea::Window, std::format("{} failed to close", kWindowName), closeStatus);
        }

        const double elapsedSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - runStart).count();
        logRuntimeStopped("close_request", elapsedSeconds);
        if(!closeStatus.ok())
        {
            return EXIT_FAILURE;
        }
        return inputClosed ? EXIT_SUCCESS : EXIT_FAILURE;
    }

} // namespace

namespace GameWIP::Game
{
    // ------------------------------------------------------------
    // Runtime entry point
    // ------------------------------------------------------------

    int run(int argc, char **argv)
    {
        GAMEWIP_PROFILE_ZONE("Game runtime", Color::kRuntime);

        const Runtime::Options options = Runtime::readOptions(argc, argv);
        const Logger::Types::Init::Result loggerInit = Runtime::initializeLogger(options);
        if(!loggerInit.status.ok() || loggerInit.outcome != Logger::Types::Init::Outcome::Started)
        {
            Runtime::reportLoggerInitializationFailure(loggerInit);
            return loggerInit.outcome == Logger::Types::Init::Outcome::Started ? Runtime::finishRuntime(EXIT_FAILURE) : EXIT_FAILURE;
        }

        Runtime::logLoggerStartup(options);
        Runtime::reportInvalidOptions(options);
        Logger::info(LogArea::Runtime, "GameWIP version {}", GameWIP::Version::display);
        logRuntimeOptions(options);

        return Runtime::finishRuntime(runSession(options));
    }
} // namespace GameWIP::Game
