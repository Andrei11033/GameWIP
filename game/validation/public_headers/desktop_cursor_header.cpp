/// @file desktop_cursor_header.cpp
/// @brief Verifies the source-tree focused Window custom cursor header.

#include "desktop/cursor.h"

#include <span>
#include <type_traits>
#include <utility>

static_assert(std::is_default_constructible_v<GameWIP::Desktop::Cursor>);
static_assert(std::is_copy_constructible_v<GameWIP::Desktop::Cursor>);
static_assert(noexcept(GameWIP::Desktop::createCursor(std::span<const GameWIP::Desktop::Types::Cursor::ImageView>{})));
static_assert(noexcept(GameWIP::Desktop::createCursor(std::declval<const GameWIP::Desktop::Types::Cursor::ImageView &>())));
static_assert(noexcept(GameWIP::Desktop::hasCustomCursor(std::declval<const GameWIP::Desktop::Window &>())));
