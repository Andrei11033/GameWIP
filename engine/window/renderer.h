/// @file renderer.h
/// @brief Optional renderer-to-Window feedback and packed pointer-hit-mask bridge.

#pragma once

#include "window/window.h"

/// @brief Renderer-owned presentation feedback accepted by Window.
namespace GameWIP::Window::Renderer
{
    /// @brief Attaches the sole authoritative occlusion provider.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT IO::Types::Status attachOcclusionProvider(Window &window) noexcept;
    /// @brief Publishes an owner-thread occlusion transition.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT IO::Types::Status reportOcclusion(Window &window, bool occluded) noexcept;
    /// @brief Detaches the occlusion provider and resets occlusion to false.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT IO::Types::Status detachOcclusionProvider(Window &window) noexcept;

    /// @brief Returns ceil(width * height / 64), or zero for an empty/overflowing extent.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT std::size_t requiredPointerHitMaskWords(Types::PixelSize framebufferSize) noexcept;

    /// @brief Publishes a packed physical-framebuffer pointer mask.
    /// @details There is one row-major bit per pixel, least-significant bit first within each
    /// 64-bit word. Zero passes input through and one accepts input. Publication is owner-thread
    /// only. A revision not newer than the active revision returns ResourceBusy, so a stale GPU
    /// readback cannot replace newer data. Unused trailing bits must be zero. Same-size updates
    /// reuse active storage, and the previous mask remains active on every failure.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT IO::Types::Status publishPointerHitMask(
        Window &window,
        Types::PixelSize framebufferSize,
        std::uint64_t revision,
        std::span<const std::uint64_t> words) noexcept;

    /// @brief Clears the persistent active mask on the owner thread.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT IO::Types::Status clearPointerHitMask(Window &window) noexcept;
} // namespace GameWIP::Window::Renderer
