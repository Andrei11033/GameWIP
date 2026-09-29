/// @file desktop_shell_tray_header.cpp
/// @brief Verifies desktop/shell_tray.h in isolation.

#include "desktop/shell_tray.h"

#include <type_traits>

static_assert(!std::is_copy_constructible_v<GameWIP::Desktop::TrayIcon>);
static_assert(!std::is_move_constructible_v<GameWIP::Desktop::TrayIcon>);
