/// @file display.h
/// @brief Fundamental monitor identity and display-mode API for GameWIP Window.

#pragma once

#include "io/io.h"
#include "window/types.h"
#include "window/window_export.h"

#include <cstdint>
#include <vector>

namespace GameWIP::Window::Types::Display
{
    /// @brief Process-local identity of a currently known desktop monitor.
    struct MonitorId
    {
        std::uint64_t value = 0; ///< Opaque identity, or zero when invalid.

        /// @brief Returns whether this identity is nonzero.
        /// @return true for a usable process-local monitor identity.
        [[nodiscard]] constexpr bool isValid() const noexcept
        {
            return value != 0;
        }

        /// @brief Compares opaque identity values.
        friend constexpr bool operator==(MonitorId, MonitorId) noexcept = default;
    };

    /// @brief Physical monitor display mode.
    struct Mode
    {
        PixelSize resolution;                    ///< Physical pixel extent.
        std::uint32_t refreshRateMillihertz = 0; ///< Refresh rate in thousandths of a hertz.
        std::uint16_t bitsPerPixel = 0;          ///< Total color depth.
        bool interlaced = false;                 ///< Whether scanout is interlaced.
        /// @brief Compares every display-mode property.
        friend constexpr bool operator==(Mode, Mode) noexcept = default;
    };

    /// @brief Materialized display-mode enumeration result.
    struct ModesResult
    {
        IO::Types::Status status; ///< Query status.
        std::vector<Mode> modes;  ///< Materialized modes on success.
    };

    /// @brief Single display-mode query result.
    struct ModeResult
    {
        IO::Types::Status status; ///< Query status.
        Mode mode;                ///< Selected mode on success.
    };
} // namespace GameWIP::Window::Types::Display

/// @brief Stateless desktop-display operations.
namespace GameWIP::Window::Display
{
    /// @name Display modes
    /// @{

    /// @brief Enumerates materialized physical modes for a currently known monitor.
    /// @param monitor Monitor identity returned by the current Window runtime.
    /// @return Query status and the monitor's available physical modes on success.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Display::ModesResult getModes(Types::Display::MonitorId monitor) noexcept;
    /// @brief Returns the monitor's active physical mode.
    /// @param monitor Monitor identity returned by the current Window runtime.
    /// @return Query status and active physical mode on success.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Display::ModeResult getCurrentMode(Types::Display::MonitorId monitor) noexcept;
    /// @brief Returns the operating system's preferred physical mode for the monitor.
    /// @param monitor Monitor identity returned by the current Window runtime.
    /// @return Query status and preferred physical mode on success.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Display::ModeResult getPreferredMode(Types::Display::MonitorId monitor) noexcept;
    /// @}
} // namespace GameWIP::Window::Display
