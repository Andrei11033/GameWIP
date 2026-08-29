/// @file desktop_native_win32_header.cpp
/// @brief Verifies the source-tree focused Window Win32 adapter header.

#include "desktop/native/win32.h"

static_assert(sizeof(GameWIP::Desktop::Native::Win32::HandleView) >= sizeof(HWND) + sizeof(HINSTANCE));
