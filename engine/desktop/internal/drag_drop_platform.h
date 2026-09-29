/// @file drag_drop_platform.h
/// @brief Internal portable-to-native backend contract for DragDrop.

#pragma once

#include "desktop/internal/drag_drop_state.h"
#include "desktop/internal/window_platform.h"

namespace GameWIP::Desktop::Detail::Platform
{
    /// @brief Registers the native OLE drop target for a Window.
    [[nodiscard]] IO::Types::Status openDragDropTarget(DragDropState &, WindowState &) noexcept;
    /// @brief Validates portable drop regions and prepares native formats.
    [[nodiscard]] IO::Types::Status prepareDragDropRegions(std::vector<DragDropRegion> &) noexcept;
    /// @brief Closes a native OLE drop target.
    [[nodiscard]] CloseResult closeDragDropTarget(DragDropState &) noexcept;
    /// @brief Attempts owner-thread OLE target cleanup without publishing errors.
    [[nodiscard]] bool closeDragDropTargetBestEffort(DragDropState &) noexcept;
    /// @brief Finalizes OLE resources when the owner dispatcher exits.
    void finalizeDragDropTargetForDispatcherExit(DragDropState &) noexcept;
    /// @brief Transfers wrong-thread OLE cleanup to its owner dispatcher.
    [[nodiscard]] bool deferDragDropCleanupToOwner(std::unique_ptr<DragDropState> &) noexcept;
    /// @brief Tests whether the current thread owns the native drop target.
    [[nodiscard]] bool dragDropTargetOwnedByCurrentThread(const DragDropState &) noexcept;
    /// @brief Tests whether the native drop target is still live.
    [[nodiscard]] bool hasLiveDragDropTarget(const DragDropState &) noexcept;
    /// @brief Tests whether any native drag/drop resource remains attached.
    [[nodiscard]] bool hasNativeDragDropResources(const DragDropState &) noexcept;
    /// @brief Tests whether a Window has a registered drop target.
    [[nodiscard]] bool hasDragDropTarget(const WindowState &) noexcept;
    /// @brief Publishes one drag/drop event and records terminal delivery.
    void routeDragDropEvent(DragDropState &, Types::DragDrop::Events::Payload, bool terminal = false) noexcept;
    /// @brief Prepares drag/drop state before a Window closes.
    [[nodiscard]] bool windowClosingDragDrop(WindowState &, bool nativeDestroyed = false) noexcept;
    /// @brief Runs the native source drag operation.
    [[nodiscard]] Types::DragDrop::Result beginNativeDrag(WindowState &, const Types::DragDrop::Description &) noexcept;
    /// @brief Prepares source payloads for OLE data transfer.
    [[nodiscard]] IO::Types::Status prepareDragDropSource(const Types::DragDrop::Description &) noexcept;
    [[nodiscard]] IO::Types::Status testDragDropOleInitialization() noexcept;
    [[nodiscard]] IO::Types::Status testDragDropMaterialization() noexcept;
    [[nodiscard]] Types::DragDrop::Result testDroppedDragDropSourceResult(Types::DragDrop::Effect, Types::DragDrop::Effect) noexcept;
    [[nodiscard]] bool testDragDropComContracts() noexcept;
    [[nodiscard]] Types::Events::PumpResult testRouteDragDropDuringPump(DragDropState &, Types::DragDrop::Events::Payload, bool) noexcept;
    [[nodiscard]] Types::DragDrop::RegionId testMatchDragDropRegion(
        const DragDropState &,
        Types::LogicalPosition,
        std::span<const Types::DataTransfer::FormatView>) noexcept;
    [[nodiscard]] std::size_t testActiveDragDropTargetCount() noexcept;
    [[nodiscard]] std::size_t testDeferredDragDropTargetCount() noexcept;
} // namespace GameWIP::Desktop::Detail::Platform
