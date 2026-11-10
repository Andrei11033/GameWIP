/// @file input_types_header.cpp
/// @brief Input types public-header self-containment compile check.

#include "input/types.h"

static_assert(!GameWIP::Input::Types::DeviceId{}.isValid());
static_assert(!GameWIP::Input::Types::ElementId{}.isValid());
