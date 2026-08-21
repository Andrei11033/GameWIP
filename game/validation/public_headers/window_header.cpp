/// @file window_header.cpp
/// @brief Verifies the source-tree Window aggregate header.

#include "window/window.h"

#include <type_traits>

static_assert(!std::is_copy_constructible_v<GameWIP::Window::Window>);
static_assert(!std::is_move_constructible_v<GameWIP::Window::Window>);
static_assert(noexcept(std::declval<GameWIP::Window::Window &>().close()));
