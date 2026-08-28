/// @file clipboard_platform.h
/// @brief Internal portable-to-native Clipboard backend contract.

#pragma once

#include "window/clipboard.h"

namespace GameWIP::Window::Detail::Platform
{
    [[nodiscard]] Types::Clipboard::FormatResult clipboardHasFormat(
        Types::DataTransfer::FormatView format,
        std::chrono::milliseconds timeout) noexcept;
    [[nodiscard]] Types::Clipboard::FormatsResult clipboardGetFormats(std::chrono::milliseconds timeout) noexcept;
    [[nodiscard]] Types::Clipboard::TextResult clipboardReadText(std::chrono::milliseconds timeout) noexcept;
    [[nodiscard]] Types::Clipboard::FileListResult clipboardReadFiles(std::chrono::milliseconds timeout) noexcept;
    [[nodiscard]] Types::Clipboard::ImageResult clipboardReadImage(std::chrono::milliseconds timeout) noexcept;
    [[nodiscard]] Types::Clipboard::CustomDataResult clipboardReadCustomData(std::string_view formatName, std::chrono::milliseconds timeout) noexcept;
    [[nodiscard]] Types::Clipboard::WriteResult clipboardWrite(
        std::span<const Types::DataTransfer::ItemView> items,
        std::chrono::milliseconds timeout) noexcept;
    [[nodiscard]] Types::Clipboard::ClearResult clipboardClear(std::chrono::milliseconds timeout) noexcept;
} // namespace GameWIP::Window::Detail::Platform
