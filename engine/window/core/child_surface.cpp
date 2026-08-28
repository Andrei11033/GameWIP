/// @file child_surface.cpp
/// @brief Portable ChildSurface validation, lifecycle, cached state, and backend forwarding.

#include "window/child_surface.h"

#include "window/internal/child_surface_platform.h"
#include "window/internal/child_surface_state.h"
#include "window/internal/window_platform.h"
#include "window/internal/window_state.h"
#include "window/internal/window_test_hooks.h"

#include <cmath>
#include <limits>
#include <new>
#include <thread>
#include <utility>

namespace GameWIP::Window
{
    namespace
    {
        using IO::Types::ErrorCode;

        [[nodiscard]] IO::Types::Status error(ErrorCode code) noexcept
        {
            return IO::makeStatus(code);
        }

        [[nodiscard]] Detail::ChildSurfaceState *owned(Detail::ChildSurfaceState *state) noexcept
        {
            return state != nullptr && state->ownerThread == std::this_thread::get_id() ? state : nullptr;
        }

        [[nodiscard]] IO::Types::Status requireOpen(Detail::ChildSurfaceState *state) noexcept
        {
            if (state == nullptr || !Detail::Platform::hasLiveNativeChildSurface(*state))
                return error(ErrorCode::NotOpen);
            if (owned(state) == nullptr)
                return error(ErrorCode::ResourceBusy);
            return IO::successStatus();
        }

        [[nodiscard]] Types::PixelSize physicalSize(Types::LogicalSize size, Types::Dpi dpi) noexcept
        {
            constexpr double baseline = 96.0;
            return {
                static_cast<std::uint32_t>(std::llround(static_cast<double>(size.width) * static_cast<double>(dpi.x) / baseline)),
                static_cast<std::uint32_t>(std::llround(static_cast<double>(size.height) * static_cast<double>(dpi.y) / baseline))};
        }

        void initializeCachedState(Detail::ChildSurfaceState &state, const Types::ChildSurface::Description &description) noexcept
        {
            state.ownerThread = std::this_thread::get_id();
            state.rect = description.rect;
            state.visible = description.visible;
            state.interactionEnabled = description.userInteractionEnabled;
            state.suppressEvents = true;
        }

        void releaseEventStorage(Detail::ChildSurfaceState &state) noexcept
        {
            state.clearRetainedEvents();
        }

        void queueGeometryChanges(Detail::ChildSurfaceState &state, Types::LogicalRect previous, Types::PixelSize previousPixels) noexcept
        {
            if (previous.position != state.rect.position)
                static_cast<void>(Detail::enqueueChildSurfaceEvent(state, Types::ChildSurface::Events::PositionChanged{state.rect.position}));
            if (previous.size != state.rect.size)
                static_cast<void>(Detail::enqueueChildSurfaceEvent(state, Types::ChildSurface::Events::SizeChanged{state.rect.size}));
            if (previousPixels != state.pixelSize)
                static_cast<void>(Detail::enqueueChildSurfaceEvent(state, Types::ChildSurface::Events::PixelSizeChanged{state.pixelSize}));
        }
    } // namespace

    ChildSurface::ChildSurface() noexcept = default;

    ChildSurface::~ChildSurface() noexcept
    {
        if (!state_)
            return;
        if (!Detail::Platform::isChildSurfaceOwnedByCurrentThread(*state_) && Detail::Platform::deferChildSurfaceCleanupToOwner(state_))
            return;
        Detail::Platform::closeChildSurfaceBestEffort(*state_);
        releaseEventStorage(*state_);
    }

    IO::Types::Status ChildSurface::open(Window &parent, const Types::ChildSurface::Description &description) noexcept
    {
        return open(parent, description, Events::kDefaultQueueCapacity);
    }

    IO::Types::Status ChildSurface::open(Window &parent, const Types::ChildSurface::Description &description, std::size_t eventQueueCapacity) noexcept
    {
        if (state_)
            return error(ErrorCode::AlreadyOpen);
        if (eventQueueCapacity == 0)
            return error(ErrorCode::InvalidArgument);
        Detail::WindowState *parentState = Detail::WindowAccess::state(parent);
        if (parentState == nullptr || !Detail::Platform::hasLiveNativeWindow(*parentState))
            return error(ErrorCode::NotOpen);
        if (!Detail::Platform::isOwnedByCurrentThread(*parentState))
            return error(ErrorCode::ResourceBusy);
        if (Detail::consumeFailure(TestHooks::FailurePoint::Allocation))
            return error(ErrorCode::OutOfMemory);

        try
        {
            auto candidate = std::make_unique<Detail::ChildSurfaceState>();
            initializeCachedState(*candidate, description);
            if (eventQueueCapacity > candidate->internalEvents.max_size())
                return error(ErrorCode::InvalidArgument);
            candidate->internalEvents.resize(eventQueueCapacity);
            candidate->eventStorage = candidate->internalEvents;
            candidate->eventStorageKind = Types::Events::StorageKind::Internal;
            candidate->parentId = parentState->id;
            IO::Types::Status status = Detail::Platform::openChildSurface(*candidate, *parentState);
            if (!status.ok())
            {
                Detail::Platform::closeChildSurfaceBestEffort(*candidate);
                return status;
            }
            candidate->suppressEvents = false;
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

    IO::Types::Status ChildSurface::open(
        Window &parent,
        const Types::ChildSurface::Description &description,
        std::span<Types::ChildSurface::Event> eventStorage) noexcept
    {
        if (state_)
            return error(ErrorCode::AlreadyOpen);
        if (eventStorage.empty())
            return error(ErrorCode::InvalidArgument);
        Detail::WindowState *parentState = Detail::WindowAccess::state(parent);
        if (parentState == nullptr || !Detail::Platform::hasLiveNativeWindow(*parentState))
            return error(ErrorCode::NotOpen);
        if (!Detail::Platform::isOwnedByCurrentThread(*parentState))
            return error(ErrorCode::ResourceBusy);
        if (Detail::consumeFailure(TestHooks::FailurePoint::Allocation))
            return error(ErrorCode::OutOfMemory);

        try
        {
            auto candidate = std::make_unique<Detail::ChildSurfaceState>();
            initializeCachedState(*candidate, description);
            candidate->eventStorage = eventStorage;
            candidate->eventStorageKind = Types::Events::StorageKind::External;
            candidate->parentId = parentState->id;
            IO::Types::Status status = Detail::Platform::openChildSurface(*candidate, *parentState);
            if (!status.ok())
            {
                Detail::Platform::closeChildSurfaceBestEffort(*candidate);
                return status;
            }
            candidate->suppressEvents = false;
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

    bool ChildSurface::isOpen() const noexcept
    {
        return state_ != nullptr && Detail::Platform::hasLiveNativeChildSurface(*state_);
    }

    Types::LifetimeState ChildSurface::lifetimeState() const noexcept
    {
        if (!state_)
            return Types::LifetimeState::Closed;
        if (state_->nativeDestroyedPendingFinalize)
            return Types::LifetimeState::NativeDestroyedPendingFinalize;
        return isOpen() ? Types::LifetimeState::Open : Types::LifetimeState::Closed;
    }

    IO::Types::Status ChildSurface::close() noexcept
    {
        if (!isOpen() && (!state_ || !state_->nativeDestroyedPendingFinalize))
        {
            if (state_)
                releaseEventStorage(*state_);
            state_.reset();
            return IO::successStatus();
        }
        if (owned(state_.get()) == nullptr)
            return error(ErrorCode::ResourceBusy);
        Detail::Platform::CloseResult result = Detail::Platform::closeChildSurface(*state_);
        if (result.resourceClosed)
        {
            releaseEventStorage(*state_);
            state_.reset();
        }
        return result.status;
    }

    Types::WindowId ChildSurface::parentId() const noexcept
    {
        return state_ ? state_->parentId : Types::WindowId{};
    }

    bool ChildSurface::isOwnedByCurrentThread() const noexcept
    {
        return owned(state_.get()) != nullptr;
    }

    bool ChildSurface::popEvent(Types::ChildSurface::Event &outEvent) noexcept
    {
        if (owned(state_.get()) == nullptr || state_->eventCount == 0)
            return false;
        Types::ChildSurface::Event &slot = state_->eventStorage[state_->eventHead];
        outEvent = slot;
        slot = {};
        state_->eventHead = (state_->eventHead + 1) % state_->eventStorage.size();
        --state_->eventCount;
        if (state_->eventCount == 0)
            state_->eventHead = 0;
        return true;
    }

    std::size_t ChildSurface::popEvents(std::span<Types::ChildSurface::Event> destination) noexcept
    {
        std::size_t count = 0;
        while (count < destination.size() && popEvent(destination[count]))
            ++count;
        return count;
    }

    void ChildSurface::clearEvents() noexcept
    {
        if (owned(state_.get()) == nullptr)
            return;
        for (std::size_t index = 0; index < state_->eventCount; ++index)
            state_->eventStorage[(state_->eventHead + index) % state_->eventStorage.size()] = {};
        state_->eventHead = 0;
        state_->eventCount = 0;
    }

    Types::Events::QueueInfo ChildSurface::eventQueueInfo() const noexcept
    {
        if (!state_)
            return {};
        return {state_->eventStorageKind, state_->eventStorage.size(), state_->eventCount, state_->droppedEvents};
    }

    void ChildSurface::clearDroppedEventCount() noexcept
    {
        if (owned(state_.get()) != nullptr)
            state_->droppedEvents = 0;
    }

    Types::LogicalRect ChildSurface::rect() const noexcept
    {
        return state_ ? state_->rect : Types::LogicalRect{};
    }
    Types::LogicalPosition ChildSurface::position() const noexcept
    {
        return rect().position;
    }
    Types::LogicalSize ChildSurface::size() const noexcept
    {
        return rect().size;
    }
    Types::PixelSize ChildSurface::pixelSize() const noexcept
    {
        return state_ ? state_->pixelSize : Types::PixelSize{};
    }
    Types::ScreenRect ChildSurface::screenRect() const noexcept
    {
        return state_ ? state_->screenRect : Types::ScreenRect{};
    }
    Types::ContentScale ChildSurface::contentScale() const noexcept
    {
        return state_ ? state_->contentScale : Types::ContentScale{};
    }
    Types::Dpi ChildSurface::effectiveDpi() const noexcept
    {
        return state_ ? state_->dpi : Types::Dpi{};
    }
    bool ChildSurface::isVisible() const noexcept
    {
        return state_ && state_->visible;
    }
    bool ChildSurface::isUserInteractionEnabled() const noexcept
    {
        return state_ && state_->interactionEnabled;
    }

    IO::Types::Status ChildSurface::setRect(Types::LogicalRect newRect) noexcept
    {
        IO::Types::Status status = requireOpen(state_.get());
        if (!status.ok())
            return status;
        const Types::LogicalRect previous = state_->rect;
        const Types::PixelSize previousPixels = state_->pixelSize;
        status = Detail::Platform::setChildSurfaceRect(*state_, newRect);
        if (!status.ok())
            return status;
        state_->rect = newRect;
        state_->pixelSize = physicalSize(newRect.size, state_->dpi);
        queueGeometryChanges(*state_, previous, previousPixels);
        return IO::successStatus();
    }

    IO::Types::Status ChildSurface::setPosition(Types::LogicalPosition newPosition) noexcept
    {
        Types::LogicalRect newRect = rect();
        newRect.position = newPosition;
        return setRect(newRect);
    }

    IO::Types::Status ChildSurface::setSize(Types::LogicalSize newSize) noexcept
    {
        Types::LogicalRect newRect = rect();
        newRect.size = newSize;
        return setRect(newRect);
    }

    Types::ScreenPositionResult ChildSurface::clientToScreen(Types::LogicalPosition position) const noexcept
    {
        IO::Types::Status status = requireOpen(state_.get());
        if (!status.ok())
            return {.status = std::move(status)};
        return Detail::Platform::childSurfaceClientToScreen(*state_, position);
    }

    Types::LogicalPositionResult ChildSurface::screenToClient(Types::ScreenPosition position) const noexcept
    {
        IO::Types::Status status = requireOpen(state_.get());
        if (!status.ok())
            return {.status = std::move(status)};
        return Detail::Platform::childSurfaceScreenToClient(*state_, position);
    }

    IO::Types::Status ChildSurface::show() noexcept
    {
        IO::Types::Status status = requireOpen(state_.get());
        if (!status.ok() || state_->visible)
            return status;
        status = Detail::Platform::showChildSurface(*state_, true);
        if (status.ok())
        {
            state_->visible = true;
            static_cast<void>(Detail::enqueueChildSurfaceEvent(*state_, Types::ChildSurface::Events::VisibilityChanged{true}));
        }
        return status;
    }

    IO::Types::Status ChildSurface::hide() noexcept
    {
        IO::Types::Status status = requireOpen(state_.get());
        if (!status.ok() || !state_->visible)
            return status;
        status = Detail::Platform::showChildSurface(*state_, false);
        if (status.ok())
        {
            state_->visible = false;
            static_cast<void>(Detail::enqueueChildSurfaceEvent(*state_, Types::ChildSurface::Events::VisibilityChanged{false}));
        }
        return status;
    }

    IO::Types::Status ChildSurface::setUserInteractionEnabled(bool enabled) noexcept
    {
        IO::Types::Status status = requireOpen(state_.get());
        if (!status.ok() || state_->interactionEnabled == enabled)
            return status;
        status = Detail::Platform::setChildSurfaceInteractionEnabled(*state_, enabled);
        if (status.ok())
            state_->interactionEnabled = enabled;
        return status;
    }

    IO::Types::Status ChildSurface::bringToFront() noexcept
    {
        IO::Types::Status status = requireOpen(state_.get());
        return status.ok() ? Detail::Platform::orderChildSurfaceEdge(*state_, true) : status;
    }

    IO::Types::Status ChildSurface::sendToBack() noexcept
    {
        IO::Types::Status status = requireOpen(state_.get());
        return status.ok() ? Detail::Platform::orderChildSurfaceEdge(*state_, false) : status;
    }

    IO::Types::Status ChildSurface::placeAbove(const ChildSurface &sibling) noexcept
    {
        IO::Types::Status status = requireOpen(state_.get());
        if (!status.ok())
            return status;
        const Detail::ChildSurfaceState *siblingState = sibling.state_.get();
        if (&sibling == this || siblingState == nullptr || !Detail::Platform::hasLiveNativeChildSurface(*siblingState) ||
            siblingState->parentId != state_->parentId || siblingState->ownerThread != state_->ownerThread)
            return error(ErrorCode::InvalidArgument);
        return Detail::Platform::orderChildSurface(*state_, siblingState, true);
    }

    IO::Types::Status ChildSurface::placeBelow(const ChildSurface &sibling) noexcept
    {
        IO::Types::Status status = requireOpen(state_.get());
        if (!status.ok())
            return status;
        const Detail::ChildSurfaceState *siblingState = sibling.state_.get();
        if (&sibling == this || siblingState == nullptr || !Detail::Platform::hasLiveNativeChildSurface(*siblingState) ||
            siblingState->parentId != state_->parentId || siblingState->ownerThread != state_->ownerThread)
            return error(ErrorCode::InvalidArgument);
        return Detail::Platform::orderChildSurface(*state_, siblingState, false);
    }
} // namespace GameWIP::Window
