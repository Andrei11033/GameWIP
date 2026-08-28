/// @file window_native_win32.cpp
/// @brief Verifies the installed focused Window Win32 adapter header in isolation.

#include "window/native/win32.h"

#include <utility>

static_assert(sizeof(GameWIP::Window::Native::Win32::HandleView) >= sizeof(HWND) + sizeof(HINSTANCE));
static_assert(noexcept(GameWIP::Window::Native::Win32::getHandle(std::declval<const GameWIP::Window::ChildSurface &>())));
