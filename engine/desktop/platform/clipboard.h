/// @file clipboard.h
/// @brief Portable backend contract for Clipboard.

#pragma once

#include "desktop/clipboard.h"
#include "status/result.h"

#include <chrono>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace GameWIP::Desktop::Platform
{
    using ClipboardWriteResult = GameWIP::Status::Types::Result<Types::Clipboard::WriteProgress>;
    using ClipboardClearResult = GameWIP::Status::Types::Result<Types::Clipboard::ClearProgress>;

    /// ------------------------------------------------------------
    /// Read formats and values
    /// ------------------------------------------------------------

    /// @brief Tests native clipboard availability for one portable format.
    [[nodiscard]] GameWIP::Status::Types::Result<bool> clipboardHasFormat(Types::DataTransfer::FormatView format, std::chrono::milliseconds timeout) noexcept;
    /// @brief Enumerates portable formats currently available on the clipboard.
    [[nodiscard]] GameWIP::Status::Types::Result<std::vector<Types::DataTransfer::Format>> clipboardGetFormats(std::chrono::milliseconds timeout) noexcept;

    /// @brief Reads Unicode text from the clipboard.
    [[nodiscard]] GameWIP::Status::Types::Result<std::string> clipboardReadText(std::chrono::milliseconds timeout) noexcept;
    /// @brief Reads an absolute-path list from the clipboard.
    [[nodiscard]] GameWIP::Status::Types::Result<std::vector<FileSystem::Types::Path>> clipboardReadFiles(std::chrono::milliseconds timeout) noexcept;
    /// @brief Reads an RGBA image from the clipboard.
    [[nodiscard]] GameWIP::Status::Types::Result<Types::DataTransfer::Image> clipboardReadImage(std::chrono::milliseconds timeout) noexcept;
    /// @brief Reads one named custom clipboard payload.
    [[nodiscard]] GameWIP::Status::Types::Result<std::vector<std::byte>> clipboardReadCustomData(
        std::string_view formatName,
        std::chrono::milliseconds timeout) noexcept;

    /// ------------------------------------------------------------
    /// Write and clear
    /// ------------------------------------------------------------

    /// @brief Publishes portable items to the clipboard within the access deadline.
    [[nodiscard]] ClipboardWriteResult clipboardWrite(
        std::span<const Types::DataTransfer::ItemView> items,
        std::chrono::milliseconds timeout) noexcept;
    /// @brief Clears the clipboard within the access deadline.
    [[nodiscard]] ClipboardClearResult clipboardClear(std::chrono::milliseconds timeout) noexcept;
} // namespace GameWIP::Desktop::Platform
