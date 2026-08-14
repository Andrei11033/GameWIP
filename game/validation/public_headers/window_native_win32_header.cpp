#include "window/native/win32.h"

static_assert(sizeof(GameWIP::Window::Native::Win32::HandleView) >= sizeof(HWND) + sizeof(HINSTANCE));
