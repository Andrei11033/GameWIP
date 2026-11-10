/// @file input_header.cpp
/// @brief Input umbrella-header self-containment compile check.

#include "input/input.h"

static_assert(GameWIP::Input::Types::DeviceId{}.isValid() == false);
