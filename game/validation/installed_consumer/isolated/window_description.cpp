/// @file window_description.cpp
/// @brief Verifies the installed focused Window description header in isolation.

#include "window/description.h"

static_assert(GameWIP::Window::Types::ModeRequest{}.mode == GameWIP::Window::Types::Mode::Windowed);
