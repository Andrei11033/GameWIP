/// @file desktop_shell_header.cpp
/// @brief Verifies desktop/shell.h in isolation.

#include "desktop/shell.h"

#include <type_traits>
#include <utility>

static_assert(!std::is_copy_constructible_v<GameWIP::Desktop::ShellEventQueue>);
static_assert(!std::is_move_constructible_v<GameWIP::Desktop::ShellEventQueue>);
static_assert(noexcept(std::declval<GameWIP::Desktop::ShellEventQueue &>().close()));
static_assert(noexcept(GameWIP::Desktop::Shell::supports(GameWIP::Desktop::Types::Shell::Capability::Taskbar)));
