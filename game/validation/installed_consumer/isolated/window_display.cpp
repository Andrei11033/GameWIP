/// @file window_display.cpp
/// @brief Verifies the installed focused Window display header in isolation.

#include "window/display.h"

static_assert(noexcept(GameWIP::Window::Display::getModes({})));
