/// @file desktop_child_surface_header.cpp
/// @brief Verifies that the opt-in ChildSurface public header is self-contained.

#include "desktop/child_surface.h"

#include <type_traits>

static_assert(!std::is_copy_constructible_v<GameWIP::Desktop::ChildSurface>);
static_assert(!std::is_move_constructible_v<GameWIP::Desktop::ChildSurface>);
static_assert(noexcept(std::declval<GameWIP::Desktop::ChildSurface &>().close()));
