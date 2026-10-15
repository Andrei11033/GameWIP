/// @file desktop_drag_drop.cpp
/// @brief Isolated installed-consumer probe for Desktop drag/drop lifecycle APIs.

#include "desktop/drag_drop.h"

#include <span>

bool probeInstalledDragDrop()
{
    GameWIP::Desktop::Window closedWindow;
    GameWIP::Desktop::DragDropTarget target;

    const GameWIP::Desktop::Types::DragDrop::Result closedBegin = GameWIP::Desktop::DragDrop::beginDrag(closedWindow, {});
    const GameWIP::IO::Types::Status firstClose = target.close();
    const GameWIP::IO::Types::Status closedSetRegions = target.setRegions(std::span<const GameWIP::Desktop::Types::DragDrop::RegionDescription>{});
    const GameWIP::IO::Types::Status closedOpen = target.open(closedWindow, {});
    const GameWIP::IO::Types::Status secondClose = target.close();

    return closedBegin.status.code == GameWIP::IO::Types::ErrorCode::NotOpen && firstClose.ok() &&
           closedSetRegions.code == GameWIP::IO::Types::ErrorCode::NotOpen && closedOpen.code == GameWIP::IO::Types::ErrorCode::NotOpen &&
           secondClose.ok() && !target.isOpen() && target.lifetimeState() == GameWIP::Desktop::Types::LifetimeState::Closed;
}
