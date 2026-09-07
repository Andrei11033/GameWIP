/// @file drag_drop.cpp
/// @brief Portable DragDrop validation, policy, lifecycle, and event queue.

#include "desktop/drag_drop.h"

#include "desktop/internal/drag_drop_platform.h"
#include "desktop/internal/drag_drop_state.h"
#include "desktop/internal/window_platform.h"
#include "desktop/internal/window_state.h"
#include "unicode/unicode.h"

#include <exception>
#include <limits>
#include <new>
#include <thread>
#include <unordered_set>
#include <utility>

namespace GameWIP::Desktop::Detail
{
    // ------------------------------------------------------------
    // Effect policy and event queue
    // ------------------------------------------------------------

    namespace
    {
        constexpr auto kAllEffects = Types::DragDrop::Effect::Copy | Types::DragDrop::Effect::Move | Types::DragDrop::Effect::Link;
        [[nodiscard]] constexpr bool validEffectBits(Types::DragDrop::Effect value) noexcept
        {
            return (static_cast<std::uint8_t>(value) & ~static_cast<std::uint8_t>(kAllEffects)) == 0;
        }
        [[nodiscard]] bool movement(const Types::DragDrop::Event &event) noexcept
        {
            return event.getIf<Types::DragDrop::Events::Moved>() != nullptr;
        }

        void unlinkMovement(DragDropState &state, std::size_t slot) noexcept
        {
            const std::size_t previous = state.movementPrevious[slot];
            const std::size_t next = state.movementNext[slot];
            if (previous == DragDropState::noSlot)
                state.movementHead = next;
            else
                state.movementNext[previous] = next;
            if (next == DragDropState::noSlot)
                state.movementTail = previous;
            else
                state.movementPrevious[next] = previous;
            state.movementPrevious[slot] = DragDropState::noSlot;
            state.movementNext[slot] = DragDropState::noSlot;
        }

        void removeSlot(DragDropState &state, std::size_t slot) noexcept
        {
            if (movement(state.eventStorage[slot]))
                unlinkMovement(state, slot);
            const std::size_t previous = state.queuePrevious[slot];
            const std::size_t next = state.queueNext[slot];
            if (previous == DragDropState::noSlot)
                state.eventHead = next;
            else
                state.queueNext[previous] = next;
            if (next == DragDropState::noSlot)
                state.eventTail = previous;
            else
                state.queuePrevious[next] = previous;
            state.eventStorage[slot] = {};
            state.queuePrevious[slot] = DragDropState::noSlot;
            state.queueNext[slot] = DragDropState::noSlot;
            state.freeNext[slot] = state.freeHead;
            state.freeHead = slot;
            --state.eventCount;
        }

        void appendSlot(DragDropState &state, std::size_t slot, Types::DragDrop::Event event) noexcept
        {
            state.eventStorage[slot] = std::move(event);
            state.queuePrevious[slot] = state.eventTail;
            state.queueNext[slot] = DragDropState::noSlot;
            if (state.eventTail == DragDropState::noSlot)
                state.eventHead = slot;
            else
                state.queueNext[state.eventTail] = slot;
            state.eventTail = slot;
            if (movement(state.eventStorage[slot]))
            {
                state.movementPrevious[slot] = state.movementTail;
                state.movementNext[slot] = DragDropState::noSlot;
                if (state.movementTail == DragDropState::noSlot)
                    state.movementHead = slot;
                else
                    state.movementNext[state.movementTail] = slot;
                state.movementTail = slot;
            }
            ++state.eventCount;
        }
    } // namespace

    void initializeDragDropEventQueue(DragDropState &state)
    {
        const std::size_t capacity = state.eventStorage.size();
        state.queueNext.assign(capacity, DragDropState::noSlot);
        state.queuePrevious.assign(capacity, DragDropState::noSlot);
        state.movementNext.assign(capacity, DragDropState::noSlot);
        state.movementPrevious.assign(capacity, DragDropState::noSlot);
        state.freeNext.resize(capacity);
        for (std::size_t slot = 0; slot < capacity; ++slot)
            state.freeNext[slot] = slot + 1 < capacity ? slot + 1 : DragDropState::noSlot;
        state.freeHead = capacity == 0 ? DragDropState::noSlot : 0;
    }

    Types::DragDrop::SessionId allocateDragDropSessionId(DragDropState &state) noexcept
    {
        std::uint64_t value = state.nextSessionId++;
        if (value == 0)
            value = state.nextSessionId++;
        return {value};
    }

    Types::DragDrop::Effect negotiateDragDropEffect(
        Types::DragDrop::Effect source,
        Types::DragDrop::Effect target,
        Types::DragDrop::Effect preferred) noexcept
    {
        if (!validEffectBits(source) || !validEffectBits(target) || !validEffectBits(preferred))
            return Types::DragDrop::Effect::None;
        const auto candidates = source & target;
        if (candidates == Types::DragDrop::Effect::None)
            return candidates;
        const auto bits = static_cast<std::uint8_t>(candidates);
        if ((bits & (bits - 1U)) == 0)
            return candidates;
        if (preferred != Types::DragDrop::Effect::None && (candidates & preferred) == preferred &&
            (static_cast<std::uint8_t>(preferred) & (static_cast<std::uint8_t>(preferred) - 1U)) == 0)
            return preferred;
        for (const auto effect : {Types::DragDrop::Effect::Copy, Types::DragDrop::Effect::Move, Types::DragDrop::Effect::Link})
            if ((candidates & effect) != Types::DragDrop::Effect::None)
                return effect;
        return Types::DragDrop::Effect::None;
    }

    bool enqueueDragDropEvent(DragDropState &state, Types::DragDrop::Events::Payload data, bool terminal) noexcept
    {
        if (state.eventStorage.empty())
            return false;
        Types::DragDrop::Event incoming{state.nextSequence++, std::move(data)};
        if (const auto *moved = incoming.getIf<Types::DragDrop::Events::Moved>(); moved != nullptr && state.eventTail != DragDropState::noSlot)
        {
            if (auto *last = state.eventStorage[state.eventTail].getIf<Types::DragDrop::Events::Moved>();
                last != nullptr && last->sessionId == moved->sessionId)
            {
                const auto previous = last->previousRegion;
                state.eventStorage[state.eventTail] = std::move(incoming);
                if (auto *replacement = state.eventStorage[state.eventTail].getIf<Types::DragDrop::Events::Moved>())
                    replacement->previousRegion = previous;
                return true;
            }
        }
        if (state.eventCount == state.eventStorage.size())
        {
            if (!terminal)
            {
                ++state.droppedEvents;
                return false;
            }
            removeSlot(state, state.movementHead != DragDropState::noSlot ? state.movementHead : state.eventHead);
            ++state.droppedEvents;
        }
        const std::size_t slot = state.freeHead;
        state.freeHead = state.freeNext[slot];
        state.freeNext[slot] = DragDropState::noSlot;
        appendSlot(state, slot, std::move(incoming));
        return true;
    }
} // namespace GameWIP::Desktop::Detail

namespace GameWIP::Desktop
{
    // ------------------------------------------------------------
    // Region validation and publication
    // ------------------------------------------------------------

    namespace
    {
        using IO::Types::ErrorCode;
        constexpr auto kAllEffects = Types::DragDrop::Effect::Copy | Types::DragDrop::Effect::Move | Types::DragDrop::Effect::Link;
        [[nodiscard]] IO::Types::Status error(ErrorCode code) noexcept
        {
            return IO::makeStatus(code);
        }
        [[nodiscard]] bool singleEffect(Types::DragDrop::Effect effect) noexcept
        {
            const auto bits = static_cast<std::uint8_t>(effect);
            return bits != 0 && (bits & (bits - 1U)) == 0 && (effect & kAllEffects) == effect;
        }
        [[nodiscard]] bool validAllowed(Types::DragDrop::Effect effect) noexcept
        {
            return effect != Types::DragDrop::Effect::None && (effect & kAllEffects) == effect;
        }
        [[nodiscard]] bool validRect(const Types::LogicalRect &rect) noexcept
        {
            const auto right = static_cast<std::int64_t>(rect.position.x) + rect.size.width;
            const auto bottom = static_cast<std::int64_t>(rect.position.y) + rect.size.height;
            return rect.size.width != 0 && rect.size.height != 0 && right <= std::numeric_limits<std::int32_t>::max() &&
                   bottom <= std::numeric_limits<std::int32_t>::max();
        }
        [[nodiscard]] bool validFormat(Types::DataTransfer::FormatView format) noexcept
        {
            using Kind = Types::DataTransfer::FormatKind;
            switch (format.kind)
            {
            case Kind::Text:
            case Kind::FileList:
            case Kind::Image:
                return format.customName.empty();
            case Kind::Custom:
                return !format.customName.empty() && format.customName.find('\0') == std::string_view::npos &&
                       Unicode::Utf8::validate(format.customName).outcome == Unicode::Types::ValidationOutcome::Valid;
            }
            return false;
        }
        [[nodiscard]] IO::Types::Status copyRegions(
            std::span<const Types::DragDrop::RegionDescription> source,
            std::vector<Detail::DragDropRegion> &destination) noexcept
        {
            if (source.empty())
                return error(ErrorCode::InvalidArgument);
            try
            {
                std::unordered_set<std::uint64_t> ids;
                destination.clear();
                destination.reserve(source.size());
                for (const auto &region : source)
                {
                    if (!region.id.isValid() || !ids.insert(region.id.value).second || (region.rect && !validRect(*region.rect)) ||
                        !validAllowed(region.allowedEffects) || !singleEffect(region.preferredEffect) ||
                        (region.allowedEffects & region.preferredEffect) != region.preferredEffect || region.formats.empty())
                        return error(ErrorCode::InvalidArgument);
                    Detail::DragDropRegion copy{region.id, region.rect, {}, {}, region.allowedEffects, region.preferredEffect};
                    std::unordered_set<std::string> identities;
                    copy.formats.reserve(region.formats.size());
                    for (const auto format : region.formats)
                    {
                        if (!validFormat(format))
                            return error(ErrorCode::InvalidArgument);
                        std::string identity = std::to_string(static_cast<int>(format.kind));
                        if (format.kind == Types::DataTransfer::FormatKind::Custom)
                            identity += ':' + std::string(format.customName);
                        if (!identities.insert(identity).second)
                            return error(ErrorCode::InvalidArgument);
                        copy.formats.push_back({format.kind, std::string(format.customName)});
                    }
                    destination.push_back(std::move(copy));
                }
                return IO::successStatus();
            }
            catch (const std::bad_alloc &)
            {
                return error(ErrorCode::OutOfMemory);
            }
            catch (...)
            {
                return error(ErrorCode::Unknown);
            }
        }
        [[nodiscard]] Detail::DragDropState *owned(Detail::DragDropState *state) noexcept
        {
            return state && state->ownerThread == std::this_thread::get_id() ? state : nullptr;
        }
    } // namespace

    // ------------------------------------------------------------
    // Target lifecycle
    // ------------------------------------------------------------

    DragDropTarget::DragDropTarget() noexcept = default;
    DragDropTarget::~DragDropTarget() noexcept
    {
        if (!state_)
            return;
        if (!Detail::Platform::dragDropTargetOwnedByCurrentThread(*state_))
        {
            if (Detail::Platform::deferDragDropCleanupToOwner(state_))
                return;
            if (!Detail::Platform::closeDragDropTargetBestEffort(*state_))
                // Dispatcher shutdown removes native resources before unregistering
                // itself, so reaching this branch indicates a broken ownership invariant.
                std::terminate();
            state_->clearRetainedEvents();
            return;
        }
        if (!Detail::Platform::closeDragDropTargetBestEffort(*state_))
        {
            if (!Detail::Platform::deferDragDropCleanupToOwner(state_))
                // An owner dispatcher must accept cleanup until its destructor has
                // finalized every active target; failure here is an invariant violation.
                std::terminate();
            return;
        }
        state_->clearRetainedEvents();
    }

    IO::Types::Status DragDropTarget::open(Window &window, const Types::DragDrop::TargetDescription &description) noexcept
    {
        return open(window, description, Events::kDefaultQueueCapacity);
    }

    IO::Types::Status DragDropTarget::open(Window &window, const Types::DragDrop::TargetDescription &description, std::size_t capacity) noexcept
    {
        if (state_)
            return error(ErrorCode::AlreadyOpen);
        if (capacity == 0)
            return error(ErrorCode::InvalidArgument);
        Detail::WindowState *windowState = Detail::WindowAccess::state(window);
        if (!windowState || !Detail::Platform::hasLiveNativeWindow(*windowState))
            return error(ErrorCode::NotOpen);
        if (!Detail::Platform::ownedByCurrentThread(*windowState))
            return error(ErrorCode::ResourceBusy);
        if (windowState->fileDropEnabled || Detail::Platform::hasDragDropTarget(*windowState))
            return error(ErrorCode::ResourceBusy);
        try
        {
            auto candidate = std::make_unique<Detail::DragDropState>();
            if (capacity > candidate->internalEvents.max_size())
                return error(ErrorCode::InvalidArgument);
            candidate->ownerThread = std::this_thread::get_id();
            candidate->windowId = windowState->id;
            candidate->window = windowState;
            IO::Types::Status result = copyRegions(description.regions, candidate->regions);
            if (!result.ok())
                return result;
            result = Detail::Platform::prepareDragDropRegions(candidate->regions);
            if (!result.ok())
                return result;
            candidate->internalEvents.resize(capacity);
            candidate->eventStorage = candidate->internalEvents;
            Detail::initializeDragDropEventQueue(*candidate);
            candidate->eventStorageKind = Types::Events::StorageKind::Internal;
            result = Detail::Platform::openDragDropTarget(*candidate, *windowState);
            if (!result.ok())
            {
                static_cast<void>(Detail::Platform::closeDragDropTargetBestEffort(*candidate));
                return result;
            }
            state_ = std::move(candidate);
            return IO::successStatus();
        }
        catch (const std::bad_alloc &)
        {
            return error(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return error(ErrorCode::Unknown);
        }
    }

    IO::Types::Status DragDropTarget::open(
        Window &window,
        const Types::DragDrop::TargetDescription &description,
        std::span<Types::DragDrop::Event> storage) noexcept
    {
        if (state_)
            return error(ErrorCode::AlreadyOpen);
        if (storage.empty())
            return error(ErrorCode::InvalidArgument);
        Detail::WindowState *windowState = Detail::WindowAccess::state(window);
        if (!windowState || !Detail::Platform::hasLiveNativeWindow(*windowState))
            return error(ErrorCode::NotOpen);
        if (!Detail::Platform::ownedByCurrentThread(*windowState) || windowState->fileDropEnabled ||
            Detail::Platform::hasDragDropTarget(*windowState))
            return error(ErrorCode::ResourceBusy);
        try
        {
            auto candidate = std::make_unique<Detail::DragDropState>();
            candidate->ownerThread = std::this_thread::get_id();
            candidate->windowId = windowState->id;
            candidate->window = windowState;
            IO::Types::Status result = copyRegions(description.regions, candidate->regions);
            if (!result.ok())
                return result;
            result = Detail::Platform::prepareDragDropRegions(candidate->regions);
            if (!result.ok())
                return result;
            candidate->eventStorage = storage;
            Detail::initializeDragDropEventQueue(*candidate);
            candidate->eventStorageKind = Types::Events::StorageKind::External;
            result = Detail::Platform::openDragDropTarget(*candidate, *windowState);
            if (!result.ok())
            {
                static_cast<void>(Detail::Platform::closeDragDropTargetBestEffort(*candidate));
                return result;
            }
            state_ = std::move(candidate);
            return IO::successStatus();
        }
        catch (const std::bad_alloc &)
        {
            return error(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return error(ErrorCode::Unknown);
        }
    }

    bool DragDropTarget::isOpen() const noexcept
    {
        return state_ && Detail::Platform::hasLiveDragDropTarget(*state_);
    }
    Types::LifetimeState DragDropTarget::lifetimeState() const noexcept
    {
        if (!state_)
            return Types::LifetimeState::Closed;
        if (state_->nativeDestroyedPendingFinalize)
            return Types::LifetimeState::NativeDestroyedPendingFinalize;
        return isOpen() ? Types::LifetimeState::Open : Types::LifetimeState::Closed;
    }
    IO::Types::Status DragDropTarget::close() noexcept
    {
        if (!state_)
            return IO::successStatus();
        if (state_->nativeDestroyedPendingFinalize && !Detail::Platform::hasNativeDragDropResources(*state_))
        {
            state_->clearRetainedEvents();
            state_.reset();
            return IO::successStatus();
        }
        if (!owned(state_.get()))
            return error(ErrorCode::ResourceBusy);
        const auto result = Detail::Platform::closeDragDropTarget(*state_);
        if (result.resourceClosed)
        {
            state_->clearRetainedEvents();
            state_.reset();
        }
        return result.status;
    }
    Types::WindowId DragDropTarget::windowId() const noexcept
    {
        return state_ ? state_->windowId : Types::WindowId{};
    }
    bool DragDropTarget::ownedByCurrentThread() const noexcept
    {
        return owned(state_.get()) != nullptr;
    }
    IO::Types::Status DragDropTarget::setRegions(std::span<const Types::DragDrop::RegionDescription> regions) noexcept
    {
        if (!isOpen())
            return error(ErrorCode::NotOpen);
        if (!owned(state_.get()))
            return error(ErrorCode::ResourceBusy);
        std::vector<Detail::DragDropRegion> candidate;
        IO::Types::Status result = copyRegions(regions, candidate);
        if (result.ok())
            result = Detail::Platform::prepareDragDropRegions(candidate);
        if (result.ok())
            state_->regions = std::move(candidate);
        return result;
    }
    // ------------------------------------------------------------
    // Target event queue
    // ------------------------------------------------------------

    bool DragDropTarget::popEvent(Types::DragDrop::Event &out) noexcept
    {
        if (!owned(state_.get()) || state_->eventCount == 0)
            return false;
        const std::size_t index = state_->eventHead;
        auto &slot = state_->eventStorage[index];
        out = std::move(slot);
        Detail::removeSlot(*state_, index);
        return true;
    }
    std::size_t DragDropTarget::popEvents(std::span<Types::DragDrop::Event> destination) noexcept
    {
        std::size_t count = 0;
        while (count < destination.size() && popEvent(destination[count]))
            ++count;
        return count;
    }
    void DragDropTarget::clearEvents() noexcept
    {
        if (!owned(state_.get()))
            return;
        while (state_->eventHead != Detail::DragDropState::noSlot)
            Detail::removeSlot(*state_, state_->eventHead);
    }
    Types::Events::QueueInfo DragDropTarget::eventQueueInfo() const noexcept
    {
        if (!state_)
            return {};
        return {state_->eventStorageKind, state_->eventStorage.size(), state_->eventCount, state_->droppedEvents};
    }
    void DragDropTarget::clearDroppedEventCount() noexcept
    {
        if (owned(state_.get()))
            state_->droppedEvents = 0;
    }
} // namespace GameWIP::Desktop

namespace GameWIP::Desktop::DragDrop
{
    // ------------------------------------------------------------
    // Source operation
    // ------------------------------------------------------------

    Types::DragDrop::Result beginDrag(Window &source, const Types::DragDrop::Description &description) noexcept
    {
        auto *state = Detail::WindowAccess::state(source);
        if (!state || !Detail::Platform::hasLiveNativeWindow(*state))
            return {.status = IO::makeStatus(IO::Types::ErrorCode::NotOpen)};
        if (!Detail::Platform::ownedByCurrentThread(*state))
            return {.status = IO::makeStatus(IO::Types::ErrorCode::ResourceBusy)};
        return Detail::Platform::beginNativeDrag(*state, description);
    }
} // namespace GameWIP::Desktop::DragDrop
