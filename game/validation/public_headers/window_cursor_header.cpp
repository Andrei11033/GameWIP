/// @file window_cursor_header.cpp
/// @brief Verifies the source-tree focused Window custom cursor header.

#include "window/cursor.h"

#include <span>
#include <type_traits>
#include <utility>

static_assert(std::is_default_constructible_v<GameWIP::Window::Cursor>);
static_assert(std::is_copy_constructible_v<GameWIP::Window::Cursor>);
static_assert(noexcept(GameWIP::Window::createCursor(std::span<const GameWIP::Window::Types::Cursor::ImageView>{})));
static_assert(noexcept(GameWIP::Window::createCursor(std::declval<const GameWIP::Window::Types::Cursor::ImageView &>())));
static_assert(noexcept(GameWIP::Window::hasCustomCursor(std::declval<const GameWIP::Window::Window &>())));
