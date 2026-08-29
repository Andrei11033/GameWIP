/// @file child_surface_state.h
/// @brief Private stable ChildSurface state and fixed-capacity event queue.

#pragma once

#include "desktop/child_surface.h"

#include <memory>
#include <span>
#include <thread>
#include <vector>

namespace GameWIP::Desktop::Detail::Platform
{
    struct ChildSurfaceData;

    struct ChildSurfaceDataDeleter
    {
        void operator()(ChildSurfaceData *data) const noexcept;
    };
} // namespace GameWIP::Desktop::Detail::Platform

namespace GameWIP::Desktop::Detail
{
    enum class ChildSurfaceEnqueueResult
    {
        Queued,
        Coalesced,
        Dropped
    };

    struct ChildSurfaceState
    {
        ~ChildSurfaceState() noexcept
        {
            clearRetainedEvents();
        }

        void clearRetainedEvents() noexcept
        {
            if (!eventStorage.empty())
            {
                for (std::size_t index = 0; index < eventCount; ++index)
                    eventStorage[(eventHead + index) % eventStorage.size()] = {};
            }
            eventHead = 0;
            eventCount = 0;
            eventStorage = {};
        }

        std::unique_ptr<Platform::ChildSurfaceData, Platform::ChildSurfaceDataDeleter> platform;
        ChildSurfaceState *deferredCleanupNext = nullptr;
        std::thread::id ownerThread;
        Types::WindowId parentId;
        std::vector<Types::ChildSurface::Event> internalEvents;
        std::span<Types::ChildSurface::Event> eventStorage;
        Types::Events::StorageKind eventStorageKind = Types::Events::StorageKind::Internal;
        std::size_t eventHead = 0;
        std::size_t eventCount = 0;
        std::uint64_t nextSequence = 1;
        std::uint64_t droppedEvents = 0;
        Types::LogicalRect rect;
        Types::PixelSize pixelSize;
        Types::ScreenRect screenRect;
        Types::ContentScale contentScale;
        Types::Dpi dpi;
        bool visible = false;
        bool interactionEnabled = true;
        bool suppressEvents = true;
        bool nativeDestroyedPendingFinalize = false;
    };

    [[nodiscard]] bool isChildSurfaceEventCoalescible(const Types::ChildSurface::Events::Payload &data) noexcept;
    [[nodiscard]] ChildSurfaceEnqueueResult enqueueChildSurfaceEvent(ChildSurfaceState &state, Types::ChildSurface::Events::Payload data) noexcept;

    struct ChildSurfaceAccess
    {
        [[nodiscard]] static ChildSurfaceState *state(ChildSurface &surface) noexcept
        {
            return surface.state_.get();
        }

        [[nodiscard]] static const ChildSurfaceState *state(const ChildSurface &surface) noexcept
        {
            return surface.state_.get();
        }

        [[nodiscard]] static std::unique_ptr<ChildSurfaceState> &stateOwner(ChildSurface &surface) noexcept
        {
            return surface.state_;
        }
    };
} // namespace GameWIP::Desktop::Detail
