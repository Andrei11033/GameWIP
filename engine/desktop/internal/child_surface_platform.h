/// @file child_surface_platform.h
/// @brief Internal portable-to-native backend contract for ChildSurface.

#pragma once

#include "desktop/internal/child_surface_state.h"
#include "desktop/internal/window_platform.h"

#include <memory>

namespace GameWIP::Desktop::Detail::Platform
{
    [[nodiscard]] IO::Types::Status openChildSurface(ChildSurfaceState &state, WindowState &parent) noexcept;
    [[nodiscard]] CloseResult closeChildSurface(ChildSurfaceState &state) noexcept;
    void closeChildSurfaceBestEffort(ChildSurfaceState &state) noexcept;
    [[nodiscard]] bool deferChildSurfaceCleanupToOwner(std::unique_ptr<ChildSurfaceState> &state) noexcept;
    [[nodiscard]] bool isChildSurfaceOwnedByCurrentThread(const ChildSurfaceState &state) noexcept;
    [[nodiscard]] bool hasLiveNativeChildSurface(const ChildSurfaceState &state) noexcept;
    [[nodiscard]] NativeHandleView childSurfaceNativeHandle(const ChildSurfaceState &state) noexcept;
    [[nodiscard]] IO::Types::Status setChildSurfaceRect(ChildSurfaceState &state, Types::LogicalRect rect) noexcept;
    [[nodiscard]] Types::ScreenPositionResult childSurfaceClientToScreen(const ChildSurfaceState &state, Types::LogicalPosition position) noexcept;
    [[nodiscard]] Types::LogicalPositionResult childSurfaceScreenToClient(const ChildSurfaceState &state, Types::ScreenPosition position) noexcept;
    [[nodiscard]] IO::Types::Status showChildSurface(ChildSurfaceState &state, bool visible) noexcept;
    [[nodiscard]] IO::Types::Status setChildSurfaceInteractionEnabled(ChildSurfaceState &state, bool enabled) noexcept;
    [[nodiscard]] IO::Types::Status orderChildSurface(ChildSurfaceState &state, const ChildSurfaceState *sibling, bool above) noexcept;
    [[nodiscard]] IO::Types::Status orderChildSurfaceEdge(ChildSurfaceState &state, bool front) noexcept;
} // namespace GameWIP::Desktop::Detail::Platform
