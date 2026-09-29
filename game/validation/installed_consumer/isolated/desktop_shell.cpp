/// @file desktop_shell.cpp
/// @brief Verifies the installed desktop/shell.h header in isolation.

#include "desktop/shell.h"

#include <type_traits>

static_assert(!std::is_copy_constructible_v<GameWIP::Desktop::ShellEventQueue>);
static_assert(!std::is_move_constructible_v<GameWIP::Desktop::ShellEventQueue>);
