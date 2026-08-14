/// @file events.cpp
/// @brief Fixed-capacity Window event routing and calling-thread event pumping.

#include "window/events.h"

#include "window/internal/window_platform.h"
#include "window/internal/window_state.h"

#include <utility>

namespace GameWIP::Window::Detail
{
    namespace
    {
        [[nodiscard]] std::size_t physicalIndex(const WindowState &state, std::size_t logicalIndex) noexcept
        {
            return (state.eventHead + logicalIndex) % state.eventStorage.size();
        }

        [[nodiscard]] bool sameCoalescibleType(const Types::Events::Payload &left, const Types::Events::Payload &right) noexcept
        {
            return left.index() == right.index() && isCoalescible(left);
        }

        void discardAt(WindowState &state, std::size_t index) noexcept
        {
            for (std::size_t current = index; current + 1 < state.eventCount; ++current)
                state.eventStorage[physicalIndex(state, current)] = std::move(state.eventStorage[physicalIndex(state, current + 1)]);
            state.eventStorage[physicalIndex(state, state.eventCount - 1)] = {};
            --state.eventCount;
            ++state.droppedEvents;
        }
    } // namespace

    bool isCoalescible(const Types::Events::Payload &data) noexcept
    {
        using namespace Types::Events;
        return std::holds_alternative<ClientPositionChanged>(data) || std::holds_alternative<ClientSizeChanged>(data) ||
               std::holds_alternative<FramebufferSizeChanged>(data) || std::holds_alternative<ContentScaleChanged>(data);
    }

    EnqueueResult enqueueEvent(WindowState &state, Types::Events::Payload data) noexcept
    {
        if (state.suppressEvents)
            return EnqueueResult::Coalesced;

        if (state.eventStorage.empty())
        {
            ++state.droppedEvents;
            return EnqueueResult::Dropped;
        }

        if (isCoalescible(data))
        {
            for (std::size_t offset = state.eventCount; offset > 0; --offset)
            {
                Types::Event &queued = state.eventStorage[physicalIndex(state, offset - 1)];
                if (!isCoalescible(queued.data))
                    break;
                if (sameCoalescibleType(queued.data, data))
                {
                    queued.data = std::move(data);
                    return EnqueueResult::Coalesced;
                }
            }
        }

        if (state.eventCount == state.eventStorage.size())
        {
            std::size_t replaceIndex = state.eventCount;
            for (std::size_t index = 0; index < state.eventCount; ++index)
            {
                if (isCoalescible(state.eventStorage[physicalIndex(state, index)].data))
                {
                    replaceIndex = index;
                    break;
                }
            }

            if (replaceIndex == state.eventCount && std::holds_alternative<Types::Events::NativeDestroyed>(data))
                replaceIndex = 0;
            if (replaceIndex == state.eventCount)
            {
                ++state.droppedEvents;
                return EnqueueResult::Dropped;
            }
            discardAt(state, replaceIndex);
        }

        const std::size_t slot = physicalIndex(state, state.eventCount);
        state.eventStorage[slot] = Types::Event{state.nextSequence++, std::move(data)};
        ++state.eventCount;
        return EnqueueResult::Queued;
    }

    EnqueueResult requestClose(WindowState &state, Types::Events::CloseRequestSource source) noexcept
    {
        if (state.closeRequested)
            return EnqueueResult::Coalesced;
        state.closeRequested = true;
        return enqueueEvent(state, Types::Events::CloseRequested{source});
    }
} // namespace GameWIP::Window::Detail

namespace GameWIP::Window::Events
{
    Types::Events::PumpResult poll() noexcept
    {
        return Detail::Platform::pumpEvents(kNoWait, false);
    }

    Types::Events::PumpResult wait(std::chrono::milliseconds timeout) noexcept
    {
        if (timeout < kWaitForever)
            return {.status = IO::makeStatus(IO::Types::ErrorCode::InvalidArgument)};
        return Detail::Platform::pumpEvents(timeout, true);
    }
} // namespace GameWIP::Window::Events
