/// @file desktop_shell_types.cpp
/// @brief Verifies the installed desktop/shell_types.h header in isolation.

#include "desktop/shell_types.h"

#include <type_traits>

static_assert(std::is_default_constructible_v<GameWIP::Desktop::Types::Shell::CommandId>);
static_assert(std::is_default_constructible_v<GameWIP::Desktop::Types::Shell::Event>);
