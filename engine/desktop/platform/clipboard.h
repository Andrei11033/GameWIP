/// @file clipboard.h
/// @brief Portable backend contract for Clipboard.

#pragma once

#include "desktop/clipboard.h"

#include <chrono>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace GameWIP::Desktop::Platform
{
    /// ------------------------------------------------------------
    /// Read formats and values
    /// ------------------------------------------------------------

    /// @brief Tests native clipboard availability for one portable format.
    [[nodiscard]] Types::Result<bool> clipboardHasFormat(Types::DataTransfer::FormatView format, std::chrono::milliseconds timeout) noexcept;
    /// @brief Enumerates portable formats currently available on the clipboard.
    [[nodiscard]] Types::Result<std::vector<Types::DataTransfer::Format>> clipboardGetFormats(std::chrono::milliseconds timeout) noexcept;

    /// @brief Reads Unicode text from the clipboard.
    [[nodiscard]] Types::Result<std::string> clipboardReadText(std::chrono::milliseconds timeout) noexcept;
    /// @brief Reads an absolute-path list from the clipboard.
    [[nodiscard]] Types::Result<std::vector<FileSystem::Types::Path>> clipboardReadFiles(std::chrono::milliseconds timeout) noexcept;
    /// @brief Reads an RGBA image from the clipboard.
    [[nodiscard]] Types::Result<Types::DataTransfer::Image> clipboardReadImage(std::chrono::milliseconds timeout) noexcept;
    /// @brief Reads one named custom clipboard payload.
    [[nodiscard]] Types::Result<std::vector<std::byte>> clipboardReadCustomData(
        std::string_view formatName,
        std::chrono::milliseconds timeout) noexcept;

    /// ------------------------------------------------------------
    /// Write and clear
    /// ------------------------------------------------------------

    /// @brief Publishes portable items to the clipboard within the access deadline.
    [[nodiscard]] Types::Clipboard::WriteResult clipboardWrite(
        std::span<const Types::DataTransfer::ItemView> items,
        std::chrono::milliseconds timeout) noexcept;
    /// @brief Clears the clipboard within the access deadline.
    [[nodiscard]] Types::Clipboard::ClearResult clipboardClear(std::chrono::milliseconds timeout) noexcept;
} // namespace GameWIP::Desktop::Platform
