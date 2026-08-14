/// @file window_native_win32_header.cpp
/// @brief Verifies the source-tree focused Window Win32 adapter header.

#include "window/native/win32.h"

static_assert(sizeof(GameWIP::Window::Native::Win32::HandleView) >= sizeof(HWND) + sizeof(HINSTANCE));
