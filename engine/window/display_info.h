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
        MonitorId id;                                ///< Process-local monitor identity.
        std::string name;                            ///< UTF-8 operating-system display name.
        ScreenRect bounds;                           ///< Full virtual-screen bounds.
        ScreenRect workArea;                         ///< Work area excluding reserved desktop regions.
        ContentScale contentScale;                   ///< Effective logical-to-physical scale.
        Dpi effectiveDpi;                            ///< Effective content DPI.
        std::uint32_t physicalWidthMillimeters = 0;  ///< Reported physical panel width.
        std::uint32_t physicalHeightMillimeters = 0; ///< Reported physical panel height.
        bool primary = false;                        ///< Whether this is the primary monitor.
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
        MonitorId monitor;                                 ///< Monitor described by this snapshot.
        ColorSpace activeColorSpace = ColorSpace::Unknown; ///< Current operating-system color space.
        bool wideColorGamutSupported = false;              ///< Whether wide-gamut output is supported.
        bool hdrSupported = false;                         ///< Whether HDR output is supported.
        bool hdrEnabled = false;                           ///< Whether HDR output is currently enabled.
        std::uint16_t bitsPerColorChannel = 0;             ///< Active per-channel bit depth.
        float minimumLuminanceNits = 0.0F;                 ///< Minimum reported luminance.
        float maximumLuminanceNits = 0.0F;                 ///< Maximum reported luminance.
        float maximumFullFrameLuminanceNits = 0.0F;        ///< Maximum full-frame luminance.
        float sdrWhiteLevelNits = 0.0F;                    ///< Operating-system SDR white level.
    };

    /// @brief Materialized monitor enumeration result.
    struct MonitorsResult
    {
        IO::Types::Status status;   ///< Query status.
        std::vector<Info> monitors; ///< Materialized monitor snapshots on success.
    };

    /// @brief Single monitor information query result.
    struct InfoResult
    {
        IO::Types::Status status; ///< Query status.
        Info monitor;             ///< Monitor snapshot on success.
    };

    /// @brief Status and current display-color information.
    struct ColorInfoResult
    {
        IO::Types::Status status; ///< Query status.
        ColorInfo info;           ///< Display-color snapshot on success.
    };
} // namespace GameWIP::Window::Types::Display

namespace GameWIP::Window::Display
{
    /// @brief Enumerates snapshots for all currently known desktop monitors.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Display::MonitorsResult getMonitors() noexcept;
    /// @brief Returns a snapshot for the current primary monitor.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Display::InfoResult getPrimaryMonitor() noexcept;
    /// @brief Returns a snapshot for a currently known monitor identity.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Display::InfoResult getMonitor(Types::Display::MonitorId monitor) noexcept;
    /// @brief Returns current color information for a currently known monitor.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Display::ColorInfoResult getColorInfo(Types::Display::MonitorId monitor) noexcept;
    /// @brief Returns current color information for an open Window's monitor.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Display::ColorInfoResult getColorInfo(const Window &window) noexcept;
} // namespace GameWIP::Window::Display
