/// @file desktop_accessibility_header.cpp
/// @brief Verifies the portable opt-in accessibility entry header in isolation.
#include "desktop/accessibility.h"
#include <type_traits>
static_assert(!std::is_copy_constructible_v<GameWIP::Desktop::Accessibility::SnapshotBuilder>);
static_assert(std::is_nothrow_move_constructible_v<GameWIP::Desktop::Accessibility::SnapshotBuilder>);
static_assert(std::is_nothrow_copy_constructible_v<GameWIP::Desktop::Accessibility::SnapshotReader>);
static_assert(noexcept(std::declval<GameWIP::Desktop::Window &>().accessibility()));
