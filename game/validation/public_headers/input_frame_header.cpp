/// @file input_frame_header.cpp
/// @brief Input frame public-header self-containment compile check.

#include "input/frame.h"

#include <type_traits>

static_assert(std::is_default_constructible_v<GameWIP::Input::Types::Frame>);
