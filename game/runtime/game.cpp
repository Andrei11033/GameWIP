/// @file game.cpp
/// @brief Implementation of the executable-owned runtime facade.
///
/// This file composes the executable runtime. Process startup policy lives in
/// main.cpp, expected runtime failures become process exit codes, and reusable
/// behavior stays in the library that owns it.

#include "runtime/game.h"

#include "logger/logger.h"
#include "desktop/display_info.h"
#include "desktop/window.h"

#if GAMEWIP_TRACY_ENABLED
#include <tracy/Tracy.hpp>
#endif

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <iterator>
#include <string>
#include <string_view>

namespace
{
#if GAMEWIP_TRACY_ENABLED
    namespace ProfileZoneColor
    {
        inline constexpr auto Runtime = 0x4C78A8;
        inline constexpr auto Initialization = 0x72B7B2;
        inline constexpr auto Frame = 0x54A24B;
        inline constexpr auto Wait = 0x9D9DA1;
        inline constexpr auto Shutdown = 0xE45756;
    } // namespace ProfileZoneColor
#endif

    [[nodiscard]] constexpr std::string_view colorSpaceName(GameWIP::Desktop::Types::Display::ColorSpace colorSpace) noexcept
    {
        using GameWIP::Desktop::Types::Display::ColorSpace;
        switch (colorSpace)
        {
        case ColorSpace::Srgb:
            return "sRGB/SDR";
        case ColorSpace::WideColorGamut:
            return "wide-color SDR";
        case ColorSpace::Hdr10Pq:
            return "HDR10/PQ";
        case ColorSpace::Unknown:
        default:
            return "unknown";
        }
    }

    [[nodiscard]] GameWIP::Logger::Types::Init::Result initializeLogger()
    {
#if GAMEWIP_TRACY_ENABLED
        ZoneScopedNC("Init Logger", ProfileZoneColor::Initialization);
#endif
        return GameWIP::Logger::initConsole(GameWIP::Logger::Types::Level::Debug);
    }

    [[nodiscard]] GameWIP::Desktop::Types::Result<std::vector<GameWIP::Desktop::Types::Display::Info>> inspectDisplays()
    {
        GameWIP::Desktop::Types::Result<std::vector<GameWIP::Desktop::Types::Display::Info>> monitors;
        {
#if GAMEWIP_TRACY_ENABLED
            ZoneScopedNC("Enumerate displays, display modes, and HDR state", ProfileZoneColor::Initialization);
#endif
            monitors = GameWIP::Desktop::Display::getMonitors();
            if (monitors.ok())
            {
                GameWIP::Logger::info("Startup", "Enumerated {} connected display(s)", monitors->size());
                for (const GameWIP::Desktop::Types::Display::Info &monitor : *monitors)
                {
                    const auto activeMode = GameWIP::Desktop::Display::getCurrentMode(monitor.id);
                    const auto supportedModes = GameWIP::Desktop::Display::getModes(monitor.id);
                    const auto colorInfo = GameWIP::Desktop::Display::getColorInfo(monitor.id);

                    std::string displayReport;
                    std::format_to(
                        std::back_inserter(displayReport),
                        "Display '{}'{} at ({}, {}) has {} supported mode(s)",
                        monitor.name,
                        monitor.primary ? " [primary]" : "",
                        monitor.bounds.position.x,
                        monitor.bounds.position.y,
                        supportedModes.ok() ? supportedModes->size() : std::size_t{0});

                    if (activeMode.ok())
                    {
                        std::format_to(
                            std::back_inserter(displayReport),
                            "\n  active: {}x{} @ {}.{:03} Hz, {} bpp{}",
                            activeMode->resolution.width,
                            activeMode->resolution.height,
                            activeMode->refreshRateMillihertz / 1000,
                            activeMode->refreshRateMillihertz % 1000,
                            activeMode->bitsPerPixel,
                            activeMode->interlaced ? ", interlaced" : "");
                    }

                    if (colorInfo.ok())
                    {
                        std::format_to(
                            std::back_inserter(displayReport),
                            "\n  color: {}, HDR supported={}, HDR enabled={}, WCG supported={}, {} bits/channel, "
                            "luminance min/peak/full-frame={:.3f}/{:.1f}/{:.1f} nits, SDR white={:.1f} nits",
                            colorSpaceName(colorInfo->activeColorSpace),
                            colorInfo->hdrSupported,
                            colorInfo->hdrEnabled,
                            colorInfo->wideColorGamutSupported,
                            colorInfo->bitsPerColorChannel.value_or(0),
                            colorInfo->minimumLuminanceNits.value_or(0.0F),
                            colorInfo->maximumLuminanceNits.value_or(0.0F),
                            colorInfo->maximumFullFrameLuminanceNits.value_or(0.0F),
                            colorInfo->sdrWhiteLevelNits.value_or(0.0F));
                    }
                    else
                    {
                        std::format_to(std::back_inserter(displayReport), "\n  HDR/color query failed: {}", colorInfo.status.message);
                    }

                    for (const auto &mode : *supportedModes)
                    {
                        std::format_to(
                            std::back_inserter(displayReport),
                            "\n  mode: {}x{} @ {}.{:03} Hz, {} bpp{}",
                            mode.resolution.width,
                            mode.resolution.height,
                            mode.refreshRateMillihertz / 1000,
                            mode.refreshRateMillihertz % 1000,
                            mode.bitsPerPixel,
                            mode.interlaced ? ", interlaced" : "");
                    }

                    GameWIP::Logger::info("Startup", "{}", displayReport);
                }
            }
        }
        return monitors;
    }

    [[nodiscard]] GameWIP::Desktop::Types::Description runtimeWindowDescription()
    {
        GameWIP::Desktop::Types::Description description;
        description.title = "GameWIP borderless fullscreen (Alt+F4 to exit)";
        description.initialWindowMode = GameWIP::Desktop::Types::WindowModes::borderlessFullscreen();
        description.activation.visible = true;
        description.activation.requestFocus = true;
        return description;
    }

    [[nodiscard]] GameWIP::IO::Types::Status openRuntimeWindow(GameWIP::Desktop::Window &window)
    {
#if GAMEWIP_TRACY_ENABLED
        ZoneScopedNC("Open borderless fullscreen window", ProfileZoneColor::Initialization);
#endif
        return window.open(runtimeWindowDescription());
    }

    [[nodiscard]] bool pumpRuntimeEvents(GameWIP::Desktop::Window &window)
    {
        while (!window.hasCloseRequest())
        {
#if GAMEWIP_TRACY_ENABLED
            ZoneScopedNC("Game frame", ProfileZoneColor::Frame);
#endif
            GameWIP::Desktop::Types::Events::PumpResult events;
            {
#if GAMEWIP_TRACY_ENABLED
                ZoneScopedNC("Wait for and pump window events", ProfileZoneColor::Wait);
#endif
                events = GameWIP::Desktop::Events::wait(std::chrono::milliseconds(16));
            }
            if (!events.status.ok())
            {
#if GAMEWIP_TRACY_ENABLED
                TracyMessage(events.status.message.c_str(), events.status.message.size());
#endif
                GameWIP::Logger::error(
                    "Window",
                    "Event pump failed: code={}({}), nativeCode={}, message={}",
                    static_cast<int>(events.status.code),
                    GameWIP::IO::errorCodeName(events.status.code),
                    events.status.nativeCode,
                    events.status.message.empty() ? "<none>" : events.status.message);
                {
#if GAMEWIP_TRACY_ENABLED
                    ZoneScopedNC("Close window after event-pump failure", ProfileZoneColor::Shutdown);
#endif
                    static_cast<void>(window.close());
                }
                return false;
            }
#if GAMEWIP_TRACY_ENABLED
            FrameMark;
#endif
        }
        return true;
    }

    [[nodiscard]] GameWIP::IO::Types::Status closeRuntimeWindow(GameWIP::Desktop::Window &window)
    {
#if GAMEWIP_TRACY_ENABLED
        ZoneScopedNC("Close borderless fullscreen window", ProfileZoneColor::Shutdown);
#endif
        return window.close();
    }

    int finishRuntime(int primaryExitCode) noexcept
    {
#if GAMEWIP_TRACY_ENABLED
        ZoneScopedNC("Logger shutdown", ProfileZoneColor::Shutdown);
#endif

        const GameWIP::IO::Types::Status shutdownStatus = GameWIP::Logger::shutdown();
        if (primaryExitCode == EXIT_SUCCESS && !shutdownStatus.ok())
        {
            std::fputs("GameWIP: Logger shutdown failed.\n", stderr);
            return EXIT_FAILURE;
        }
        return primaryExitCode;
    }
} // namespace

namespace GameWIP::Game
{
    int run(int argc, char **argv)
    {
#if GAMEWIP_TRACY_ENABLED
        ZoneScopedNC("Game runtime", ProfileZoneColor::Runtime);
#endif
        // Initialize logging first so every later startup failure has a diagnostic sink.
        const Logger::Types::Init::Result loggerInit = initializeLogger();
        if (!loggerInit.status.ok() || loggerInit.outcome != Logger::Types::Init::Outcome::Started)
        {
            std::fputs("GameWIP: Logger initialization failed.\n", stderr);
            return EXIT_FAILURE;
        }
        Logger::info("Startup", "Logger initialized");

        // Display inspection must precede window creation so it logs the pre-fullscreen topology.
        const auto monitors = inspectDisplays();
        if (!monitors.ok())
        {
#if GAMEWIP_TRACY_ENABLED
            TracyMessage(monitors.status.message.c_str(), monitors.status.message.size());
#endif
            Logger::error("Window", "Failed to enumerate displays: {}", monitors.status.message);
            return finishRuntime(EXIT_FAILURE);
        }

        Desktop::Window window;
        const IO::Types::Status openStatus = openRuntimeWindow(window);
        if (!openStatus.ok())
        {
#if GAMEWIP_TRACY_ENABLED
            TracyMessage(openStatus.message.c_str(), openStatus.message.size());
#endif
            Logger::error("Window", "Failed to open borderless fullscreen window: {}", openStatus.message);
            return finishRuntime(EXIT_FAILURE);
        }
#if GAMEWIP_TRACY_ENABLED
        TracyMessageL("Borderless fullscreen window opened");
#endif

        Logger::info("Startup", "Borderless fullscreen window is active; press Alt+F4 to exit");

        if (!pumpRuntimeEvents(window))
        {
            return finishRuntime(EXIT_FAILURE);
        }

        // Close before logger shutdown so the final lifecycle result is recorded.
        const IO::Types::Status closeStatus = closeRuntimeWindow(window);
        if (!closeStatus.ok())
        {
#if GAMEWIP_TRACY_ENABLED
            TracyMessage(closeStatus.message.c_str(), closeStatus.message.size());
#endif
            Logger::error("Window", "Failed to close borderless fullscreen window: {}", closeStatus.message);
            return finishRuntime(EXIT_FAILURE);
        }
        Logger::info("Window", "Borderless fullscreen window closed");
#if GAMEWIP_TRACY_ENABLED
        TracyMessageL("Borderless fullscreen window closed");
#endif

        static_cast<void>(argc);
        static_cast<void>(argv);
        Logger::warn("Shutdown", "Logger shutting down");
        return finishRuntime(EXIT_SUCCESS);
    }
} // namespace GameWIP::Game
