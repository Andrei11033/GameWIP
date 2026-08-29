/// @file desktop_display.cpp
/// @brief Verifies the installed focused Window display header in isolation.

#include "desktop/display.h"

static_assert(noexcept(GameWIP::Desktop::Display::getModes({})));
