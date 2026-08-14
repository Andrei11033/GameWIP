/// @file window_window.cpp
/// @brief Verifies the installed focused Window owner header in isolation.

#include "window/window.h"

#include <type_traits>

static_assert(!std::is_copy_constructible_v<GameWIP::Window::Window>);
