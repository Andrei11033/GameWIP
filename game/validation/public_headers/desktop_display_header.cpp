/// @file desktop_display_header.cpp
/// @brief Verifies the source-tree focused Window display header.

#include "desktop/display.h"

static_assert(!GameWIP::Desktop::Types::Display::MonitorId{}.isValid());
static_assert(GameWIP::Desktop::Types::Display::MonitorId{1}.isValid());
static_assert(noexcept(GameWIP::Desktop::Display::getModes({})));
