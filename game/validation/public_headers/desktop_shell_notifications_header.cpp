/// @file desktop_shell_notifications_header.cpp
/// @brief Verifies desktop/shell_notifications.h in isolation.

#include "desktop/shell_notifications.h"

#include <type_traits>

static_assert(!std::is_copy_constructible_v<GameWIP::Desktop::NotificationCenter>);
static_assert(!std::is_move_constructible_v<GameWIP::Desktop::NotificationCenter>);
