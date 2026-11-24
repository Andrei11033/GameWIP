/// @file desktop_icon_types_header.cpp
/// @brief Verifies the focused Desktop icon types header in isolation.

#include "desktop/icon_types.h"

static_assert(GameWIP::Desktop::Types::IconImageView{}.size == GameWIP::Desktop::Types::PixelSize{});
