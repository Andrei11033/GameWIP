/// @file renderer_bridge.h
/// @brief Optional renderer-to-Window integration bridge.

#pragma once

#include "io/io.h"
#include "window/types.h"
#include "window/window_export.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace GameWIP::Window
{
    class Window;
}

namespace GameWIP::Window::Types::Renderer
{
    /// @brief Packed 32-pixel acceptance word for a pointer hit mask.
    using PointerHitMaskWord = std::uint32_t;

    /// @brief Publication target returned for one pointer hit-mask update.
    struct PointerHitMaskTarget
    {
        std::uint64_t generation = 0;      ///< Per-lifetime publication generation.
        PixelSize framebufferSize;         ///< Framebuffer extent represented by the mask.
        std::size_t requiredWordCount = 0; ///< Exact packed-word count required for publication.
    };

    /// @brief Status and publication target for beginning a pointer hit-mask update.
    struct PointerHitMaskResult
    {
        IO::Types::Status status;    ///< Begin-update status.
        PointerHitMaskTarget target; ///< Publication target on success.
    };
} // namespace GameWIP::Window::Types::Renderer

/// @brief Window-side bridge for renderer-owned presentation feedback and integration data.
namespace GameWIP::Window::Renderer
{
    /// @brief Attaches the renderer as the Window's sole occlusion provider.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT IO::Types::Status attachOcclusionProvider(Window &window) noexcept;
    /// @brief Returns whether an occlusion provider is attached.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT bool hasOcclusionProvider(const Window &window) noexcept;
    /// @brief Publishes a renderer-observed occlusion transition.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT IO::Types::Status reportOcclusion(Window &window, bool occluded) noexcept;
    /// @brief Detaches the current occlusion provider and clears its state.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT IO::Types::Status detachOcclusionProvider(Window &window) noexcept;

    /// @brief Computes the packed-word count for a framebuffer-sized one-bit mask.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT std::size_t requiredPointerHitMaskWords(Types::PixelSize framebufferSize) noexcept;
    /// @brief Begins an update against the Window's current framebuffer generation.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Renderer::PointerHitMaskResult beginPointerHitMaskUpdate(Window &window) noexcept;
    /// @brief Publishes an exact-size packed mask for a previously returned generation.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT IO::Types::Status publishPointerHitMask(
        Window &window,
        std::uint64_t generation,
        std::span<const Types::Renderer::PointerHitMaskWord> words) noexcept;
    /// @brief Clears any currently published pointer hit mask.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT IO::Types::Status clearPointerHitMask(Window &window) noexcept;
    /// @brief Returns whether a current pointer hit mask is published.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT bool hasPointerHitMask(const Window &window) noexcept;
} // namespace GameWIP::Window::Renderer
