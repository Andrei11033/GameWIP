/// @file desktop_display_info_header.cpp
/// @brief Verifies the source-tree focused Window display-information header.

#include "desktop/display_info.h"

static_assert(noexcept(GameWIP::Desktop::Display::getMonitors()));
static_assert(noexcept(GameWIP::Desktop::Display::getColorInfo(GameWIP::Desktop::Types::Display::MonitorId{})));
