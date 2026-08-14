/// @file window_display_header.cpp
/// @brief Verifies the source-tree focused Window display header.

#include "window/display.h"

static_assert(!GameWIP::Window::Types::Display::MonitorId{}.isValid());
static_assert(GameWIP::Window::Types::Display::MonitorId{1}.isValid());
static_assert(noexcept(GameWIP::Window::Display::getModes({})));
