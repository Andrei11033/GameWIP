/// @file desktop_drag_drop.cpp
/// @brief Isolated installed-consumer probe for Desktop drag/drop lifecycle APIs.

#include "desktop/drag_drop.h"

#include <span>

[[nodiscard]] bool probeInstalledDragDrop()
{
    GameWIP::Desktop::Window closedWindow;
    GameWIP::Desktop::DragDropTarget target;

    const GameWIP::Desktop::Types::DragDrop::Result closedBegin = GameWIP::Desktop::DragDrop::beginDrag(closedWindow, {});
    const GameWIP::Status::Types::Result<void> firstClose = target.close();
    const GameWIP::Status::Types::Result<void> closedSetRegions = target.setRegions(std::span<const GameWIP::Desktop::Types::DragDrop::RegionDescription>{});
    const GameWIP::Status::Types::Result<void> closedOpen = target.open(closedWindow, {});
    const GameWIP::Status::Types::Result<void> secondClose = target.close();

    return closedBegin.status().code() == GameWIP::Status::Types::Code::NotOpen && firstClose.ok() &&
           closedSetRegions.status().code() == GameWIP::Status::Types::Code::NotOpen &&
           closedOpen.status().code() == GameWIP::Status::Types::Code::NotOpen &&
           secondClose.ok() && !target.isOpen() && target.lifetimeState() == GameWIP::Desktop::Types::LifetimeState::Closed;
}
