/// @file desktop_events.cpp
/// @brief Verifies the installed focused Window events header in isolation.

#include "desktop/events.h"

static_assert(noexcept(GameWIP::Desktop::Events::poll()));
