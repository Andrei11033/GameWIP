/// @file desktop_window.cpp
/// @brief Verifies the installed focused Window owner header in isolation.

#include "desktop/window.h"

#include <type_traits>

static_assert(!std::is_copy_constructible_v<GameWIP::Desktop::Window>);
