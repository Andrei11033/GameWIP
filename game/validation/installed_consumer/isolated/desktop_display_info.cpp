/// @file desktop_display_info.cpp
/// @brief Verifies the installed focused Window display-information header in isolation.

#include "desktop/display_info.h"

static_assert(noexcept(GameWIP::Desktop::Display::getMonitors()));
