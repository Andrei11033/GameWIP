/// @file desktop_cursor_support.inl
/// @brief Shared image fixtures for Desktop cursor tests.

/// ------------------------------------------------------------
/// Cursor image fixtures
/// ------------------------------------------------------------

namespace CursorTest
{
    using Pixels = std::array<std::byte, 16>;

    [[nodiscard]] Pixels pixels(std::byte red = std::byte{0x11}, std::byte green = std::byte{0x22}, std::byte blue = std::byte{0x33})
    {
        return {
            red,
            green,
            blue,
            std::byte{0x44},
            std::byte{0x50},
            std::byte{0x60},
            std::byte{0x70},
            std::byte{0x80},
            std::byte{0x90},
            std::byte{0xA0},
            std::byte{0xB0},
            std::byte{0xC0},
            std::byte{0xD0},
            std::byte{0xE0},
            std::byte{0xF0},
            std::byte{0xFF}};
    }

    [[nodiscard]] Desktop::Types::Cursor::ImageView image(
        std::span<const std::byte> bytes,
        std::uint32_t dpi = 96,
        std::optional<std::size_t> stride = std::nullopt,
        Desktop::Types::Cursor::PixelPosition hotspot = {1, 1}) noexcept
    {
        return {{2, 2}, hotspot, dpi, stride, bytes};
    }

    [[nodiscard]] GameWIP::Status::Types::Result<Desktop::Cursor> create(std::span<const std::byte> bytes, std::uint32_t dpi = 96)
    {
        const Desktop::Types::Cursor::ImageView variant = image(bytes, dpi);
        return Desktop::createCursor(variant);
    }
} // namespace CursorTest
