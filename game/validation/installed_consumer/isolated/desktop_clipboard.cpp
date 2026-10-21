/// @file desktop_clipboard.cpp
/// @brief Isolated installed-consumer check for desktop/clipboard.h.

#include "desktop/clipboard.h"

void consumeWindowClipboard()
{
    const auto result = GameWIP::Desktop::Clipboard::hasFormat(GameWIP::Desktop::Types::DataTransfer::FormatView::text());
    static_cast<void>(result);
}
