/// @file data_transfer.h
/// @brief Shared portable data-transfer values for Clipboard and native drag and drop.

#pragma once

#include "filesystem/path.h"
#include "desktop/types.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace GameWIP::Desktop::Types::DataTransfer
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
    /// @details The named factories make standard and custom formats distinct at the call site;
    /// a custom name cannot accidentally accompany a standard category.
    class FormatView
    {
    public:
        /// @brief Standard text format.
        [[nodiscard]] static constexpr FormatView text() noexcept
        {
            return FormatView{FormatKind::Text, {}};
        }
        /// @brief Standard file-list format.
        [[nodiscard]] static constexpr FormatView files() noexcept
        {
            return FormatView{FormatKind::FileList, {}};
        }
        /// @brief Standard image format.
        [[nodiscard]] static constexpr FormatView image() noexcept
        {
            return FormatView{FormatKind::Image, {}};
        }
        /// @brief Custom format; `name` is borrowed and must outlive the view.
        [[nodiscard]] static constexpr FormatView custom(std::string_view name) noexcept
        {
            return FormatView{FormatKind::Custom, name};
        }

        /// @brief Format category.
        [[nodiscard]] constexpr FormatKind kind() const noexcept
        {
            return kind_;
        }

        /// @brief Custom format name; empty unless `kind()` is Custom.
        [[nodiscard]] constexpr std::string_view customName() const noexcept
        {
            return customName_;
        }

    private:
        constexpr FormatView(FormatKind kind, std::string_view customName) noexcept
            : kind_(kind)
            , customName_(customName)
        {
        }

        FormatKind kind_;
        std::string_view customName_;
    };

    /// @brief Owning format description returned by enumeration.
    struct Format
    {
        FormatKind kind = FormatKind::Text; ///< Portable format category.
        std::string customName;             ///< Owned UTF-8 name only for Custom.
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
        std::span<const FileSystem::Types::Path> paths; ///< Non-empty absolute paths.
    };

    /// @brief Non-owning sRGB, top-to-bottom, straight-alpha RGBA8 image.
    struct ImageView
    {
        PixelSize size;                            ///< Positive physical dimensions.
        std::optional<std::size_t> rowStrideBytes; ///< Nullopt for packed rows; otherwise at least width * 4.
        std::span<const std::byte> rgba8;          ///< Exactly resolved stride times height bytes.

        /// @brief Image with tightly packed rows.
        [[nodiscard]] static constexpr ImageView packed(PixelSize size, std::span<const std::byte> rgba8) noexcept
        {
            return {size, std::nullopt, rgba8};
        }

        /// @brief Image with an explicit row stride in bytes.
        [[nodiscard]] static constexpr ImageView strided(PixelSize size, std::size_t rowStrideBytes, std::span<const std::byte> rgba8) noexcept
        {
            return {size, rowStrideBytes, rgba8};
        }
    };

    /// @brief Non-owning named opaque binary item.
    struct CustomView
    {
        std::string_view formatName;      ///< Non-empty strict UTF-8 native format name.
        std::span<const std::byte> bytes; ///< Opaque native-format payload; empty is representable when supported by the backend.
    };

    /// @brief One non-owning transfer item.
    using ItemView = std::variant<TextView, FileListView, ImageView, CustomView>;

    /// @brief Named constructors for transfer items, keeping the variant hidden at call sites.
    namespace Items
    {
        /// @brief Text item; borrows `value`.
        [[nodiscard]] inline ItemView text(std::string_view value) noexcept
        {
            return TextView{value};
        }
        /// @brief File-list item; borrows the span and its paths.
        [[nodiscard]] inline ItemView files(std::span<const FileSystem::Types::Path> value) noexcept
        {
            return FileListView{value};
        }
        /// @brief Image item; borrows the pixel data.
        [[nodiscard]] inline ItemView image(ImageView value) noexcept
        {
            return value;
        }
        /// @brief Custom binary item; borrows name and bytes.
        [[nodiscard]] inline ItemView custom(CustomView value) noexcept
        {
            return value;
        }
    } // namespace Items

    /// @brief Owned UTF-8 text item.
    struct Text
    {
        std::string text; ///< Complete strict UTF-8 text.
        friend bool operator==(const Text &, const Text &) noexcept = default;
    };

    /// @brief Owned ordered file-list item.
    struct FileList
    {
        std::vector<FileSystem::Types::Path> paths; ///< Ordered materialized paths.
        friend bool operator==(const FileList &, const FileList &) noexcept = default;
    };

    /// @brief Owned tightly packed sRGB, top-to-bottom, straight-alpha RGBA8 image.
    struct Image
    {
        PixelSize size;               ///< Positive physical dimensions when populated.
        std::vector<std::byte> rgba8; ///< Exactly width * height * 4 bytes when populated.
        friend bool operator==(const Image &, const Image &) noexcept = default;
    };

    /// @brief Owned named opaque binary item.
    struct CustomData
    {
        std::string formatName;       ///< Owned strict UTF-8 native format name.
        std::vector<std::byte> bytes; ///< Opaque native-format payload.
        friend bool operator==(const CustomData &, const CustomData &) noexcept = default;
    };

    /// @brief One owned transfer item.
    using Item = std::variant<Text, FileList, Image, CustomData>;

    /// @brief Ordered owned multi-format transfer payload.
    using Payload = std::vector<Item>;
} // namespace GameWIP::Desktop::Types::DataTransfer
