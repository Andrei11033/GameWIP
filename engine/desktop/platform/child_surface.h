/// @file child_surface.h
/// @brief Portable backend contract for ChildSurface.

#pragma once

#include "desktop/internal/child_surface_state.h"
#include "desktop/platform/window.h"

#include <memory>

namespace GameWIP::Desktop::Platform
{
    /// ------------------------------------------------------------
    /// Shared portable state
    /// ------------------------------------------------------------

    using Internal::ChildSurfaceState;
    using Internal::WindowState;

    /// ------------------------------------------------------------
    /// Lifetime and ownership
    /// ------------------------------------------------------------

    /// @brief Opens a native child surface on the parent Window's dispatcher thread.
    [[nodiscard]] GameWIP::Status::Types::Status openChildSurface(ChildSurfaceState &state, WindowState &parent) noexcept;
    /// @brief Closes a native child surface and reports whether ownership ended.
    [[nodiscard]] CloseResult closeChildSurface(ChildSurfaceState &state) noexcept;
    /// @brief Attempts owner-thread child cleanup without throwing or publishing errors.
    void closeChildSurfaceBestEffort(ChildSurfaceState &state) noexcept;
    /// @brief Transfers wrong-thread child cleanup to the owning dispatcher.
    [[nodiscard]] bool deferChildSurfaceCleanupToOwner(std::unique_ptr<ChildSurfaceState> &state) noexcept;
    /// @brief Tests whether the current thread may mutate the child surface.
    [[nodiscard]] bool isChildSurfaceOwnedByCurrentThread(const ChildSurfaceState &state) noexcept;
    /// @brief Tests whether the child surface is still a live native resource.
    [[nodiscard]] bool hasLiveNativeChildSurface(const ChildSurfaceState &state) noexcept;
    /// @brief Returns native handles for owner-thread interoperability.
    [[nodiscard]] NativeHandleView childSurfaceNativeHandle(const ChildSurfaceState &state) noexcept;

    /// ------------------------------------------------------------
    /// Geometry and placement
    /// ------------------------------------------------------------

    /// @brief Applies a logical child rectangle and refreshes cached screen geometry.
    [[nodiscard]] GameWIP::Status::Types::Status setChildSurfaceRect(ChildSurfaceState &state, Types::LogicalRect rect) noexcept;
    /// @brief Converts a child-client logical point into screen pixels.
    [[nodiscard]] GameWIP::Status::Types::Result<Types::ScreenPosition> childSurfaceClientToScreen(
        const ChildSurfaceState &state,
        Types::LogicalPosition position) noexcept;
    /// @brief Converts a screen-pixel point into child-client logical units.
    [[nodiscard]] GameWIP::Status::Types::Result<Types::LogicalPosition> childSurfaceScreenToClient(
        const ChildSurfaceState &state,
        Types::ScreenPosition position) noexcept;

    /// ------------------------------------------------------------
    /// Visibility and ordering
    /// ------------------------------------------------------------

    /// @brief Shows or hides the native child surface.
    [[nodiscard]] GameWIP::Status::Types::Status showChildSurface(ChildSurfaceState &state, bool visible) noexcept;
    /// @brief Enables or disables native input delivery to the child surface.
    [[nodiscard]] GameWIP::Status::Types::Status setChildSurfaceInteractionEnabled(ChildSurfaceState &state, bool enabled) noexcept;
    /// @brief Places a child surface relative to one sibling in z-order.
    [[nodiscard]] GameWIP::Status::Types::Status orderChildSurface(ChildSurfaceState &state, const ChildSurfaceState *sibling, bool above) noexcept;
    /// @brief Places a child surface at the front or back of its parent's child list.
    [[nodiscard]] GameWIP::Status::Types::Status orderChildSurfaceEdge(ChildSurfaceState &state, bool front) noexcept;
} // namespace GameWIP::Desktop::Platform
