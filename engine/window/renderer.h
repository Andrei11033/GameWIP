/// @file renderer.h
/// @brief Optional renderer-to-Window feedback and packed pointer-hit-mask bridge.

#pragma once

#include "window/window.h"

/// @brief Renderer-owned presentation feedback accepted by Window.
namespace GameWIP::Window::Renderer
{
    /// @brief Window-owned target for one asynchronous packed-mask update.
    struct PointerHitMaskTarget
    {
        std::uint64_t generation = 0;      ///< Nonzero generation created by Window.
        Types::PixelSize framebufferSize;  ///< Coherent physical framebuffer snapshot.
        std::size_t requiredWordCount = 0; ///< Exact number of packed 64-bit words.
    };

    /// @brief Status and target returned when beginning a packed-mask update.
    struct PointerHitMaskTargetResult
    {
        IO::Types::Status status;
        PointerHitMaskTarget target;
    };

    /// @brief Attaches the sole authoritative occlusion provider.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT IO::Types::Status attachOcclusionProvider(Window &window) noexcept;
    /// @brief Publishes an owner-thread occlusion transition.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT IO::Types::Status reportOcclusion(Window &window, bool occluded) noexcept;
    /// @brief Detaches the occlusion provider and resets occlusion to false.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT IO::Types::Status detachOcclusionProvider(Window &window) noexcept;

    /// @brief Returns ceil(width * height / 64), or zero for an empty/overflowing extent.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT std::size_t requiredPointerHitMaskWords(Types::PixelSize framebufferSize) noexcept;

    /// @brief Begins the newest packed physical-framebuffer pointer-mask update.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT PointerHitMaskTargetResult beginPointerHitMaskUpdate(Window &window) noexcept;

    /// @brief Publishes a completed packed physical-framebuffer pointer mask.
    /// @details There is one row-major bit per pixel, least-significant bit first within each
    /// 64-bit word. Zero passes input through and one accepts input. Publication is owner-thread
    /// only. Only the newest Window-created generation is accepted; stale work returns Interrupted.
    /// Unused trailing bits must be zero. Same-size updates reuse active storage, and the previous
    /// mask remains active on every failure.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT IO::Types::Status publishPointerHitMask(
        Window &window,
        std::uint64_t generation,
        std::span<const std::uint64_t> words) noexcept;

    /// @brief Clears the persistent active mask on the owner thread.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT IO::Types::Status clearPointerHitMask(Window &window) noexcept;

    /// @brief Reports whether the current native lifetime has a valid active mask.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT bool hasPointerHitMask(const Window &window) noexcept;
} // namespace GameWIP::Window::Renderer
