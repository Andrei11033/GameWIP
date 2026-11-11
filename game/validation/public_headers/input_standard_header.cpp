/// @file input_standard_header.cpp
/// @brief Input standard-controls public-header self-containment compile check.

#include "input/standard.h"

static_assert(GameWIP::Input::Standard::Key::A.isValid());
