/// @file desktop_shell_notifications.cpp
/// @brief Verifies the installed desktop/shell_notifications.h header in isolation.

#include "desktop/shell_notifications.h"

#include <type_traits>

static_assert(!std::is_copy_constructible_v<GameWIP::Desktop::NotificationCenter>);
static_assert(!std::is_move_constructible_v<GameWIP::Desktop::NotificationCenter>);
