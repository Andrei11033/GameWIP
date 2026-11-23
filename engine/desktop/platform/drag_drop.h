/// @file drag_drop.h
/// @brief Portable backend contract for DragDrop.

#pragma once

#include "desktop/internal/drag_drop_state.h"
#include "desktop/platform/window.h"

#include <memory>
#include <vector>

namespace GameWIP::Desktop::Platform
{
    /// ------------------------------------------------------------
    /// Shared portable state
    /// ------------------------------------------------------------

    using Internal::DragDropRegion;
    using Internal::DragDropState;
    using Internal::WindowState;

    /// ------------------------------------------------------------
    /// Target lifecycle and ownership
    /// ------------------------------------------------------------

    /// @brief Registers the native drop target for a Window.
    [[nodiscard]] IO::Types::Status openDragDropTarget(DragDropState &, WindowState &) noexcept;
    /// @brief Closes a native drop target.
    [[nodiscard]] CloseResult closeDragDropTarget(DragDropState &) noexcept;
    /// @brief Attempts owner-thread target cleanup without publishing errors.
    [[nodiscard]] bool closeDragDropTargetBestEffort(DragDropState &) noexcept;
    /// @brief Finalizes target resources when the owner dispatcher exits.
    void finalizeDragDropTargetForDispatcherExit(DragDropState &) noexcept;
    /// @brief Transfers wrong-thread target cleanup to its owner dispatcher.
    [[nodiscard]] bool deferDragDropCleanupToOwner(std::unique_ptr<DragDropState> &) noexcept;
    /// @brief Tests whether the current thread owns the native drop target.
    [[nodiscard]] bool dragDropTargetOwnedByCurrentThread(const DragDropState &) noexcept;
    /// @brief Tests whether the native drop target is still live.
    [[nodiscard]] bool hasLiveDragDropTarget(const DragDropState &) noexcept;
    /// @brief Tests whether any native drag/drop resource remains attached.
    [[nodiscard]] bool hasNativeDragDropResources(const DragDropState &) noexcept;
    /// @brief Tests whether a Window has a registered drop target.
    [[nodiscard]] bool hasDragDropTarget(const WindowState &) noexcept;
    /// @brief Prepares drag/drop state before a Window closes.
    [[nodiscard]] bool windowClosingDragDrop(WindowState &, bool nativeDestroyed = false) noexcept;

    /// ------------------------------------------------------------
    /// Region preparation and event routing
    /// ------------------------------------------------------------

    /// @brief Validates portable drop regions and prepares native formats.
    [[nodiscard]] IO::Types::Status prepareDragDropRegions(std::vector<DragDropRegion> &) noexcept;
    /// @brief Publishes one drag/drop event and records terminal delivery.
    void routeDragDropEvent(DragDropState &, Types::DragDrop::Events::Payload, bool terminal = false) noexcept;

    /// ------------------------------------------------------------
    /// Source drag
    /// ------------------------------------------------------------

    /// @brief Runs the native source drag operation.
    [[nodiscard]] Types::DragDrop::Result beginNativeDrag(WindowState &, const Types::DragDrop::Description &) noexcept;
} // namespace GameWIP::Desktop::Platform
