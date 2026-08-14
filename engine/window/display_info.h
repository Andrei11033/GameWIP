/// @file display_info.h
/// @brief Rich monitor and operating-system display-color inspection for GameWIP Window.

#pragma once

#include "window/display.h"

#include <cstdint>
#include <string>
#include <vector>

namespace GameWIP::Window
{
    class Window;
}

namespace GameWIP::Window::Types::Display
{
    /// @brief Snapshot describing one monitor.
    struct Info
    {
        MonitorId id;
        std::string name;
        ScreenRect bounds;
        ScreenRect workArea;
        ContentScale contentScale;
        Dpi effectiveDpi;
        std::uint32_t physicalWidthMillimeters = 0;
        std::uint32_t physicalHeightMillimeters = 0;
        bool primary = false;
    };

    /// @brief Current operating-system display-color classification.
    enum class ColorSpace
    {
        Unknown,
        Srgb,
        WideColorGamut,
        Hdr10Pq
    };

    /// @brief Current native color capabilities and state for one monitor.
    struct ColorInfo
    {
        MonitorId monitor;
        ColorSpace activeColorSpace = ColorSpace::Unknown;
        bool wideColorGamutSupported = false;
        bool hdrSupported = false;
        bool hdrEnabled = false;
        std::uint16_t bitsPerColorChannel = 0;
        float minimumLuminanceNits = 0.0F;
        float maximumLuminanceNits = 0.0F;
        float maximumFullFrameLuminanceNits = 0.0F;
        float sdrWhiteLevelNits = 0.0F;
    };

    /// @brief Materialized monitor enumeration result.
    struct MonitorsResult
    {
        IO::Types::Status status;
        std::vector<Info> monitors;
    };

    /// @brief Single monitor information query result.
    struct InfoResult
    {
        IO::Types::Status status;
        Info monitor;
    };

    /// @brief Status and current display-color information.
    struct ColorInfoResult
    {
        IO::Types::Status status;
        ColorInfo info;
    };
} // namespace GameWIP::Window::Types::Display

namespace GameWIP::Window::Display
{
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Display::MonitorsResult getMonitors() noexcept;
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Display::InfoResult getPrimaryMonitor() noexcept;
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Display::InfoResult getMonitor(Types::Display::MonitorId monitor) noexcept;
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Display::ColorInfoResult getColorInfo(Types::Display::MonitorId monitor) noexcept;
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Display::ColorInfoResult getColorInfo(const Window &window) noexcept;
} // namespace GameWIP::Window::Display
