/// @file desktop_shell_jump_lists_header.cpp
/// @brief Verifies desktop/shell_jump_lists.h in isolation.

#include "desktop/shell_jump_lists.h"

static_assert(noexcept(GameWIP::Desktop::JumpLists::publish({})));
