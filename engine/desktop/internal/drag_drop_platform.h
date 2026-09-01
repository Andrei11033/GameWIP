/// @file drag_drop_platform.h
/// @brief Internal portable-to-native backend contract for DragDrop.

#pragma once

#include "desktop/internal/drag_drop_state.h"
#include "desktop/internal/window_platform.h"

namespace GameWIP::Desktop::Detail::Platform
{
    [[nodiscard]] IO::Types::Status openDragDropTarget(DragDropState &, WindowState &) noexcept;
    [[nodiscard]] IO::Types::Status prepareDragDropRegions(std::vector<DragDropRegion> &) noexcept;
    [[nodiscard]] CloseResult closeDragDropTarget(DragDropState &) noexcept;
    [[nodiscard]] bool closeDragDropTargetBestEffort(DragDropState &) noexcept;
    void finalizeDragDropTargetForDispatcherExit(DragDropState &) noexcept;
    [[nodiscard]] bool deferDragDropCleanupToOwner(std::unique_ptr<DragDropState> &) noexcept;
    [[nodiscard]] bool dragDropTargetOwnedByCurrentThread(const DragDropState &) noexcept;
    [[nodiscard]] bool hasLiveDragDropTarget(const DragDropState &) noexcept;
    [[nodiscard]] bool hasNativeDragDropResources(const DragDropState &) noexcept;
    [[nodiscard]] bool hasDragDropTarget(const WindowState &) noexcept;
    void routeDragDropEvent(DragDropState &, Types::DragDrop::Events::Payload, bool terminal = false) noexcept;
    [[nodiscard]] bool windowClosingDragDrop(WindowState &, bool nativeDestroyed = false) noexcept;
    [[nodiscard]] Types::DragDrop::Result beginNativeDrag(WindowState &, const Types::DragDrop::Description &) noexcept;
    [[nodiscard]] IO::Types::Status prepareDragDropSource(const Types::DragDrop::Description &) noexcept;
    [[nodiscard]] IO::Types::Status testDragDropOleInitialization() noexcept;
    [[nodiscard]] IO::Types::Status testDragDropMaterialization() noexcept;
    [[nodiscard]] bool testDragDropComContracts() noexcept;
    [[nodiscard]] Types::Events::PumpResult testRouteDragDropDuringPump(DragDropState &, Types::DragDrop::Events::Payload, bool) noexcept;
    [[nodiscard]] Types::DragDrop::RegionId testMatchDragDropRegion(
        const DragDropState &,
        Types::LogicalPosition,
        std::span<const Types::DataTransfer::FormatView>) noexcept;
    [[nodiscard]] std::size_t testActiveDragDropTargetCount() noexcept;
    [[nodiscard]] std::size_t testDeferredDragDropTargetCount() noexcept;
} // namespace GameWIP::Desktop::Detail::Platform
