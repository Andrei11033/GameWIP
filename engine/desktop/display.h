/// @file display.h
/// @brief Fundamental monitor identity and display-mode API for GameWIP Window.

#pragma once

#include "io/status.h"
#include "desktop/types.h"
#include "desktop/result.h"
#include "desktop/desktop_export.h"

#include <cstdint>
#include <vector>

namespace GameWIP::Desktop::Types::Display
{
    /// @brief Process-local identity of a currently known desktop monitor.
    struct MonitorId
    {
        std::uint64_t value = 0; ///< Opaque identity, or zero when invalid.

        /// @brief Returns the explicit no-monitor identity.
        [[nodiscard]] static constexpr MonitorId invalid() noexcept
        {
            return {};
        }

        /// @brief Reports whether this identity is usable.
        [[nodiscard]] constexpr bool isValid() const noexcept
        {
            return value != 0;
        }

        friend constexpr bool operator==(MonitorId, MonitorId) noexcept = default;
    };

    /// @brief Physical monitor display mode.
    struct Mode
    {
        PixelSize resolution;                    ///< Physical pixel extent.
        std::uint32_t refreshRateMillihertz = 0; ///< Refresh rate in thousandths of a hertz.
        std::uint16_t bitsPerPixel = 0;          ///< Total color depth.
        bool interlaced = false;                 ///< Whether scanout is interlaced.
        friend constexpr bool operator==(Mode, Mode) noexcept = default;
    };

} // namespace GameWIP::Desktop::Types::Display

/// @brief Stateless desktop-display operations.
namespace GameWIP::Desktop::Display
{
    /// @name Display modes
    /// @{

    /// @brief Enumerates materialized physical modes for a currently known monitor.
    /// @param monitor Monitor identity returned by the current Window runtime.
    /// @return Query status and the monitor's available physical modes on success.
    [[nodiscard]] DESKTOP_EXPORT Types::Result<std::vector<Types::Display::Mode>> getModes(Types::Display::MonitorId monitor) noexcept;
    /// @brief Returns the monitor's active physical mode.
    /// @param monitor Monitor identity returned by the current Window runtime.
    /// @return Query status and active physical mode on success.
    [[nodiscard]] DESKTOP_EXPORT Types::Result<Types::Display::Mode> getCurrentMode(Types::Display::MonitorId monitor) noexcept;
    /// @brief Returns the operating system's preferred physical mode for the monitor.
    /// @param monitor Monitor identity returned by the current Window runtime.
    /// @return Query status and preferred physical mode on success.
    [[nodiscard]] DESKTOP_EXPORT Types::Result<Types::Display::Mode> getPreferredMode(Types::Display::MonitorId monitor) noexcept;
    /// @}
} // namespace GameWIP::Desktop::Display
