/// @file window_cursor.cpp
/// @brief Verifies the installed focused Window custom cursor header in isolation.

#include "window/cursor.h"

#include <span>
#include <type_traits>

static_assert(std::is_default_constructible_v<GameWIP::Window::Cursor>);
static_assert(std::is_copy_constructible_v<GameWIP::Window::Cursor>);
static_assert(noexcept(GameWIP::Window::createCursor(std::span<const GameWIP::Window::Types::Cursor::ImageView>{})));
static_assert(noexcept(GameWIP::Window::createCursor(GameWIP::Window::Types::Cursor::ImageView{})));
