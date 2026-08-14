/// @file game.cpp
/// @brief Implementation of the executable-owned runtime facade.
///
/// This file owns executable runtime composition. Keep process startup policy in
/// main.cpp, return expected runtime failures as process exit codes, and place
/// reusable behavior in the owning reusable library.

#include "runtime/game.h"

#include "logger/logger.h"
#include "window/renderer_bridge.h"
#include "window/window.h"

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

    [[nodiscard]] constexpr std::string_view colorSpaceName(GameWIP::Window::Types::DisplayColorSpace colorSpace) noexcept
    {
        using GameWIP::Window::Types::DisplayColorSpace;
        switch (colorSpace)
        {
        case DisplayColorSpace::Srgb:
            return "sRGB/SDR";
        case DisplayColorSpace::WideColorGamut:
            return "wide-color SDR";
        case DisplayColorSpace::Hdr10Pq:
            return "HDR10/PQ";
        case DisplayColorSpace::Unknown:
        default:
            return "unknown";
        }
    }
} // namespace

namespace GameWIP::Game
{
    int run(int argc, char **argv)
    {
#if GAMEWIP_TRACY_ENABLED
        ZoneScopedNC("Game runtime", ProfileZoneColor::Runtime);
#endif
        {
#if GAMEWIP_TRACY_ENABLED
            ZoneScopedNC("Init Logger", ProfileZoneColor::Initialization);
#endif
            Logger::initConsole(Logger::Types::Level::Debug);
        }

        Logger::info("Startup", "Logger initialized");
        Window::Types::MonitorListResult monitors;
        {
#if GAMEWIP_TRACY_ENABLED
            ZoneScopedNC("Enumerate displays, display modes, and HDR state", ProfileZoneColor::Initialization);
#endif
            monitors = Window::getMonitors();
            if (monitors.status.ok())
            {
                Logger::info("Startup", "Enumerated {} connected display(s)", monitors.monitors.size());
                for (const Window::Types::MonitorInfo &monitor : monitors.monitors)
                {
                    const Window::Types::DisplayModeResult activeMode = Window::getCurrentDisplayMode(monitor.id);
                    const Window::Types::DisplayModeListResult supportedModes = Window::getDisplayModes(monitor.id);
                    const Window::Types::DisplayColorInfoResult colorInfo = Window::Renderer::getDisplayColorInfo(monitor.id);

                    std::string displayReport;
                    std::format_to(
                        std::back_inserter(displayReport),
                        "Display '{}'{} at ({}, {}) has {} supported mode(s)",
                        monitor.name,
                        monitor.primary ? " [primary]" : "",
                        monitor.bounds.position.x,
                        monitor.bounds.position.y,
                        supportedModes.displayModes.size());
                    if (activeMode.status.ok())
                    {
                        std::format_to(
                            std::back_inserter(displayReport),
                            "\n  active: {}x{} @ {}.{:03} Hz, {} bpp{}",
                            activeMode.displayMode.resolution.width,
                            activeMode.displayMode.resolution.height,
                            activeMode.displayMode.refreshRateMillihertz / 1000,
                            activeMode.displayMode.refreshRateMillihertz % 1000,
                            activeMode.displayMode.bitsPerPixel,
                            activeMode.displayMode.interlaced ? ", interlaced" : "");
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
                    for (const Window::Types::DisplayMode &mode : supportedModes.displayModes)
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
                    Logger::info("Startup", "{}", displayReport);
                }
            }
        }
        if (!monitors.status.ok())
        {
#if GAMEWIP_TRACY_ENABLED
            TracyMessage(monitors.status.message.c_str(), monitors.status.message.size());
#endif
            Logger::error("Window", "Failed to enumerate displays: {}", monitors.status.message);
            Logger::shutdown();
            return EXIT_FAILURE;
        }

        Window::Types::Description windowDescription;
        windowDescription.title = "GameWIP borderless fullscreen (Alt+F4 to exit)";
        windowDescription.mode.mode = Window::Types::WindowMode::BorderlessFullscreen;
        windowDescription.visible = true;
        windowDescription.requestFocus = true;

        Window::Window window;
        IO::Types::Status openStatus;
        {
#if GAMEWIP_TRACY_ENABLED
            ZoneScopedNC("Open borderless-fullscreen window", ProfileZoneColor::Initialization);
#endif
            openStatus = window.open(windowDescription);
        }
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
        while (!window.closeRequested())
        {
#if GAMEWIP_TRACY_ENABLED
            ZoneScopedNC("Game frame", ProfileZoneColor::Frame);
#endif
            Window::Types::EventPumpResult events;
            {
#if GAMEWIP_TRACY_ENABLED
                ZoneScopedNC("Wait for and pump window events", ProfileZoneColor::Wait);
#endif
                events = Window::waitEvents(std::chrono::milliseconds(16));
            }
            if (!events.status.ok())
            {
#if GAMEWIP_TRACY_ENABLED
                TracyMessage(events.status.message.c_str(), events.status.message.size());
#endif
                Logger::error("Window", "Event pump failed: {}", events.status.message);
                {
#if GAMEWIP_TRACY_ENABLED
                    ZoneScopedNC("Close window after event-pump failure", ProfileZoneColor::Shutdown);
#endif
                    static_cast<void>(window.close());
                }
                Logger::shutdown();
                return EXIT_FAILURE;
            }
#if GAMEWIP_TRACY_ENABLED
            FrameMark;
#endif
        }

        IO::Types::Status closeStatus;
        {
#if GAMEWIP_TRACY_ENABLED
            ZoneScopedNC("Close borderless-fullscreen window", ProfileZoneColor::Shutdown);
#endif
            closeStatus = window.close();
        }
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
