/// @file display_info.h
/// @brief Rich monitor and operating-system display-color inspection for GameWIP Window.

#pragma once

#include "desktop/display.h"
#include "desktop/result.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace GameWIP::Desktop
{
    class Window;
}

namespace GameWIP::Desktop::Types::Display
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
        Unknown,        ///< The operating system did not provide a recognized classification.
        Srgb,           ///< Standard dynamic range in an sRGB-like space.
        WideColorGamut, ///< Wide-gamut output without active HDR10 PQ classification.
        Hdr10Pq         ///< HDR10-style perceptual-quantizer output is active.
    };

    /// @brief Current native color capabilities and state for one monitor.
    /// @details Optional measurements are disengaged when the operating system does not provide a
    /// finite positive value. `hdrEnabled` implies `hdrSupported`; a non-Unknown active color
    /// space describes the current output classification, not a promise that every measurement is available.
    struct ColorInfo
    {
        MonitorId monitor;                                 ///< Monitor described by this snapshot.
        ColorSpace activeColorSpace = ColorSpace::Unknown; ///< Current operating-system color space.
        bool wideColorGamutSupported = false;              ///< Whether wide-gamut output is supported.
        bool hdrSupported = false;                         ///< Whether HDR output is supported.
        bool hdrEnabled = false;                           ///< Whether HDR output is enabled.
        std::optional<std::uint16_t> bitsPerColorChannel; ///< Active per-channel bit depth when reported.
        std::optional<float> minimumLuminanceNits;         ///< Minimum reported luminance when reported.
        std::optional<float> maximumLuminanceNits;         ///< Maximum reported luminance when reported.
        std::optional<float> maximumFullFrameLuminanceNits; ///< Maximum full-frame luminance when reported.
        std::optional<float> sdrWhiteLevelNits;            ///< Operating-system SDR white level when reported.
    };

} // namespace GameWIP::Desktop::Types::Display

namespace GameWIP::Desktop::Display
{
    /// @name Monitor and color queries
    /// @{

    /// @brief Enumerates snapshots for all currently known desktop monitors.
    /// @return Query status and materialized monitor snapshots on success.
    [[nodiscard]] DESKTOP_EXPORT Types::Result<std::vector<Types::Display::Info>> getMonitors() noexcept;
    /// @brief Returns a snapshot for the current primary monitor.
    /// @return Query status and primary-monitor snapshot on success.
    [[nodiscard]] DESKTOP_EXPORT Types::Result<Types::Display::Info> getPrimaryMonitor() noexcept;
    /// @brief Returns a snapshot for a currently known monitor identity.
    /// @param monitor Monitor identity returned by the current Window runtime.
    /// @return Query status and matching monitor snapshot on success.
    [[nodiscard]] DESKTOP_EXPORT Types::Result<Types::Display::Info> getMonitor(Types::Display::MonitorId monitor) noexcept;
    /// @brief Returns current color information for a currently known monitor.
    /// @param monitor Monitor identity returned by the current Window runtime.
    /// @return Query status and current operating-system color facts on success.
    [[nodiscard]] DESKTOP_EXPORT Types::Result<Types::Display::ColorInfo> getColorInfo(Types::Display::MonitorId monitor) noexcept;
    /// @brief Returns current color information for an open Window's monitor.
    /// @param window Open Window whose cached current monitor is queried.
    /// @return Query status and current operating-system color facts on success.
    [[nodiscard]] DESKTOP_EXPORT Types::Result<Types::Display::ColorInfo> getColorInfo(const Window &window) noexcept;
    /// @}
} // namespace GameWIP::Desktop::Display
