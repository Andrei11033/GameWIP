#include "window/display_info.h"

static_assert(noexcept(GameWIP::Window::Display::getMonitors()));
static_assert(noexcept(GameWIP::Window::Display::getColorInfo(GameWIP::Window::Types::Display::MonitorId{})));
