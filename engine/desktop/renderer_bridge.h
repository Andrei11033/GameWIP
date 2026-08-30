/// @file renderer_bridge.h
/// @brief Optional renderer-to-Window integration bridge.

#pragma once

#include "io/status.h"
#include "desktop/types.h"
#include "desktop/desktop_export.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace GameWIP::Desktop
{
    class Window;
}

namespace GameWIP::Desktop::Types::Renderer
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
} // namespace GameWIP::Desktop::Types::Renderer

/// @brief Optional Window and renderer presentation integration.
namespace GameWIP::Desktop::Renderer
{
    /// @name Concurrent presentation reads
    /// @{

    /// @brief Enables atomic renderer-facing presentation reads for this Window object.
    /// @details Requires an open Window on its owner thread. On first success, lazily allocates stable publication storage and immediately publishes
    /// the current authoritative presentation state. Enablement is one-way for the C++ object, persists across close/reopen, and is idempotent.
    /// Call this before another thread begins using the documented renderer-facing getters. Enabling concurrently with getter reads is unsupported.
    /// Failure leaves the facility disabled and ordinary owner-thread cached getter behavior unchanged.
    /// The broadened contract covers clientSize(), framebufferSize(), contentScale(), effectiveDpi(), currentMonitor(), presentationState(),
    /// minimized(), maximized(), visible(), interactiveMoveResizeActive(), and occluded() only. Each compound result is coherent; separate calls may
    /// observe successive states. Concurrent C++ Window destruction remains unsupported; stop or join renderer readers before destruction.
    /// @param window Window whose renderer-facing getter contract is broadened.
    /// @return Success, NotOpen for a closed Window, ResourceBusy on the wrong thread, or OutOfMemory when allocation fails.
    [[nodiscard]] GAMEWIP_DESKTOP_EXPORT IO::Types::Status enableConcurrentPresentationReads(Window &window) noexcept;

    /// @brief Returns whether concurrent renderer-facing presentation reads have been enabled.
    /// @details Returns false by default without allocating. It remains true across close/reopen after the first successful enable. After enablement
    /// completes, it may be read from another thread. Do not call concurrently with enableConcurrentPresentationReads() or C++ object destruction.
    /// @param window Window to inspect.
    /// @return true after this Window object has successfully enabled the facility.
    [[nodiscard]] GAMEWIP_DESKTOP_EXPORT bool concurrentPresentationReadsEnabled(const Window &window) noexcept;
    /// @}

    /// @name Occlusion reporting
    /// @{

    /// @brief Attaches the renderer as the Window's sole occlusion provider.
    /// @details Independent from concurrent presentation-read enablement.
    /// @param window Open Window to update; the call must run on its owner thread.
    /// @return Success, or the open-state, thread, or already-attached failure.
    [[nodiscard]] GAMEWIP_DESKTOP_EXPORT IO::Types::Status attachOcclusionProvider(Window &window) noexcept;
    /// @brief Returns whether an occlusion provider is attached.
    /// @param window Window to inspect.
    /// @return true when a renderer provider is currently attached.
    [[nodiscard]] GAMEWIP_DESKTOP_EXPORT bool hasOcclusionProvider(const Window &window) noexcept;
    /// @brief Publishes a renderer-observed occlusion transition.
    /// @param window Window receiving renderer presentation feedback.
    /// @param occluded Authoritative current renderer occlusion state.
    /// @return Success, or the open-state, thread, or missing-provider failure.
    [[nodiscard]] GAMEWIP_DESKTOP_EXPORT IO::Types::Status reportOcclusion(Window &window, bool occluded) noexcept;
    /// @brief Detaches the current occlusion provider and clears its state.
    /// @param window Window whose provider is detached.
    /// @return Success, or the open-state or wrong-thread failure.
    [[nodiscard]] GAMEWIP_DESKTOP_EXPORT IO::Types::Status detachOcclusionProvider(Window &window) noexcept;
    /// @}

    /// @name Pointer hit masks
    /// @{

    /// @brief Computes the packed-word count for a framebuffer-sized one-bit mask.
    /// @param framebufferSize Physical mask extent in pixels.
    /// @return Required 32-bit word count, or zero when the dimensions overflow.
    [[nodiscard]] GAMEWIP_DESKTOP_EXPORT std::size_t requiredPointerHitMaskWords(Types::PixelSize framebufferSize) noexcept;
    /// @brief Begins an update against the Window's current framebuffer generation.
    /// @param window Open Window to inspect on its owner thread.
    /// @return Status plus the generation, framebuffer size, and exact required word count.
    [[nodiscard]] GAMEWIP_DESKTOP_EXPORT Types::Renderer::PointerHitMaskResult beginPointerHitMaskUpdate(Window &window) noexcept;
    /// @brief Publishes an exact-size packed mask for a previously returned generation.
    /// @param window Window that produced the update target.
    /// @param generation Generation returned by beginPointerHitMaskUpdate().
    /// @param words Exact-size packed mask; bit one accepts the corresponding pixel.
    /// @return Success, or the validation, stale-generation, open-state, thread, or allocation failure.
    [[nodiscard]] GAMEWIP_DESKTOP_EXPORT IO::Types::Status publishPointerHitMask(
        Window &window,
        std::uint64_t generation,
        std::span<const Types::Renderer::PointerHitMaskWord> words) noexcept;
    /// @brief Clears any currently published pointer hit mask.
    /// @param window Window whose mask is cleared.
    /// @return Success, or the open-state or wrong-thread failure.
    [[nodiscard]] GAMEWIP_DESKTOP_EXPORT IO::Types::Status clearPointerHitMask(Window &window) noexcept;
    /// @brief Returns whether a current pointer hit mask is published.
    /// @param window Window to inspect.
    /// @return true when a mask matching the current framebuffer generation is published.
    [[nodiscard]] GAMEWIP_DESKTOP_EXPORT bool hasPointerHitMask(const Window &window) noexcept;
    /// @}
} // namespace GameWIP::Desktop::Renderer
