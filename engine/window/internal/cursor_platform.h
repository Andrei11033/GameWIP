/// @file cursor_platform.h
/// @brief Private portable-to-native custom cursor backend contract.

#pragma once

#include "window/cursor.h"
#include "window/internal/cursor_state.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace GameWIP::Window::Detail
{
    struct WindowState;
}

namespace GameWIP::Window::Detail::Platform
{
    struct NativeCursorSnapshot
    {
        std::uint32_t hotspotX = 0;
        std::uint32_t hotspotY = 0;
        std::array<std::byte, 4> firstBgraPixel{};
        bool valid = false;
    };

    [[nodiscard]] IO::Types::Status createNativeCursorVariants(
        std::span<const Types::Cursor::ImageView> images,
        std::vector<NativeCursorVariant> &variants) noexcept;
    void destroyNativeCursorVariants(std::span<const NativeCursorVariant> variants) noexcept;

    [[nodiscard]] IO::Types::Status setCustomCursor(WindowState &window, std::shared_ptr<const CursorState> cursor) noexcept;
    [[nodiscard]] bool hasCustomCursor(const WindowState &window) noexcept;
    [[nodiscard]] std::uint32_t customCursorBindingDpi(const WindowState &window) noexcept;
    [[nodiscard]] NativeCursorSnapshot inspectNativeCursor(const NativeCursorVariant &variant) noexcept;
} // namespace GameWIP::Window::Detail::Platform
