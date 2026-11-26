/// @file clipboard.cpp
/// @brief Portable Clipboard overload forwarding and single-item composition.

#include "desktop/clipboard.h"
#include "desktop/platform/clipboard.h"

#include <array>

namespace GameWIP::Desktop::Clipboard
{
    /// ------------------------------------------------------------
    /// Format and read operations
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Result<bool> hasFormat(Types::DataTransfer::FormatView format) noexcept
    {
        return hasFormat(format, kDefaultAccessTimeout);
    }

    GameWIP::Status::Types::Result<bool> hasFormat(Types::DataTransfer::FormatView format, std::chrono::milliseconds timeout) noexcept
    {
        return Platform::clipboardHasFormat(format, timeout);
    }

    GameWIP::Status::Types::Result<std::vector<Types::DataTransfer::Format>> getFormats() noexcept
    {
        return getFormats(kDefaultAccessTimeout);
    }

    GameWIP::Status::Types::Result<std::vector<Types::DataTransfer::Format>> getFormats(std::chrono::milliseconds timeout) noexcept
    {
        return Platform::clipboardGetFormats(timeout);
    }

    GameWIP::Status::Types::Result<std::string> readText() noexcept
    {
        return readText(kDefaultAccessTimeout);
    }

    GameWIP::Status::Types::Result<std::string> readText(std::chrono::milliseconds timeout) noexcept
    {
        return Platform::clipboardReadText(timeout);
    }

    GameWIP::Status::Types::Result<std::vector<FileSystem::Types::Path>> readFiles() noexcept
    {
        return readFiles(kDefaultAccessTimeout);
    }

    GameWIP::Status::Types::Result<std::vector<FileSystem::Types::Path>> readFiles(std::chrono::milliseconds timeout) noexcept
    {
        return Platform::clipboardReadFiles(timeout);
    }

    GameWIP::Status::Types::Result<Types::DataTransfer::Image> readImage() noexcept
    {
        return readImage(kDefaultAccessTimeout);
    }

    GameWIP::Status::Types::Result<Types::DataTransfer::Image> readImage(std::chrono::milliseconds timeout) noexcept
    {
        return Platform::clipboardReadImage(timeout);
    }

    GameWIP::Status::Types::Result<std::vector<std::byte>> readCustomData(std::string_view formatName) noexcept
    {
        return readCustomData(formatName, kDefaultAccessTimeout);
    }

    GameWIP::Status::Types::Result<std::vector<std::byte>> readCustomData(std::string_view formatName, std::chrono::milliseconds timeout) noexcept
    {
        return Platform::clipboardReadCustomData(formatName, timeout);
    }

    GameWIP::Status::Types::Result<Types::Clipboard::WriteProgress> writeText(std::string_view text) noexcept
    {
        return writeText(text, kDefaultAccessTimeout);
    }

    GameWIP::Status::Types::Result<Types::Clipboard::WriteProgress> writeText(std::string_view text, std::chrono::milliseconds timeout) noexcept
    {
        const std::array<Types::DataTransfer::ItemView, 1> items{Types::DataTransfer::TextView{text}};
        return write(items, timeout);
    }

    GameWIP::Status::Types::Result<Types::Clipboard::WriteProgress> writeFiles(std::span<const FileSystem::Types::Path> paths) noexcept
    {
        return writeFiles(paths, kDefaultAccessTimeout);
    }

    GameWIP::Status::Types::Result<Types::Clipboard::WriteProgress> writeFiles(
        std::span<const FileSystem::Types::Path> paths,
        std::chrono::milliseconds timeout) noexcept
    {
        const std::array<Types::DataTransfer::ItemView, 1> items{Types::DataTransfer::FileListView{paths}};
        return write(items, timeout);
    }

    GameWIP::Status::Types::Result<Types::Clipboard::WriteProgress> writeImage(const Types::DataTransfer::ImageView &image) noexcept
    {
        return writeImage(image, kDefaultAccessTimeout);
    }

    GameWIP::Status::Types::Result<Types::Clipboard::WriteProgress> writeImage(
        const Types::DataTransfer::ImageView &image,
        std::chrono::milliseconds timeout) noexcept
    {
        const std::array<Types::DataTransfer::ItemView, 1> items{image};
        return write(items, timeout);
    }

    GameWIP::Status::Types::Result<Types::Clipboard::WriteProgress> writeCustomData(const Types::DataTransfer::CustomView &data) noexcept
    {
        return writeCustomData(data, kDefaultAccessTimeout);
    }

    GameWIP::Status::Types::Result<Types::Clipboard::WriteProgress> writeCustomData(
        const Types::DataTransfer::CustomView &data,
        std::chrono::milliseconds timeout) noexcept
    {
        const std::array<Types::DataTransfer::ItemView, 1> items{data};
        return write(items, timeout);
    }

    GameWIP::Status::Types::Result<Types::Clipboard::WriteProgress> write(std::span<const Types::DataTransfer::ItemView> items) noexcept
    {
        return write(items, kDefaultAccessTimeout);
    }

    GameWIP::Status::Types::Result<Types::Clipboard::WriteProgress> write(
        std::span<const Types::DataTransfer::ItemView> items,
        std::chrono::milliseconds timeout) noexcept
    {
        return Platform::clipboardWrite(items, timeout);
    }

    GameWIP::Status::Types::Result<Types::Clipboard::ClearProgress> clear() noexcept
    {
        return clear(kDefaultAccessTimeout);
    }

    GameWIP::Status::Types::Result<Types::Clipboard::ClearProgress> clear(std::chrono::milliseconds timeout) noexcept
    {
        return Platform::clipboardClear(timeout);
    }
} // namespace GameWIP::Desktop::Clipboard
