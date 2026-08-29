/// @file desktop_types_header.cpp
/// @brief Verifies the source-tree focused Window types header.

#include "desktop/types.h"

static_assert(!GameWIP::Desktop::Types::WindowId{}.isValid());
static_assert(GameWIP::Desktop::Types::WindowId{1}.isValid());
static_assert(GameWIP::Desktop::Types::LogicalSize{1, 2} == GameWIP::Desktop::Types::LogicalSize{1, 2});
