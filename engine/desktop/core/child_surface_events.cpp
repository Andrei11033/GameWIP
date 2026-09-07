/// @file child_surface_events.cpp
/// @brief Fixed-capacity ChildSurface event routing.

#include "desktop/internal/child_surface_state.h"

namespace GameWIP::Desktop::Detail
{
    namespace
    {
        [[nodiscard]] std::size_t physicalIndex(const ChildSurfaceState &state, std::size_t logicalIndex) noexcept
        {
            return (state.eventHead + logicalIndex) % state.eventStorage.size();
        }

        void discardAt(ChildSurfaceState &state, std::size_t index) noexcept
        {
            for (std::size_t current = index; current + 1 < state.eventCount; ++current)
                state.eventStorage[physicalIndex(state, current)] = state.eventStorage[physicalIndex(state, current + 1)];
            state.eventStorage[physicalIndex(state, state.eventCount - 1)] = {};
            --state.eventCount;
            ++state.droppedEvents;
        }
    } // namespace

    bool isChildSurfaceEventCoalescible(const Types::ChildSurface::Events::Payload &data) noexcept
    {
        using namespace Types::ChildSurface::Events;
        return std::holds_alternative<PositionChanged>(data) || std::holds_alternative<SizeChanged>(data) ||
               std::holds_alternative<PixelSizeChanged>(data) || std::holds_alternative<ContentScaleChanged>(data);
    }

    ChildSurfaceEnqueueResult enqueueChildSurfaceEvent(ChildSurfaceState &state, Types::ChildSurface::Events::Payload data) noexcept
    {
        if (state.suppressEvents)
            return ChildSurfaceEnqueueResult::Coalesced;
        if (state.eventStorage.empty())
        {
            ++state.droppedEvents;
            return ChildSurfaceEnqueueResult::Dropped;
        }

        if (isChildSurfaceEventCoalescible(data))
        {
            for (std::size_t offset = state.eventCount; offset > 0; --offset)
            {
                auto &queued = state.eventStorage[physicalIndex(state, offset - 1)];
                if (!isChildSurfaceEventCoalescible(queued.data))
                    break;
                if (queued.data.index() == data.index())
                {
                    queued.data = data;
                    return ChildSurfaceEnqueueResult::Coalesced;
                }
            }
        }

        if (state.eventCount == state.eventStorage.size())
        {
            std::size_t replaceIndex = state.eventCount;
            for (std::size_t index = 0; index < state.eventCount; ++index)
            {
                if (isChildSurfaceEventCoalescible(state.eventStorage[physicalIndex(state, index)].data))
                {
                    replaceIndex = index;
                    break;
                }
            }
            if (replaceIndex == state.eventCount && std::holds_alternative<Types::ChildSurface::Events::NativeDestroyed>(data))
                replaceIndex = 0;
            if (replaceIndex == state.eventCount)
            {
                ++state.droppedEvents;
                return ChildSurfaceEnqueueResult::Dropped;
            }
            discardAt(state, replaceIndex);
        }

        const std::size_t slot = physicalIndex(state, state.eventCount);
        state.eventStorage[slot] = {state.nextSequence++, data};
        ++state.eventCount;
        return ChildSurfaceEnqueueResult::Queued;
    }
} // namespace GameWIP::Desktop::Detail
