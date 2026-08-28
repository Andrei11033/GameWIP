/// @file window_clipboard.cpp
/// @brief Isolated installed-consumer check for window/clipboard.h.

#include "window/clipboard.h"

void consumeWindowClipboard()
{
    const auto result = GameWIP::Window::Clipboard::hasFormat({GameWIP::Window::Types::DataTransfer::FormatKind::Text, {}});
    static_cast<void>(result);
}
