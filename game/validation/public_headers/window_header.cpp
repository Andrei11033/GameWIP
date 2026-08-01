/// @file window_header.cpp
/// @brief Portable Window public-header self-containment compile check.

#include "window/window.h"

static_assert(GameWIP::Window::kDefaultEventQueueCapacity > 0);
