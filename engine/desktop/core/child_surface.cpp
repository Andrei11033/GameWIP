/// @file child_surface.cpp
/// @brief Portable ChildSurface validation, lifecycle, cached state, and backend forwarding.

#include "desktop/child_surface.h"

#include "desktop/internal/child_surface_state.h"
#include "desktop/internal/desktop_test_hooks.h"
#include "desktop/internal/results.h"
#include "desktop/internal/window_state.h"
#include "desktop/platform/child_surface.h"
#include "desktop/platform/window.h"

#include <cmath>
#include <new>
#include <thread>
#include <utility>

namespace GameWIP::Desktop
{
    /// ------------------------------------------------------------
    /// Validation and cached-state helpers
    /// ------------------------------------------------------------

    namespace
    {
        using ErrorCode = GameWIP::Status::Types::Code;

        /// @brief Builds a portable status from a single error code.
        [[nodiscard]] GameWIP::Status::Types::Status error(ErrorCode code) noexcept
        {
            return GameWIP::Desktop::Internal::statusFor(code);
        }

        /// @brief Returns the state only when the current thread owns the ChildSurface lifetime.
        [[nodiscard]] Internal::ChildSurfaceState *owned(Internal::ChildSurfaceState *state) noexcept
        {
            return state != nullptr && state->ownerThread == std::this_thread::get_id() ? state : nullptr;
        }

        /// @brief Validates both native liveness and owner-thread access for a mutating operation.
        [[nodiscard]] GameWIP::Status::Types::Status requireOpen(Internal::ChildSurfaceState *state) noexcept
        {
            if(state == nullptr || !Platform::hasLiveNativeChildSurface(*state))
            {
                return error(ErrorCode::NotOpen);
            }

            if(owned(state) == nullptr)
            {
                return error(ErrorCode::ResourceBusy);
            }

            return GameWIP::Status::Types::Status{};
        }

        /// @brief Converts baseline logical units to physical pixels using the current effective DPI.
        [[nodiscard]] Types::PixelSize physicalSize(Types::LogicalSize size, Types::Dpi dpi) noexcept
        {
            constexpr double baseline = 96.0;
            return {
                static_cast<std::uint32_t>(std::llround(static_cast<double>(size.width) * static_cast<double>(dpi.x) / baseline)),
                static_cast<std::uint32_t>(std::llround(static_cast<double>(size.height) * static_cast<double>(dpi.y) / baseline))};
        }

        /// @brief Seeds portable state before native creation and suppresses construction-time events.
        void initializeCachedState(Internal::ChildSurfaceState &state, const Types::ChildSurface::Description &description) noexcept
        {
            state.ownerThread = std::this_thread::get_id();
            state.rect = description.rect;
            state.visible = description.visible;
            state.interactionEnabled = description.userInteractionEnabled;
            state.suppressEvents = true;
        }

        /// @brief Releases retained queue entries before state storage is destroyed or replaced.
        void releaseEventStorage(Internal::ChildSurfaceState &state) noexcept
        {
            state.clearRetainedEvents();
        }

        /// @brief Publishes only the geometry events whose cached values changed.
        void queueGeometryChanges(Internal::ChildSurfaceState &state, Types::LogicalRect previous, Types::PixelSize previousPixels) noexcept
        {
            if(previous.position != state.rect.position)
            {
                static_cast<void>(Internal::enqueueChildSurfaceEvent(state, Types::ChildSurface::Events::PositionChanged{state.rect.position}));
            }

            if(previous.size != state.rect.size)
            {
                static_cast<void>(Internal::enqueueChildSurfaceEvent(state, Types::ChildSurface::Events::SizeChanged{state.rect.size}));
            }

            if(previousPixels != state.pixelSize)
            {
                static_cast<void>(Internal::enqueueChildSurfaceEvent(state, Types::ChildSurface::Events::PixelSizeChanged{state.pixelSize}));
            }
        }
    } // namespace

    /// ------------------------------------------------------------
    /// Lifecycle and ownership
    /// ------------------------------------------------------------

    ChildSurface::ChildSurface() noexcept = default;

    ChildSurface::~ChildSurface() noexcept
    {
        if(!state_)
        {
            return;
        }

        if(!Platform::isChildSurfaceOwnedByCurrentThread(*state_) && Platform::deferChildSurfaceCleanupToOwner(state_))
        {
            return;
        }
        Platform::closeChildSurfaceBestEffort(*state_);
        releaseEventStorage(*state_);
    }

    GameWIP::Status::Types::Result<void> ChildSurface::open(Window &parent, const Types::ChildSurface::Description &description) noexcept
    {
        return open(parent, description, Types::ChildSurface::QueueConfiguration::owned());
    }

    GameWIP::Status::Types::Result<void> ChildSurface::open(
        Window &parent,
        const Types::ChildSurface::Description &description,
        Types::ChildSurface::QueueConfiguration queue) noexcept
    {
        // Validate the wrapper and requested queue storage.
        if(state_)
        {
            return Internal::result(error(ErrorCode::AlreadyOpen));
        }

        const auto *ownedQueue = std::get_if<Types::ChildSurface::QueueConfiguration::Owned>(&queue.storage);
        const auto *borrowedQueue = std::get_if<Types::ChildSurface::QueueConfiguration::Borrowed>(&queue.storage);

        if((ownedQueue != nullptr && ownedQueue->capacity == 0) || (borrowedQueue != nullptr && borrowedQueue->storage.empty()))
        {
            return Internal::result(error(ErrorCode::InvalidArgument));
        }

        // Require a live parent owned by the calling thread.
        Internal::WindowState *parentState = Internal::WindowAccess::state(parent);

        if(parentState == nullptr || !Platform::hasLiveNativeWindow(*parentState))
        {
            return Internal::result(error(ErrorCode::NotOpen));
        }

        if(!Platform::ownedByCurrentThread(*parentState))
        {
            return Internal::result(error(ErrorCode::ResourceBusy));
        }

        if(Internal::consumeFailure(Internal::TestHooks::FailurePoint::Allocation))
        {
            return Internal::result(error(ErrorCode::OutOfMemory));
        }

        try
        {
            // Allocate state and bind the selected event-storage policy.
            auto candidate = std::make_unique<Internal::ChildSurfaceState>();
            initializeCachedState(*candidate, description);

            if(ownedQueue != nullptr)
            {
                if(ownedQueue->capacity > candidate->internalEvents.max_size())
                {
                    return Internal::result(error(ErrorCode::InvalidArgument));
                }
                candidate->internalEvents.resize(ownedQueue->capacity);
                candidate->eventStorage = candidate->internalEvents;
                candidate->eventStorageKind = Types::Events::StorageKind::Internal;
            }
            else
            {
                candidate->eventStorage = borrowedQueue->storage;
                candidate->eventStorageKind = Types::Events::StorageKind::External;
            }

            candidate->parentId = parentState->id;

            // Open the native surface before publishing the candidate state.
            GameWIP::Status::Types::Status status = Platform::openChildSurface(*candidate, *parentState);
            if(!status.ok())
            {
                Platform::closeChildSurfaceBestEffort(*candidate);
                return Internal::result(status);
            }
            candidate->suppressEvents = false;
            state_ = std::move(candidate);
            return GameWIP::Status::Types::Result<void>::success();
        }
        catch(const std::bad_alloc &)
        {
            return Internal::result(error(ErrorCode::OutOfMemory));
        }
        catch(...)
        {
            return Internal::result(error(ErrorCode::Unknown));
        }
    }

    bool ChildSurface::isOpen() const noexcept
    {
        return state_ != nullptr && Platform::hasLiveNativeChildSurface(*state_);
    }

    Types::LifetimeState ChildSurface::lifetimeState() const noexcept
    {
        if(!state_)
        {
            return Types::LifetimeState::Closed;
        }

        if(state_->nativeDestroyedPendingFinalize)
        {
            return Types::LifetimeState::NativeDestroyedPendingFinalize;
        }

        return isOpen() ? Types::LifetimeState::Open : Types::LifetimeState::Closed;
    }

    GameWIP::Status::Types::Result<void> ChildSurface::close() noexcept
    {
        // Release portable state when no native cleanup remains.
        if(!isOpen() && (!state_ || !state_->nativeDestroyedPendingFinalize))
        {
            if(state_)
            {
                releaseEventStorage(*state_);
            }
            state_.reset();
            return GameWIP::Status::Types::Result<void>::success();
        }

        // Native teardown must run on the surface's owner thread.
        if(owned(state_.get()) == nullptr)
        {
            return Internal::result(error(ErrorCode::ResourceBusy));
        }

        Platform::CloseResult result = Platform::closeChildSurface(*state_);

        // Retain state only when the backend still owns a native resource.
        if(result.hasValue() && result.value())
        {
            releaseEventStorage(*state_);
            state_.reset();
        }

        return Internal::result(result.status());
    }

    /// ------------------------------------------------------------
    /// Cached state and event queue
    /// ------------------------------------------------------------

    Types::WindowId ChildSurface::parentId() const noexcept
    {
        return state_ ? state_->parentId : Types::WindowId{};
    }

    bool ChildSurface::ownedByCurrentThread() const noexcept
    {
        return owned(state_.get()) != nullptr;
    }

    bool ChildSurface::popEvent(Types::ChildSurface::Event &outEvent) noexcept
    {
        if(owned(state_.get()) == nullptr || state_->eventCount == 0)
        {
            return false;
        }
        Types::ChildSurface::Event &slot = state_->eventStorage[state_->eventHead];
        outEvent = slot;
        slot = {};
        state_->eventHead = (state_->eventHead + 1) % state_->eventStorage.size();
        --state_->eventCount;

        if(state_->eventCount == 0)
        {
            state_->eventHead = 0;
        }

        return true;
    }

    std::size_t ChildSurface::popEvents(std::span<Types::ChildSurface::Event> destination) noexcept
    {
        std::size_t count = 0;

        while(count < destination.size() && popEvent(destination[count]))
        {
            ++count;
        }

        return count;
    }

    void ChildSurface::clearEvents() noexcept
    {
        if(owned(state_.get()) == nullptr)
        {
            return;
        }

        for(std::size_t index = 0; index < state_->eventCount; ++index)
        {
            state_->eventStorage[(state_->eventHead + index) % state_->eventStorage.size()] = {};
        }

        state_->eventHead = 0;
        state_->eventCount = 0;
    }

    Types::Events::QueueInfo ChildSurface::eventQueueInfo() const noexcept
    {
        if(!state_)
        {
            return {};
        }
        return {state_->eventStorageKind, state_->eventStorage.size(), state_->eventCount, state_->droppedEvents};
    }

    void ChildSurface::clearDroppedEventCount() noexcept
    {
        if(owned(state_.get()) != nullptr)
        {
            state_->droppedEvents = 0;
        }
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

    bool ChildSurface::visible() const noexcept
    {
        return state_ && state_->visible;
    }

    bool ChildSurface::userInteractionEnabled() const noexcept
    {
        return state_ && state_->interactionEnabled;
    }

    /// ------------------------------------------------------------
    /// Geometry and coordinate conversion
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Result<void> ChildSurface::setRect(Types::LogicalRect newRect) noexcept
    {
        GameWIP::Status::Types::Status status = requireOpen(state_.get());

        if(!status.ok())
        {
            return Internal::result(status);
        }

        const Types::LogicalRect previous = state_->rect;
        const Types::PixelSize previousPixels = state_->pixelSize;
        status = Platform::setChildSurfaceRect(*state_, newRect);

        if(!status.ok())
        {
            return Internal::result(status);
        }

        state_->rect = newRect;
        state_->pixelSize = physicalSize(newRect.size, state_->dpi);
        queueGeometryChanges(*state_, previous, previousPixels);
        return GameWIP::Status::Types::Result<void>::success();
    }

    GameWIP::Status::Types::Result<void> ChildSurface::setPosition(Types::LogicalPosition newPosition) noexcept
    {
        Types::LogicalRect newRect = rect();
        newRect.position = newPosition;
        return setRect(newRect);
    }

    GameWIP::Status::Types::Result<void> ChildSurface::setSize(Types::LogicalSize newSize) noexcept
    {
        Types::LogicalRect newRect = rect();
        newRect.size = newSize;
        return setRect(newRect);
    }

    GameWIP::Status::Types::Result<Types::ScreenPosition> ChildSurface::clientToScreen(Types::LogicalPosition position) const noexcept
    {
        GameWIP::Status::Types::Status status = requireOpen(state_.get());

        if(!status.ok())
        {
            return GameWIP::Status::Types::Result<Types::ScreenPosition>::failure(status);
        }

        auto result = Platform::childSurfaceClientToScreen(*state_, position);
        return result;
    }

    GameWIP::Status::Types::Result<Types::LogicalPosition> ChildSurface::screenToClient(Types::ScreenPosition position) const noexcept
    {
        GameWIP::Status::Types::Status status = requireOpen(state_.get());

        if(!status.ok())
        {
            return GameWIP::Status::Types::Result<Types::LogicalPosition>::failure(status);
        }

        auto result = Platform::childSurfaceScreenToClient(*state_, position);
        return result;
    }

    /// ------------------------------------------------------------
    /// Visibility and sibling ordering
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Result<void> ChildSurface::show() noexcept
    {
        GameWIP::Status::Types::Status status = requireOpen(state_.get());

        if(!status.ok() || state_->visible)
        {
            return Internal::result(status);
        }

        status = Platform::showChildSurface(*state_, true);

        if(status.ok())
        {
            state_->visible = true;
            static_cast<void>(Internal::enqueueChildSurfaceEvent(*state_, Types::ChildSurface::Events::VisibilityChanged{true}));
        }

        return Internal::result(status);
    }

    GameWIP::Status::Types::Result<void> ChildSurface::hide() noexcept
    {
        GameWIP::Status::Types::Status status = requireOpen(state_.get());

        if(!status.ok() || !state_->visible)
        {
            return Internal::result(status);
        }

        status = Platform::showChildSurface(*state_, false);

        if(status.ok())
        {
            state_->visible = false;
            static_cast<void>(Internal::enqueueChildSurfaceEvent(*state_, Types::ChildSurface::Events::VisibilityChanged{false}));
        }

        return Internal::result(status);
    }

    GameWIP::Status::Types::Result<void> ChildSurface::setUserInteractionEnabled(bool enabled) noexcept
    {
        GameWIP::Status::Types::Status status = requireOpen(state_.get());

        if(!status.ok() || state_->interactionEnabled == enabled)
        {
            return Internal::result(status);
        }

        status = Platform::setChildSurfaceInteractionEnabled(*state_, enabled);

        if(status.ok())
        {
            state_->interactionEnabled = enabled;
        }

        return Internal::result(status);
    }

    GameWIP::Status::Types::Result<void> ChildSurface::bringToFront() noexcept
    {
        GameWIP::Status::Types::Status status = requireOpen(state_.get());
        return Internal::result(status.ok() ? Platform::orderChildSurfaceEdge(*state_, true) : status);
    }

    GameWIP::Status::Types::Result<void> ChildSurface::sendToBack() noexcept
    {
        GameWIP::Status::Types::Status status = requireOpen(state_.get());
        return Internal::result(status.ok() ? Platform::orderChildSurfaceEdge(*state_, false) : status);
    }

    GameWIP::Status::Types::Result<void> ChildSurface::placeAbove(const ChildSurface &sibling) noexcept
    {
        GameWIP::Status::Types::Status status = requireOpen(state_.get());

        if(!status.ok())
        {
            return Internal::result(status);
        }

        const Internal::ChildSurfaceState *siblingState = sibling.state_.get();

        if(&sibling == this || siblingState == nullptr || !Platform::hasLiveNativeChildSurface(*siblingState) ||
           siblingState->parentId != state_->parentId || siblingState->ownerThread != state_->ownerThread)
        {
            return Internal::result(error(ErrorCode::InvalidArgument));
        }
        return Internal::result(Platform::orderChildSurface(*state_, siblingState, true));
    }

    GameWIP::Status::Types::Result<void> ChildSurface::placeBelow(const ChildSurface &sibling) noexcept
    {
        GameWIP::Status::Types::Status status = requireOpen(state_.get());

        if(!status.ok())
        {
            return Internal::result(status);
        }

        const Internal::ChildSurfaceState *siblingState = sibling.state_.get();

        if(&sibling == this || siblingState == nullptr || !Platform::hasLiveNativeChildSurface(*siblingState) ||
           siblingState->parentId != state_->parentId || siblingState->ownerThread != state_->ownerThread)
        {
            return Internal::result(error(ErrorCode::InvalidArgument));
        }
        return Internal::result(Platform::orderChildSurface(*state_, siblingState, false));
    }
} // namespace GameWIP::Desktop
