/// @file desktop_window_header.cpp
/// @brief Verifies the source-tree Window aggregate header.

#include "desktop/window.h"

#include <type_traits>

static_assert(!std::is_copy_constructible_v<GameWIP::Desktop::Window>);
static_assert(!std::is_move_constructible_v<GameWIP::Desktop::Window>);
static_assert(noexcept(std::declval<GameWIP::Desktop::Window &>().close()));
