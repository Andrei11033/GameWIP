/// @file drag_drop_state.h
/// @brief Private DragDrop target state and fixed-capacity event queue.

#pragma once

#include "desktop/drag_drop.h"

#include <limits>
#include <memory>
#include <span>
#include <thread>
#include <vector>

namespace GameWIP::Desktop::Detail::Platform
{
    struct DragDropData;
    struct DragDropDataDeleter
    {
        void operator()(DragDropData *) const noexcept;
    };
} // namespace GameWIP::Desktop::Detail::Platform

namespace GameWIP::Desktop::Detail
{
    struct WindowState;
    /// @brief Validated, owned target region and its native format identities.
    struct DragDropRegion
    {
        Types::DragDrop::RegionId id;
        std::optional<Types::LogicalRect> rect;
        std::vector<Types::DataTransfer::Format> formats;
        std::vector<std::uint32_t> nativeFormats;
        Types::DragDrop::Effect allowedEffects = Types::DragDrop::Effect::Copy;
        Types::DragDrop::Effect preferredEffect = Types::DragDrop::Effect::Copy;
    };

    /// @brief Stable owner-thread DragDrop target state and indexed event storage.
    struct DragDropState
    {
        static constexpr std::size_t noSlot = std::numeric_limits<std::size_t>::max();

        ~DragDropState() noexcept
        {
            clearRetainedEvents();
        }
        /// @brief Clears FIFO and movement indexes before releasing event storage.
        void clearRetainedEvents() noexcept
        {
            for (std::size_t slot = eventHead; slot != noSlot; slot = queueNext[slot])
            {
                eventStorage[slot] = {};
            }
            eventHead = noSlot;
            eventTail = noSlot;
            movementHead = noSlot;
            movementTail = noSlot;
            eventCount = 0;
            eventStorage = {};
            queueNext = {};
            queuePrevious = {};
            movementNext = {};
            movementPrevious = {};
            freeNext = {};
            freeHead = noSlot;
        }

        std::unique_ptr<Platform::DragDropData, Platform::DragDropDataDeleter> platform;
        DragDropState *deferredCleanupNext = nullptr;
        std::thread::id ownerThread;
        std::uint32_t ownerNativeThreadId = 0;
        Types::WindowId windowId;
        WindowState *window = nullptr;
        std::vector<DragDropRegion> regions;
        std::vector<Types::DragDrop::Event> internalEvents;
        std::span<Types::DragDrop::Event> eventStorage;
        Types::Events::StorageKind eventStorageKind = Types::Events::StorageKind::Internal;
        std::vector<std::size_t> queueNext;        ///< Intrusive FIFO successor index for each slot.
        std::vector<std::size_t> queuePrevious;    ///< Intrusive FIFO predecessor index for each slot.
        std::vector<std::size_t> movementNext;     ///< Secondary list used to evict movement events first.
        std::vector<std::size_t> movementPrevious; ///< Secondary movement-list predecessor index.
        std::vector<std::size_t> freeNext;         ///< Intrusive free-slot successor index.
        std::size_t eventHead = noSlot;
        std::size_t eventTail = noSlot;
        std::size_t movementHead = noSlot;
        std::size_t movementTail = noSlot;
        std::size_t freeHead = noSlot;
        std::size_t eventCount = 0;
        std::uint64_t nextSequence = 1;
        std::uint64_t nextSessionId = 1;
        std::uint64_t droppedEvents = 0;
        bool nativeDestroyedPendingFinalize = false; ///< OLE/native target is gone; close must finalize portable state.
    };

    [[nodiscard]] Types::DragDrop::Effect negotiateDragDropEffect(
        Types::DragDrop::Effect source,
        Types::DragDrop::Effect target,
        Types::DragDrop::Effect preferred) noexcept;
    [[nodiscard]] bool enqueueDragDropEvent(DragDropState &, Types::DragDrop::Events::Payload, bool terminal = false) noexcept;
    void initializeDragDropEventQueue(DragDropState &);
    [[nodiscard]] Types::DragDrop::SessionId allocateDragDropSessionId(DragDropState &) noexcept;

    struct DragDropAccess
    {
        [[nodiscard]] static DragDropState *state(DragDropTarget &target) noexcept
        {
            return target.state_.get();
        }
        [[nodiscard]] static const DragDropState *state(const DragDropTarget &target) noexcept
        {
            return target.state_.get();
        }
        [[nodiscard]] static std::unique_ptr<DragDropState> &stateOwner(DragDropTarget &target) noexcept
        {
            return target.state_;
        }
    };
} // namespace GameWIP::Desktop::Detail
