/// @file desktop_shell_types_header.cpp
/// @brief Verifies desktop/shell_types.h in isolation.

#include "desktop/shell_types.h"

#include <type_traits>

static_assert(std::is_default_constructible_v<GameWIP::Desktop::Types::Shell::CommandId>);
static_assert(std::is_default_constructible_v<GameWIP::Desktop::Types::Shell::Event>);
