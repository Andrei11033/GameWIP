/// @file desktop_cursor.cpp
/// @brief Verifies the installed focused Window custom cursor header in isolation.

#include "desktop/cursor.h"

#include <span>
#include <type_traits>

static_assert(std::is_default_constructible_v<GameWIP::Desktop::Cursor>);
static_assert(std::is_copy_constructible_v<GameWIP::Desktop::Cursor>);
static_assert(noexcept(GameWIP::Desktop::createCursor(std::span<const GameWIP::Desktop::Types::Cursor::ImageView>{})));
static_assert(noexcept(GameWIP::Desktop::createCursor(GameWIP::Desktop::Types::Cursor::ImageView{})));
