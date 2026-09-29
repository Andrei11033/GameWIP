/// @file clipboard_platform.h
/// @brief Internal portable-to-native Clipboard backend contract.

#pragma once

#include "desktop/clipboard.h"

namespace GameWIP::Desktop::Detail::Platform
{
    /// @brief Tests native clipboard availability for one portable format.
    [[nodiscard]] Types::Clipboard::FormatResult clipboardHasFormat(
        Types::DataTransfer::FormatView format,
        std::chrono::milliseconds timeout) noexcept;
    /// @brief Enumerates portable formats currently available on the clipboard.
    [[nodiscard]] Types::Clipboard::FormatsResult clipboardGetFormats(std::chrono::milliseconds timeout) noexcept;
    /// @brief Reads Unicode text from the clipboard.
    [[nodiscard]] Types::Clipboard::TextResult clipboardReadText(std::chrono::milliseconds timeout) noexcept;
    /// @brief Reads an absolute-path list from the clipboard.
    [[nodiscard]] Types::Clipboard::FileListResult clipboardReadFiles(std::chrono::milliseconds timeout) noexcept;
    /// @brief Reads an RGBA image from the clipboard.
    [[nodiscard]] Types::Clipboard::ImageResult clipboardReadImage(std::chrono::milliseconds timeout) noexcept;
    /// @brief Reads one named custom clipboard payload.
    [[nodiscard]] Types::Clipboard::CustomDataResult clipboardReadCustomData(std::string_view formatName, std::chrono::milliseconds timeout) noexcept;
    /// @brief Publishes portable items to the clipboard within the access deadline.
    [[nodiscard]] Types::Clipboard::WriteResult clipboardWrite(
        std::span<const Types::DataTransfer::ItemView> items,
        std::chrono::milliseconds timeout) noexcept;
    /// @brief Clears the clipboard within the access deadline.
    [[nodiscard]] Types::Clipboard::ClearResult clipboardClear(std::chrono::milliseconds timeout) noexcept;
} // namespace GameWIP::Desktop::Detail::Platform
