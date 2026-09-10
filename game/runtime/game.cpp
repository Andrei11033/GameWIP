/// @file game.cpp
/// @brief Implementation of the executable-owned runtime facade.
///
/// This file owns executable runtime composition. Keep process startup policy in
/// main.cpp, return expected runtime failures as process exit codes, and place
/// reusable behavior in the owning reusable library.

#include "runtime/game.h"

#include "logger/logger.h"
#include "desktop/display_info.h"
#include "desktop/window.h"

#if GAMEWIP_TRACY_ENABLED
#include <tracy/Tracy.hpp>
#endif

#include <chrono>
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

    void initializeLogger()
    {
#if GAMEWIP_TRACY_ENABLED
        ZoneScopedNC("Init Logger", ProfileZoneColor::Initialization);
#endif
        GameWIP::Logger::initConsole(GameWIP::Logger::Types::Level::Debug);
    }

    [[nodiscard]] GameWIP::Desktop::Types::Display::MonitorsResult inspectDisplays()
    {
        GameWIP::Desktop::Types::Display::MonitorsResult monitors;
        {
#if GAMEWIP_TRACY_ENABLED
            ZoneScopedNC("Enumerate displays, display modes, and HDR state", ProfileZoneColor::Initialization);
#endif
            monitors = GameWIP::Desktop::Display::getMonitors();
            if (monitors.status.ok())
            {
                GameWIP::Logger::info("Startup", "Enumerated {} connected display(s)", monitors.monitors.size());
                for (const GameWIP::Desktop::Types::Display::Info &monitor : monitors.monitors)
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
                        supportedModes.modes.size());
                    if (activeMode.status.ok())
                    {
                        std::format_to(
                            std::back_inserter(displayReport),
                            "\n  active: {}x{} @ {}.{:03} Hz, {} bpp{}",
                            activeMode.mode.resolution.width,
                            activeMode.mode.resolution.height,
                            activeMode.mode.refreshRateMillihertz / 1000,
                            activeMode.mode.refreshRateMillihertz % 1000,
                            activeMode.mode.bitsPerPixel,
                            activeMode.mode.interlaced ? ", interlaced" : "");
                    }
                    if (colorInfo.status.ok())
                    {
                        std::format_to(
                            std::back_inserter(displayReport),
                            "\n  color: {}, HDR supported={}, HDR enabled={}, WCG supported={}, {} bits/channel, "
                            "luminance min/peak/full-frame={:.3f}/{:.1f}/{:.1f} nits, SDR white={:.1f} nits",
                            colorSpaceName(colorInfo.info.activeColorSpace),
                            colorInfo.info.hdrSupported,
                            colorInfo.info.hdrEnabled,
                            colorInfo.info.wideColorGamutSupported,
                            colorInfo.info.bitsPerColorChannel,
                            colorInfo.info.minimumLuminanceNits,
                            colorInfo.info.maximumLuminanceNits,
                            colorInfo.info.maximumFullFrameLuminanceNits,
                            colorInfo.info.sdrWhiteLevelNits);
                    }
                    else
                    {
                        std::format_to(std::back_inserter(displayReport), "\n  HDR/color query failed: {}", colorInfo.status.message);
                    }
                    for (const auto &mode : supportedModes.modes)
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
        description.mode.mode = GameWIP::Desktop::Types::Mode::BorderlessFullscreen;
        description.visible = true;
        description.requestFocus = true;
        return description;
    }

    [[nodiscard]] GameWIP::IO::Types::Status openRuntimeWindow(GameWIP::Desktop::Window &window)
    {
#if GAMEWIP_TRACY_ENABLED
        ZoneScopedNC("Open borderless-fullscreen window", ProfileZoneColor::Initialization);
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
                GameWIP::Logger::error("Window", "Event pump failed: {}", events.status.message);
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
        ZoneScopedNC("Close borderless-fullscreen window", ProfileZoneColor::Shutdown);
#endif
        return window.close();
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
        initializeLogger();
        // Capture display topology and color capabilities before creating the fullscreen window.
        Logger::info("Startup", "Logger initialized");
        const Desktop::Types::Display::MonitorsResult monitors = inspectDisplays();
        if (!monitors.status.ok())
        {
#if GAMEWIP_TRACY_ENABLED
            TracyMessage(monitors.status.message.c_str(), monitors.status.message.size());
#endif
            Logger::error("Window", "Failed to enumerate displays: {}", monitors.status.message);
            Logger::shutdown();
            return EXIT_FAILURE;
        }

        // Request a borderless fullscreen window without changing the desktop display mode.
        Desktop::Window window;
        const IO::Types::Status openStatus = openRuntimeWindow(window);
        if (!openStatus.ok())
        {
#if GAMEWIP_TRACY_ENABLED
            TracyMessage(openStatus.message.c_str(), openStatus.message.size());
#endif
            Logger::error("Window", "Failed to open borderless-fullscreen window: {}", openStatus.message);
            Logger::shutdown();
            return EXIT_FAILURE;
        }
#if GAMEWIP_TRACY_ENABLED
        TracyMessageL("Borderless-fullscreen window opened");
#endif

        Logger::info("Startup", "Borderless-fullscreen window is active; desktop resolution is unchanged; press Alt+F4 to exit");

        // Keep the process responsive while the native event pump owns timing and delivery.
        if (!pumpRuntimeEvents(window))
        {
            Logger::shutdown();
            return EXIT_FAILURE;
        }

        // Close the window before shutting down logging so the final lifecycle result is recorded.
        const IO::Types::Status closeStatus = closeRuntimeWindow(window);
        if (!closeStatus.ok())
        {
#if GAMEWIP_TRACY_ENABLED
            TracyMessage(closeStatus.message.c_str(), closeStatus.message.size());
#endif
            Logger::error("Window", "Failed to close borderless-fullscreen window: {}", closeStatus.message);
            Logger::shutdown();
            return EXIT_FAILURE;
        }
        Logger::info("Window", "Borderless-fullscreen window closed");
#if GAMEWIP_TRACY_ENABLED
        TracyMessageL("Borderless-fullscreen window closed");
#endif

        {
#if GAMEWIP_TRACY_ENABLED
            ZoneScopedNC("Logger shutdown", ProfileZoneColor::Shutdown);
#endif
            Logger::warn("Shutdown", "Logger shutting down");
            Logger::shutdown();
        }

        static_cast<void>(argc);
        static_cast<void>(argv);
        return EXIT_SUCCESS;
    }
} // namespace GameWIP::Game
