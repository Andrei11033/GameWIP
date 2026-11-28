/// @file input_win32_header.cpp
/// @brief Input Win32 public-header self-containment compile check.

#include "input/win32.h"

#include <type_traits>

static_assert(std::is_default_constructible_v<GameWIP::Input::Platform::Win32::Attachment>);
