/// @file desktop_clipboard_header.cpp
/// @brief Standalone public-header compilation check for desktop/clipboard.h.

#include "desktop/clipboard.h"

static_assert(noexcept(GameWIP::Desktop::Clipboard::clear()));
static_assert(GameWIP::Desktop::Clipboard::kNoWait.count() == 0);
