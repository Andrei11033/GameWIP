/// @file window_display_info_header.cpp
/// @brief Verifies the source-tree focused Window display-information header.

#include "window/display_info.h"

static_assert(noexcept(GameWIP::Window::Display::getMonitors()));
static_assert(noexcept(GameWIP::Window::Display::getColorInfo(GameWIP::Window::Types::Display::MonitorId{})));
