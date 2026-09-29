/// @file child_surface_platform.h
/// @brief Internal portable-to-native backend contract for ChildSurface.

#pragma once

#include "desktop/internal/child_surface_state.h"
#include "desktop/internal/window_platform.h"

#include <memory>

namespace GameWIP::Desktop::Detail::Platform
{
    /// @brief Opens a child HWND owned by the parent Window's dispatcher thread.
    [[nodiscard]] IO::Types::Status openChildSurface(ChildSurfaceState &state, WindowState &parent) noexcept;
    /// @brief Closes a child HWND and reports whether native ownership ended.
    [[nodiscard]] CloseResult closeChildSurface(ChildSurfaceState &state) noexcept;
    /// @brief Attempts owner-thread child cleanup without throwing or publishing errors.
    void closeChildSurfaceBestEffort(ChildSurfaceState &state) noexcept;
    /// @brief Transfers wrong-thread child cleanup to the owning dispatcher.
    [[nodiscard]] bool deferChildSurfaceCleanupToOwner(std::unique_ptr<ChildSurfaceState> &state) noexcept;
    /// @brief Tests whether the current thread may mutate the child HWND.
    [[nodiscard]] bool isChildSurfaceOwnedByCurrentThread(const ChildSurfaceState &state) noexcept;
    /// @brief Tests whether the child HWND is still a live native resource.
    [[nodiscard]] bool hasLiveNativeChildSurface(const ChildSurfaceState &state) noexcept;
    /// @brief Returns the native module and child HWND for owner-thread interop.
    [[nodiscard]] NativeHandleView childSurfaceNativeHandle(const ChildSurfaceState &state) noexcept;
    /// @brief Applies a logical child rectangle and refreshes cached screen geometry.
    [[nodiscard]] IO::Types::Status setChildSurfaceRect(ChildSurfaceState &state, Types::LogicalRect rect) noexcept;
    /// @brief Converts a child-client logical point into screen pixels.
    [[nodiscard]] Types::ScreenPositionResult childSurfaceClientToScreen(const ChildSurfaceState &state, Types::LogicalPosition position) noexcept;
    /// @brief Converts a screen-pixel point into child-client logical units.
    [[nodiscard]] Types::LogicalPositionResult childSurfaceScreenToClient(const ChildSurfaceState &state, Types::ScreenPosition position) noexcept;
    /// @brief Shows or hides the native child surface.
    [[nodiscard]] IO::Types::Status showChildSurface(ChildSurfaceState &state, bool visible) noexcept;
    /// @brief Enables or disables native input delivery to the child surface.
    [[nodiscard]] IO::Types::Status setChildSurfaceInteractionEnabled(ChildSurfaceState &state, bool enabled) noexcept;
    /// @brief Places a child surface relative to one sibling in z-order.
    [[nodiscard]] IO::Types::Status orderChildSurface(ChildSurfaceState &state, const ChildSurfaceState *sibling, bool above) noexcept;
    /// @brief Places a child surface at the front or back of its parent's child list.
    [[nodiscard]] IO::Types::Status orderChildSurfaceEdge(ChildSurfaceState &state, bool front) noexcept;
} // namespace GameWIP::Desktop::Detail::Platform
