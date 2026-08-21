/// @file window_events.cpp
/// @brief Verifies the installed focused Window events header in isolation.

#include "window/events.h"

static_assert(noexcept(GameWIP::Window::Events::poll()));
