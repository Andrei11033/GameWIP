/// @file window_description_header.cpp
/// @brief Verifies the source-tree focused Window description header.

#include "window/description.h"

static_assert(GameWIP::Window::Types::Controls{}.closable);
static_assert(GameWIP::Window::Types::ModeRequest{}.mode == GameWIP::Window::Types::Mode::Windowed);
