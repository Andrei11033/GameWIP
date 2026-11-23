/// @file display.cpp
/// @brief Fundamental display-mode query forwarding for Window.

#include "desktop/display.h"

#include "desktop/platform/window.h"

namespace GameWIP::Desktop::Display
{
    /// ------------------------------------------------------------
    /// Display-mode queries
    /// ------------------------------------------------------------

    Types::Result<std::vector<Types::Display::Mode>> getModes(Types::Display::MonitorId monitor) noexcept
    {
        return Platform::getModes(monitor);
    }

    Types::Result<Types::Display::Mode> getCurrentMode(Types::Display::MonitorId monitor) noexcept
    {
        return Platform::getCurrentMode(monitor);
    }

    Types::Result<Types::Display::Mode> getPreferredMode(Types::Display::MonitorId monitor) noexcept
    {
        return Platform::getPreferredMode(monitor);
    }
} // namespace GameWIP::Desktop::Display
