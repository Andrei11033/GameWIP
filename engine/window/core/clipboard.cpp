/// @file clipboard.cpp
/// @brief Portable Clipboard overload forwarding and single-item composition.

#include "window/clipboard.h"
#include "window/internal/clipboard_platform.h"

#include <array>

namespace GameWIP::Window::Clipboard
{
    Types::Clipboard::FormatResult hasFormat(Types::DataTransfer::FormatView format) noexcept
    {
        return hasFormat(format, kDefaultAccessTimeout);
    }

    Types::Clipboard::FormatResult hasFormat(Types::DataTransfer::FormatView format, std::chrono::milliseconds timeout) noexcept
    {
        return Detail::Platform::clipboardHasFormat(format, timeout);
    }

    Types::Clipboard::FormatsResult getFormats() noexcept
    {
        return getFormats(kDefaultAccessTimeout);
    }
    Types::Clipboard::FormatsResult getFormats(std::chrono::milliseconds timeout) noexcept
    {
        return Detail::Platform::clipboardGetFormats(timeout);
    }

    Types::Clipboard::TextResult readText() noexcept
    {
        return readText(kDefaultAccessTimeout);
    }
    Types::Clipboard::TextResult readText(std::chrono::milliseconds timeout) noexcept
    {
        return Detail::Platform::clipboardReadText(timeout);
    }

    Types::Clipboard::FileListResult readFiles() noexcept
    {
        return readFiles(kDefaultAccessTimeout);
    }
    Types::Clipboard::FileListResult readFiles(std::chrono::milliseconds timeout) noexcept
    {
        return Detail::Platform::clipboardReadFiles(timeout);
    }

    Types::Clipboard::ImageResult readImage() noexcept
    {
        return readImage(kDefaultAccessTimeout);
    }
    Types::Clipboard::ImageResult readImage(std::chrono::milliseconds timeout) noexcept
    {
        return Detail::Platform::clipboardReadImage(timeout);
    }

    Types::Clipboard::CustomDataResult readCustomData(std::string_view formatName) noexcept
    {
        return readCustomData(formatName, kDefaultAccessTimeout);
    }

    Types::Clipboard::CustomDataResult readCustomData(std::string_view formatName, std::chrono::milliseconds timeout) noexcept
    {
        return Detail::Platform::clipboardReadCustomData(formatName, timeout);
    }

    Types::Clipboard::WriteResult writeText(std::string_view text) noexcept
    {
        return writeText(text, kDefaultAccessTimeout);
    }
    Types::Clipboard::WriteResult writeText(std::string_view text, std::chrono::milliseconds timeout) noexcept
    {
        const std::array<Types::DataTransfer::ItemView, 1> items{Types::DataTransfer::TextView{text}};
        return write(items, timeout);
    }

    Types::Clipboard::WriteResult writeFiles(std::span<const FileSystem::Types::Path> paths) noexcept
    {
        return writeFiles(paths, kDefaultAccessTimeout);
    }

    Types::Clipboard::WriteResult writeFiles(std::span<const FileSystem::Types::Path> paths, std::chrono::milliseconds timeout) noexcept
    {
        const std::array<Types::DataTransfer::ItemView, 1> items{Types::DataTransfer::FileListView{paths}};
        return write(items, timeout);
    }

    Types::Clipboard::WriteResult writeImage(const Types::DataTransfer::ImageView &image) noexcept
    {
        return writeImage(image, kDefaultAccessTimeout);
    }

    Types::Clipboard::WriteResult writeImage(const Types::DataTransfer::ImageView &image, std::chrono::milliseconds timeout) noexcept
    {
        const std::array<Types::DataTransfer::ItemView, 1> items{image};
        return write(items, timeout);
    }

    Types::Clipboard::WriteResult writeCustomData(const Types::DataTransfer::CustomView &data) noexcept
    {
        return writeCustomData(data, kDefaultAccessTimeout);
    }

    Types::Clipboard::WriteResult writeCustomData(const Types::DataTransfer::CustomView &data, std::chrono::milliseconds timeout) noexcept
    {
        const std::array<Types::DataTransfer::ItemView, 1> items{data};
        return write(items, timeout);
    }

    Types::Clipboard::WriteResult write(std::span<const Types::DataTransfer::ItemView> items) noexcept
    {
        return write(items, kDefaultAccessTimeout);
    }

    Types::Clipboard::WriteResult write(std::span<const Types::DataTransfer::ItemView> items, std::chrono::milliseconds timeout) noexcept
    {
        return Detail::Platform::clipboardWrite(items, timeout);
    }

    Types::Clipboard::ClearResult clear() noexcept
    {
        return clear(kDefaultAccessTimeout);
    }
    Types::Clipboard::ClearResult clear(std::chrono::milliseconds timeout) noexcept
    {
        return Detail::Platform::clipboardClear(timeout);
    }
} // namespace GameWIP::Window::Clipboard
