/// @file input_synthetic_header.cpp
/// @brief Input synthetic-source public-header self-containment compile check.

#include "input/synthetic.h"

#include <type_traits>

static_assert(std::is_default_constructible_v<GameWIP::Input::Synthetic::Source>);
