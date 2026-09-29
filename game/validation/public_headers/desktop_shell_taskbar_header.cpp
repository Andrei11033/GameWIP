/// @file desktop_shell_taskbar_header.cpp
/// @brief Verifies desktop/shell_taskbar.h in isolation.

#include "desktop/shell_taskbar.h"

#include <type_traits>

static_assert(!std::is_copy_constructible_v<GameWIP::Desktop::TaskbarItem>);
static_assert(!std::is_move_constructible_v<GameWIP::Desktop::TaskbarItem>);
