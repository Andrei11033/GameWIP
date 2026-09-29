/// @file desktop_shell_jump_lists.cpp
/// @brief Verifies the installed desktop/shell_jump_lists.h header in isolation.

#include "desktop/shell_jump_lists.h"

static_assert(noexcept(GameWIP::Desktop::JumpLists::publish({})));
