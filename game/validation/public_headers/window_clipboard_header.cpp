/// @file window_clipboard_header.cpp
/// @brief Standalone public-header compilation check for window/clipboard.h.

#include "window/clipboard.h"

static_assert(noexcept(GameWIP::Window::Clipboard::clear()));
static_assert(GameWIP::Window::Clipboard::kNoWait.count() == 0);
