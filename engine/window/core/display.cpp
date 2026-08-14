/// @file display.cpp
/// @brief Fundamental display-mode query forwarding for Window.

#include "window/display.h"

#include "window/internal/window_platform.h"

namespace GameWIP::Window::Display
{
    Types::Display::ModesResult getModes(Types::Display::MonitorId monitor) noexcept
    {
        return Detail::Platform::getDisplayModes(monitor);
    }

    Types::Display::ModeResult getCurrentMode(Types::Display::MonitorId monitor) noexcept
    {
        return Detail::Platform::getCurrentDisplayMode(monitor);
    }

    Types::Display::ModeResult getPreferredMode(Types::Display::MonitorId monitor) noexcept
    {
        return Detail::Platform::getPreferredDisplayMode(monitor);
    }
} // namespace GameWIP::Window::Display
