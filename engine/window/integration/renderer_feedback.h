/// @file renderer_feedback.h
/// @brief Owner-thread renderer feedback accepted by Window.

#pragma once

#include "window/window.h"

/// @brief Narrow feedback accepted from a renderer-owned presentation surface.
namespace GameWIP::Window::Integration::Renderer
{
    /// @brief Attaches the sole reliable occlusion provider for an open Window.
    /// @details The caller must run on the Window owner thread. While attached,
    /// Window::supports(Types::Capability::OcclusionReporting) returns true.
    /// @return AlreadyOpen when a provider is already attached.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT IO::Types::Status attachOcclusionProvider(Window &window) noexcept;

    /// @brief Reports the renderer's latest authoritative occlusion state.
    /// @details The caller must run on the Window owner thread after attachment. A state
    /// transition updates Window::isOccluded() before attempting to enqueue one
    /// Types::OcclusionChangedEvent. Repeated values succeed without adding an event.
    /// @return NotOpen when no provider is attached.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT IO::Types::Status reportOcclusion(Window &window, bool occluded) noexcept;

    /// @brief Detaches the provider and restores the neutral non-occluded state.
    /// @details The caller must run on the Window owner thread. Detach is idempotent while the
    /// Window remains open. Resetting a true state queues one final false transition where
    /// capacity permits. Closing the Window implicitly detaches without an observable event.
    [[nodiscard]] GAMEWIP_WINDOW_EXPORT IO::Types::Status detachOcclusionProvider(Window &window) noexcept;
} // namespace GameWIP::Window::Integration::Renderer
