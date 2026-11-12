/// @file logging.h
/// @brief Registered Logger areas shared by the GameWIP runtime.

#pragma once

#include "logger/logger.h"

#include <array>
#include <cstdint>

namespace GameWIP::Game::Logging
{
    /// @brief Runtime subsystems used as registered Logger areas.
    enum class Area : std::uint32_t
    {
        Startup = 1,
        Display,
        Input,
        Window,
        Shutdown,
        Diagnostics
    };

    /// @brief Source definitions installed before runtime logging begins.
    inline constexpr std::array kSources{
        Logger::defineSource(Area::Startup, "Startup", Logger::Color::rgb(120, 175, 255)),
        Logger::defineSource(Area::Display, "Display", Logger::Color::rgb(75, 195, 220)),
        Logger::defineSource(Area::Input, "Input", Logger::Color::rgb(190, 140, 245)),
        Logger::defineSource(Area::Window, "Window", Logger::Color::rgb(255, 180, 95)),
        Logger::defineSource(Area::Shutdown, "Shutdown", Logger::Color::rgb(155, 160, 175)),
        Logger::defineSource(Area::Diagnostics, "Diagnostics", Logger::Color::rgb(255, 115, 105)),
    };
} // namespace GameWIP::Game::Logging
