/// @file display.cpp
/// @brief Fundamental display-mode query forwarding for Window.

#include "desktop/display.h"

#include "desktop/internal/window_platform.h"

namespace GameWIP::Desktop::Display
{
    Types::Display::ModesResult getModes(Types::Display::MonitorId monitor) noexcept
    {
        return Detail::Platform::getModes(monitor);
    }

    Types::Display::ModeResult getCurrentMode(Types::Display::MonitorId monitor) noexcept
    {
        return Detail::Platform::getCurrentMode(monitor);
    }

    Types::Display::ModeResult getPreferredMode(Types::Display::MonitorId monitor) noexcept
    {
        return Detail::Platform::getPreferredMode(monitor);
    }
} // namespace GameWIP::Desktop::Display
