/// @file types.h
/// @brief Shared portable value vocabulary for GameWIP Window.

#pragma once

#include <cstdint>
#include <optional>

/// @brief Portable desktop-window ownership, state, event, display, and control APIs.
namespace GameWIP::Window
{
    /// @brief Passive portable values shared across focused Window API surfaces.
    namespace Types
    {
        /// @brief Process-local identity of one successful Window open lifetime.
        struct WindowId
        {
            std::uint64_t value = 0; ///< Opaque identity, or zero when invalid.

            /// @brief Returns whether this identity is nonzero.
            [[nodiscard]] constexpr bool isValid() const noexcept
            {
                return value != 0;
            }

            /// @brief Compares opaque identity values.
            friend constexpr bool operator==(WindowId, WindowId) noexcept = default;
        };

        /// @brief Position in a Window's logical client coordinate space.
        struct LogicalPosition
        {
            std::int32_t x = 0;
            std::int32_t y = 0;
            friend constexpr bool operator==(LogicalPosition, LogicalPosition) noexcept = default;
        };

        /// @brief Extent in DPI-independent logical client units.
        struct LogicalSize
        {
            std::uint32_t width = 0;
            std::uint32_t height = 0;
            friend constexpr bool operator==(LogicalSize, LogicalSize) noexcept = default;
        };

        /// @brief Physical drawable extent in pixels.
        struct PixelSize
        {
            std::uint32_t width = 0;
            std::uint32_t height = 0;
            friend constexpr bool operator==(PixelSize, PixelSize) noexcept = default;
        };

        /// @brief Client-local logical rectangle.
        struct LogicalRect
        {
            LogicalPosition position;
            LogicalSize size;
            friend constexpr bool operator==(LogicalRect, LogicalRect) noexcept = default;
        };

        /// @brief Physical position in the shared virtual-screen coordinate space.
        struct ScreenPosition
        {
            std::int32_t x = 0;
            std::int32_t y = 0;
            friend constexpr bool operator==(ScreenPosition, ScreenPosition) noexcept = default;
        };

        /// @brief Physical rectangle in the shared virtual-screen coordinate space.
        struct ScreenRect
        {
            ScreenPosition position;
            PixelSize size;
            friend constexpr bool operator==(ScreenRect, ScreenRect) noexcept = default;
        };

        /// @brief Logical distances between a client area and complete outer frame.
        struct Insets
        {
            std::uint32_t left = 0;
            std::uint32_t top = 0;
            std::uint32_t right = 0;
            std::uint32_t bottom = 0;
            friend constexpr bool operator==(Insets, Insets) noexcept = default;
        };

        /// @brief Physical-pixel scale relative to baseline logical units.
        struct ContentScale
        {
            float x = 1.0F;
            float y = 1.0F;
            friend constexpr bool operator==(ContentScale, ContentScale) noexcept = default;
        };

        /// @brief Effective dots per inch reported for content.
        struct Dpi
        {
            float x = 0.0F;
            float y = 0.0F;
            friend constexpr bool operator==(Dpi, Dpi) noexcept = default;
        };

        /// @brief Optional logical client-size constraints.
        struct SizeLimits
        {
            std::optional<LogicalSize> minimum;
            std::optional<LogicalSize> maximum;
            friend bool operator==(const SizeLimits &, const SizeLimits &) noexcept = default;
        };

        /// @brief Positive width-to-height ratio.
        struct AspectRatio
        {
            std::uint32_t numerator = 1;
            std::uint32_t denominator = 1;
            friend constexpr bool operator==(AspectRatio, AspectRatio) noexcept = default;
        };

        /// @brief Native presentation state independent from top-level Window mode.
        enum class PresentationState
        {
            Normal,
            Minimized,
            Maximized
        };
    } // namespace Types
} // namespace GameWIP::Window
