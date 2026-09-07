/// @file desktop_drag_drop_header.cpp
/// @brief Standalone public-header compilation check for desktop/drag_drop.h.

#include "desktop/drag_drop.h"

static_assert(!GameWIP::Desktop::Types::DragDrop::RegionId{}.isValid());
