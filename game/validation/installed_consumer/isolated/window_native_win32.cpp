/// @file window_native_win32.cpp
/// @brief Verifies the installed focused Window Win32 adapter header in isolation.

#include "window/native/win32.h"

static_assert(sizeof(GameWIP::Window::Native::Win32::HandleView) >= sizeof(HWND) + sizeof(HINSTANCE));
