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
        Shutdown
    };

    /// @brief Source definitions installed before runtime logging begins.
    /// @details RGB values match distinct basic terminal colors when RGB output is unavailable.
    inline constexpr std::array kSources{
        Logger::defineSource(Area::Startup, "Startup", Logger::Color::rgb(0, 0, 255)),
        Logger::defineSource(Area::Display, "Display", Logger::Color::rgb(0, 255, 255)),
        Logger::defineSource(Area::Input, "Input", Logger::Color::rgb(255, 0, 255)),
        Logger::defineSource(Area::Window, "Window", Logger::Color::rgb(255, 255, 0)),
        Logger::defineSource(Area::Shutdown, "Shutdown", Logger::Color::rgb(128, 128, 128)),
    };
} // namespace GameWIP::Game::Logging
