/// @file cursor_platform.h
/// @brief Private portable-to-native custom cursor backend contract.

#pragma once

#include "desktop/cursor.h"
#include "desktop/internal/cursor_state.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace GameWIP::Desktop::Detail
{
    struct WindowState;
}

namespace GameWIP::Desktop::Detail::Platform
{
    /// @brief Inspection-only snapshot of a materialized native cursor.
    struct NativeCursorSnapshot
    {
        std::uint32_t hotspotX = 0;
        std::uint32_t hotspotY = 0;
        std::array<std::byte, 4> firstBgraPixel{};
        bool valid = false;
    };

    /// @brief Creates one native cursor for each portable DPI variant.
    [[nodiscard]] IO::Types::Status createNativeCursorVariants(
        std::span<const Types::Cursor::ImageView> images,
        std::vector<NativeCursorVariant> &variants) noexcept;
    /// @brief Destroys native cursor handles after the owning cursor state expires.
    void destroyNativeCursorVariants(std::span<const NativeCursorVariant> variants) noexcept;

    /// @brief Binds a shared cursor state to a Window's native HWND.
    [[nodiscard]] IO::Types::Status setCustomCursor(WindowState &window, std::shared_ptr<const CursorState> cursor) noexcept;
    /// @brief Tests whether a Window currently has a custom cursor binding.
    [[nodiscard]] bool hasCustomCursor(const WindowState &window) noexcept;
    /// @brief Returns the intended DPI of the Window's selected cursor variant.
    [[nodiscard]] std::uint32_t customCursorBindingDpi(const WindowState &window) noexcept;
    /// @brief Inspects native cursor pixels for internal validation.
    [[nodiscard]] NativeCursorSnapshot inspectNativeCursor(const NativeCursorVariant &variant) noexcept;
} // namespace GameWIP::Desktop::Detail::Platform
