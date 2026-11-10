/// @file game.cpp
/// @brief Implementation of the executable-owned runtime facade.
///
/// This file composes the executable runtime. Process startup policy lives in
/// main.cpp, expected runtime failures become process exit codes, and reusable
/// behavior stays in the library that owns it.

#include "runtime/game.h"

#include "runtime/display_report.h"
#include "runtime/profiling.h"

#include "logger/logger.h"
#include "desktop/window.h"
#include "input/input.h"

#if defined(_WIN32)
#include "input/native/desktop_win32.h"
#endif

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <string>
#include <string_view>

namespace
{
    namespace Color = GameWIP::Profiling::Color;

    /// @brief Window name shared by the runtime's log messages.
    inline constexpr std::string_view kWindowName = "Borderless fullscreen window";
    /// @brief Longest wait for window events before the loop runs another frame.
    inline constexpr std::chrono::milliseconds kEventWaitInterval{16};

    /// @brief Logs a failed operation with its error code and mirrors it to the profiler timeline.
    void reportFailure(std::string_view category, std::string_view action, const GameWIP::IO::Types::Status &status)
    {
        const std::string message = std::format(
            "{}: {} (code={}, nativeCode={})",
            action,
            status.message.empty() ? "<none>" : status.message,
            GameWIP::IO::errorCodeName(status.code),
            status.nativeCode);
        GameWIP::Profiling::message(message);
        GameWIP::Logger::error(category, "{}", message);
    }

    /// @brief Starts console logging so every later startup failure has a diagnostic sink.
    [[nodiscard]] GameWIP::Logger::Types::Init::Result initializeLogger()
    {
        GAMEWIP_PROFILE_ZONE("Init Logger", Color::kInitialization);
        return GameWIP::Logger::initConsole(GameWIP::Logger::Types::Level::Debug);
    }

    /// @brief Describes the borderless fullscreen runtime window.
    [[nodiscard]] GameWIP::Desktop::Types::Description runtimeWindowDescription()
    {
        GameWIP::Desktop::Types::Description description;
        description.title = "GameWIP borderless fullscreen (Alt+F4 to exit)";
        description.initialWindowMode = GameWIP::Desktop::Types::WindowModes::borderlessFullscreen();
        description.activation.visible = true;
        description.activation.requestFocus = true;
        return description;
    }

    /// @brief Opens the runtime window inside its profiling zone.
    [[nodiscard]] GameWIP::IO::Types::Status openRuntimeWindow(GameWIP::Desktop::Window &window)
    {
        GAMEWIP_PROFILE_ZONE("Open borderless fullscreen window", Color::kInitialization);
        return window.open(runtimeWindowDescription());
    }

    /// @brief Pumps window events until a close request, returning false after an event-pump failure.
    [[nodiscard]] bool pumpRuntimeEvents(GameWIP::Desktop::Window &window)
    {
        while(!window.hasCloseRequest())
        {
            GAMEWIP_PROFILE_ZONE("Game frame", Color::kFrame);
            GameWIP::Desktop::Types::Events::PumpResult events;
            {
                GAMEWIP_PROFILE_ZONE("Wait for and pump window events", Color::kWait);
                const auto waitStart = std::chrono::steady_clock::now();
                events = GameWIP::Desktop::Events::wait(kEventWaitInterval);
                const std::chrono::duration<double, std::milli> waited = std::chrono::steady_clock::now() - waitStart;
                GAMEWIP_PROFILE_PLOT("Event wait (ms)", waited.count());
            }

            if(!events.status.ok())
            {
                reportFailure("Window", "Event pump failed", events.status);
                {
                    GAMEWIP_PROFILE_ZONE("Close window after event-pump failure", Color::kShutdown);
                    const GameWIP::IO::Types::Status closeStatus = window.close();

                    if(!closeStatus.ok())
                    {
                        reportFailure("Window", "Failed to close window after event-pump failure", closeStatus);
                    }
                }

                return false;
            }

            GameWIP::Profiling::frameMark();
        }

        return true;
    }

    /// @brief Closes the runtime window inside its profiling zone.
    [[nodiscard]] GameWIP::IO::Types::Status closeRuntimeWindow(GameWIP::Desktop::Window &window)
    {
        GAMEWIP_PROFILE_ZONE("Close borderless fullscreen window", Color::kShutdown);
        return window.close();
    }

    /// @brief Logs the final result and shuts Logger down, upgrading a clean exit to failure if shutdown fails.
    int finishRuntime(int primaryExitCode) noexcept
    {
        GAMEWIP_PROFILE_ZONE("Logger shutdown", Color::kShutdown);

        // The logger cannot record anything once it is shut down, so the final result is logged first.
        if(primaryExitCode == EXIT_SUCCESS)
        {
            GameWIP::Logger::info("Shutdown", "Exiting with code {}", primaryExitCode);
        }
        else
        {
            GameWIP::Logger::error("Shutdown", "Exiting with failure code {}", primaryExitCode);
        }

        const GameWIP::IO::Types::Status shutdownStatus = GameWIP::Logger::shutdown();

        if(primaryExitCode == EXIT_SUCCESS && !shutdownStatus.ok())
        {
            std::fputs("GameWIP: Logger shutdown failed.\n", stderr);
            return EXIT_FAILURE;
        }

        return primaryExitCode;
    }

    /// @brief Runs everything after logger startup and returns the primary exit code.
    /// @note Locals are destroyed before the caller shuts Logger down, so their lifecycle logging is still recorded.
    [[nodiscard]] int runSession()
    {
        // Display inspection must precede window creation so it logs the pre-fullscreen topology.
        const auto monitors = GameWIP::Game::inspectDisplays();

        if(!monitors.ok())
        {
            reportFailure("Window", "Failed to enumerate displays", monitors.status);
            return EXIT_FAILURE;
        }

        GameWIP::Input::System input;
#if defined(_WIN32)
        GameWIP::Input::Platform::Win32::Attachment inputAttachment;
#endif
        const GameWIP::IO::Types::Status inputOpenStatus = input.open();

        if(!inputOpenStatus.ok())
        {
            reportFailure("Input", "Failed to open input system", inputOpenStatus);
            return EXIT_FAILURE;
        }

        GameWIP::Logger::info("Input", "Input system opened");

        GameWIP::Desktop::Window window;
        const GameWIP::IO::Types::Status openStatus = openRuntimeWindow(window);

        if(!openStatus.ok())
        {
            reportFailure("Window", std::format("{} failed to open", kWindowName), openStatus);
            return EXIT_FAILURE;
        }

        GameWIP::Profiling::message("Borderless fullscreen window opened");

#if defined(_WIN32)
        const GameWIP::IO::Types::Status inputAttachStatus = GameWIP::Input::Platform::Win32::attachToWindow(input, inputAttachment, window);

        if(!inputAttachStatus.ok())
        {
            reportFailure("Input", "Failed to attach Input to Desktop Window", inputAttachStatus);

            // Close explicitly so the window lifecycle is logged before the logger shuts down.
            const GameWIP::IO::Types::Status cleanupStatus = closeRuntimeWindow(window);

            if(!cleanupStatus.ok())
            {
                reportFailure("Window", "Failed to close window after input attach failure", cleanupStatus);
            }
            return EXIT_FAILURE;
        }

        GameWIP::Logger::info("Input", "Input attached to window");
#endif

        GameWIP::Logger::info("Startup", "{} is active; press Alt+F4 to exit", kWindowName);

        const auto runStart = std::chrono::steady_clock::now();

        if(!pumpRuntimeEvents(window))
        {
            return EXIT_FAILURE;
        }

        GameWIP::Logger::info(
            "Window",
            "Close requested after {:.1f}s; shutting down",
            std::chrono::duration<double>(std::chrono::steady_clock::now() - runStart).count());

        // Close before logger shutdown so the final lifecycle result is recorded.
        const GameWIP::IO::Types::Status closeStatus = closeRuntimeWindow(window);

        if(!closeStatus.ok())
        {
            reportFailure("Window", std::format("{} failed to close", kWindowName), closeStatus);
            return EXIT_FAILURE;
        }

        GameWIP::Logger::info("Window", "{} closed", kWindowName);
        GameWIP::Profiling::message("Borderless fullscreen window closed");

        return EXIT_SUCCESS;
    }
} // namespace

namespace GameWIP::Game
{
    int run(int argc, char **argv)
    {
        GAMEWIP_PROFILE_ZONE("Game runtime", Color::kRuntime);
        static_cast<void>(argc);
        static_cast<void>(argv);

        // Initialize logging first so every later startup failure has a diagnostic sink.
        const Logger::Types::Init::Result loggerInit = initializeLogger();

        if(!loggerInit.status.ok() || loggerInit.outcome != Logger::Types::Init::Outcome::Started)
        {
            std::fputs("GameWIP: Logger initialization failed.\n", stderr);
            return EXIT_FAILURE;
        }

        Logger::info("Startup", "Logger initialized");
        return finishRuntime(runSession());
    }
} // namespace GameWIP::Game
