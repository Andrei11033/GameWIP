/// @file window_child_surface_header.cpp
/// @brief Verifies that the opt-in ChildSurface public header is self-contained.

#include "window/child_surface.h"

#include <type_traits>

static_assert(!std::is_copy_constructible_v<GameWIP::Window::ChildSurface>);
static_assert(!std::is_move_constructible_v<GameWIP::Window::ChildSurface>);
static_assert(noexcept(std::declval<GameWIP::Window::ChildSurface &>().close()));
