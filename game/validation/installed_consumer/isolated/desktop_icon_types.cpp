/// @file desktop_icon_types.cpp
/// @brief Verifies the installed Desktop icon types header in isolation.

#include "desktop/icon_types.h"

static_assert(GameWIP::Desktop::Types::IconImageView{}.rgba8.empty());
