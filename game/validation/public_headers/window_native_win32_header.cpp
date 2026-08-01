/// @file window_native_win32_header.cpp
/// @brief Deliberate Win32 Window adapter self-containment compile check.

#include "window/native/win32.h"

#include <type_traits>

static_assert(std::is_default_constructible_v<GameWIP::Window::Native::Win32::HandleView>);
