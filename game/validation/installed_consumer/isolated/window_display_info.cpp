/// @file window_display_info.cpp
/// @brief Verifies the installed focused Window display-information header in isolation.

#include "window/display_info.h"

static_assert(noexcept(GameWIP::Window::Display::getMonitors()));
