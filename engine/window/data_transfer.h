/// @file data_transfer.h
/// @brief Shared portable data-transfer values for Clipboard and future drag and drop.

#pragma once

#include "filesystem/filesystem.h"
#include "window/types.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace GameWIP::Window::Types::DataTransfer
{
    /// @brief Portable category of transferred data.
    enum class FormatKind
    {
        Text,     ///< Strict project-standard UTF-8 text.
        FileList, ///< Ordered native file-system paths.
        Image,    ///< sRGB, 8-bit straight-alpha RGBA image.
        Custom    ///< Opaque bytes identified by a native format name.
    };

    /// @brief Non-owning format description used as caller input.
    struct FormatView
    {
        FormatKind kind = FormatKind::Text; ///< Portable format category.
        std::string_view customName;        ///< Nonempty UTF-8 name only for Custom.
    };

    /// @brief Owning format description returned by enumeration.
    struct Format
    {
        FormatKind kind = FormatKind::Text; ///< Portable format category.
        std::string customName;             ///< Owned UTF-8 name only for Custom.
        /// @brief Compares the category and owned custom name.
        friend bool operator==(const Format &, const Format &) noexcept = default;
    };

    /// @brief Non-owning UTF-8 text item.
    struct TextView
    {
        std::string_view text; ///< Complete strict UTF-8 text.
    };

    /// @brief Non-owning ordered file-list item.
    struct FileListView
    {
        std::span<const FileSystem::Types::Path> paths; ///< Nonempty absolute paths.
    };

    /// @brief Non-owning sRGB, top-to-bottom, straight-alpha RGBA8 image.
    struct ImageView
    {
        PixelSize size;                   ///< Positive physical dimensions.
        std::size_t rowStrideBytes = 0;   ///< Zero for packed rows, otherwise at least width * 4.
        std::span<const std::byte> rgba8; ///< Exactly resolved stride times height bytes.
    };

    /// @brief Non-owning named opaque binary item.
    struct CustomView
    {
        std::string_view formatName;      ///< Nonempty strict UTF-8 native format name.
        std::span<const std::byte> bytes; ///< Opaque native-format payload, including an empty payload.
    };

    /// @brief One non-owning transfer item.
    using ItemView = std::variant<TextView, FileListView, ImageView, CustomView>;

    /// @brief Owned UTF-8 text item.
    struct Text
    {
        std::string text; ///< Complete strict UTF-8 text.
        /// @brief Compares the owned UTF-8 text.
        friend bool operator==(const Text &, const Text &) noexcept = default;
    };

    /// @brief Owned ordered file-list item.
    struct FileList
    {
        std::vector<FileSystem::Types::Path> paths; ///< Ordered materialized paths.
        /// @brief Compares ordered native paths.
        friend bool operator==(const FileList &, const FileList &) noexcept = default;
    };

    /// @brief Owned tightly packed sRGB, top-to-bottom, straight-alpha RGBA8 image.
    struct Image
    {
        PixelSize size;               ///< Positive physical dimensions when populated.
        std::vector<std::byte> rgba8; ///< Exactly width * height * 4 bytes when populated.
        /// @brief Compares dimensions and tightly packed pixels.
        friend bool operator==(const Image &, const Image &) noexcept = default;
    };

    /// @brief Owned named opaque binary item.
    struct CustomData
    {
        std::string formatName;       ///< Owned strict UTF-8 native format name.
        std::vector<std::byte> bytes; ///< Opaque native-format payload.
        /// @brief Compares the format name and opaque byte block.
        friend bool operator==(const CustomData &, const CustomData &) noexcept = default;
    };

    /// @brief One owned transfer item.
    using Item = std::variant<Text, FileList, Image, CustomData>;

    /// @brief Ordered owned multi-format transfer payload.
    using Payload = std::vector<Item>;
} // namespace GameWIP::Window::Types::DataTransfer
