/// @file logging.h
/// @brief Registered Logger areas shared by the GameWIP runtime.

#pragma once

#include "logger/config.h"

#include <array>
#include <cstdint>

namespace GameWIP::Game::Logging
{
    /// ------------------------------------------------------------
    /// Logger areas
    /// ------------------------------------------------------------

    /// @brief Runtime subsystems used as registered Logger areas.
    enum class Area : std::uint32_t
    {
        Startup = 1,
        Display,
        Input,
        Window,
        Shutdown,
        Runtime = 6
    };

    /// ------------------------------------------------------------
    /// Registered sources
    /// ------------------------------------------------------------

    /// @brief Source definitions installed before runtime logging begins.
    /// @details RGB values match distinct basic terminal colors when RGB output is unavailable.
    inline constexpr std::array kSources{
        Logger::defineSource(Area::Startup, "Startup", Logger::Color::rgb(0, 0, 255)),
        Logger::defineSource(Area::Display, "Display", Logger::Color::rgb(0, 255, 255)),
        Logger::defineSource(Area::Input, "Input", Logger::Color::rgb(255, 0, 255)),
        Logger::defineSource(Area::Window, "Window", Logger::Color::rgb(255, 255, 0)),
        Logger::defineSource(Area::Shutdown, "Shutdown", Logger::Color::rgb(128, 128, 128)),
        Logger::defineSource(Area::Runtime, "Runtime", Logger::Color::rgb(0, 255, 0))};
} // namespace GameWIP::Game::Logging
