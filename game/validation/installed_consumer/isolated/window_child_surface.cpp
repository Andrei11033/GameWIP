/// @file window_child_surface.cpp
/// @brief Verifies the installed opt-in ChildSurface header in isolation.

#include "window/child_surface.h"

#include <type_traits>

static_assert(!std::is_copy_constructible_v<GameWIP::Window::ChildSurface>);
static_assert(!std::is_move_constructible_v<GameWIP::Window::ChildSurface>);
