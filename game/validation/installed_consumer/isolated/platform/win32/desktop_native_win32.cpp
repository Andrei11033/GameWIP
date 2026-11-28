/// @file desktop_native_win32.cpp
/// @brief Verifies the installed focused Window Win32 adapter header in isolation.

#include "desktop/native/win32.h"

#include <utility>

static_assert(sizeof(GameWIP::Desktop::Native::Win32::HandleView) >= sizeof(HWND) + sizeof(HINSTANCE));
static_assert(noexcept(GameWIP::Desktop::Native::Win32::getHandle(std::declval<const GameWIP::Desktop::ChildSurface &>())));
