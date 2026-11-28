/// @file child_surface_state.h
/// @brief Private stable ChildSurface state and fixed-capacity event queue.

#pragma once

#include "desktop/child_surface.h"
#include "desktop/internal/helpers/event_storage.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <thread>
#include <vector>

namespace GameWIP::Desktop::Platform
{
    struct ChildSurfaceData;

    struct ChildSurfaceDataDeleter
    {
        void operator()(ChildSurfaceData *data) const noexcept;
    };
} // namespace GameWIP::Desktop::Platform

namespace GameWIP::Desktop::Internal
{
    /// @brief Outcome of inserting one ChildSurface event into fixed storage.
    enum class ChildSurfaceEnqueueResult
    {
        Queued,
        Coalesced,
        Dropped
    };

    /// @brief Stable portable ChildSurface state retained across native backend calls.
    struct ChildSurfaceState
    {
        ~ChildSurfaceState() noexcept
        {
            clearRetainedEvents();
        }

        /// @brief Clears queued payloads and releases the caller-storage view.
        void clearRetainedEvents() noexcept
        {
            clearRetainedEventStorage(eventStorage, eventHead, eventCount);
        }

        // Native ownership and parent binding
        std::unique_ptr<Platform::ChildSurfaceData, Platform::ChildSurfaceDataDeleter> platform;
        ChildSurfaceState *deferredCleanupNext = nullptr;
        std::thread::id ownerThread;
        Types::WindowId parentId;

        // Event storage and sequencing
        std::vector<Types::ChildSurface::Event> internalEvents;
        std::span<Types::ChildSurface::Event> eventStorage; ///< Internal or caller-owned slots retained until close.
        Types::Events::StorageKind eventStorageKind = Types::Events::StorageKind::Internal;
        std::size_t eventHead = 0;
        std::size_t eventCount = 0;
        std::uint64_t nextSequence = 1;
        std::uint64_t droppedEvents = 0;

        // Cached geometry and scale
        Types::LogicalRect rect;
        Types::PixelSize pixelSize;
        Types::ScreenRect screenRect;
        Types::ContentScale contentScale;
        Types::Dpi dpi;

        // Visibility and teardown
        bool visible = false;
        bool interactionEnabled = true;
        bool suppressEvents = true;                  ///< Suppresses construction-time native callbacks.
        bool nativeDestroyedPendingFinalize = false; ///< Native child surface is gone; owner-thread finalization remains.
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
} // namespace GameWIP::Desktop::Internal
