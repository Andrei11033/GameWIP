/// @file renderer_bridge.h
/// @brief Optional renderer-to-Window integration bridge.

#pragma once

#include "window/window.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace GameWIP::Window::Types::Renderer
{
    using PointerHitMaskWord = std::uint32_t;

    struct PointerHitMaskTarget
    {
        std::uint64_t generation = 0;
        PixelSize framebufferSize;
        std::size_t requiredWordCount = 0;
    };

    struct PointerHitMaskResult
    {
        IO::Types::Status status;
        PointerHitMaskTarget target;
    };
} // namespace GameWIP::Window::Types::Renderer

/// @brief Window-side bridge for renderer-owned presentation feedback and integration data.
namespace GameWIP::Window::Renderer
{
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT IO::Types::Status attachOcclusionProvider(Window &window) noexcept;
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT bool hasOcclusionProvider(const Window &window) noexcept;
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT IO::Types::Status reportOcclusion(Window &window, bool occluded) noexcept;
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT IO::Types::Status detachOcclusionProvider(Window &window) noexcept;

    [[nodiscard]] GAMEWIP_WINDOW_EXPORT std::size_t requiredPointerHitMaskWords(Types::PixelSize framebufferSize) noexcept;
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT Types::Renderer::PointerHitMaskResult beginPointerHitMaskUpdate(Window &window) noexcept;
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT IO::Types::Status publishPointerHitMask(
        Window &window,
        std::uint64_t generation,
        std::span<const Types::Renderer::PointerHitMaskWord> words) noexcept;
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT IO::Types::Status clearPointerHitMask(Window &window) noexcept;
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT bool hasPointerHitMask(const Window &window) noexcept;
} // namespace GameWIP::Window::Renderer
