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
        std::uint64_t value = 0;

        [[nodiscard]] constexpr bool isValid() const noexcept
        {
            return value != 0;
        }

        friend constexpr bool operator==(MonitorId, MonitorId) noexcept = default;
    };

    /// @brief Physical monitor display mode.
    struct Mode
    {
        PixelSize resolution;
        std::uint32_t refreshRateMillihertz = 0;
        std::uint16_t bitsPerPixel = 0;
        bool interlaced = false;
        friend constexpr bool operator==(Mode, Mode) noexcept = default;
    };

    /// @brief Materialized display-mode enumeration result.
    struct ModesResult
    {
        IO::Types::Status status;
        std::vector<Mode> displayModes;
    };

    /// @brief Single display-mode query result.
    struct ModeResult
    {
        IO::Types::Status status;
        Mode displayMode;
    };
} // namespace GameWIP::Window::Types::Display

/// @brief Stateless desktop-display operations.
namespace GameWIP::Window::Display
{
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Display::ModesResult getModes(Types::Display::MonitorId monitor) noexcept;
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Display::ModeResult getCurrentMode(Types::Display::MonitorId monitor) noexcept;
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Display::ModeResult getPreferredMode(Types::Display::MonitorId monitor) noexcept;
} // namespace GameWIP::Window::Display
