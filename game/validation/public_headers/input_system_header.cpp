/// @file input_system_header.cpp
/// @brief Input system public-header self-containment compile check.

#include "input/system.h"

#include <type_traits>

static_assert(std::is_default_constructible_v<GameWIP::Input::System>);
