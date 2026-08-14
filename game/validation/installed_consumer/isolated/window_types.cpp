/// @file window_types.cpp
/// @brief Verifies the installed focused Window types header in isolation.

#include "window/types.h"

static_assert(GameWIP::Window::Types::Mode::Windowed != GameWIP::Window::Types::Mode::ExclusiveFullscreen);
