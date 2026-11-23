/// @file window.cpp
/// @brief Portable Window validation, lifecycle, cached state, and backend forwarding.

#include "desktop/window.h"
#include "desktop/internal/accessibility_state.h"
#include "desktop/internal/desktop_test_hooks.h"
#include "desktop/internal/helpers/window_validation.h"
#include "desktop/internal/presentation_publication_state.h"
#include "desktop/platform/window.h"

#include <cmath>
#include <memory>
#include <new>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>

namespace GameWIP::Desktop
{
    /// ------------------------------------------------------------
    /// Validation and state construction
    /// ------------------------------------------------------------

    namespace
    {
        using Internal::Helpers::error;
        using Internal::Helpers::requireState;
        using Internal::Helpers::sizeWithin;
        using Internal::Helpers::validDisplayMode;
        using Internal::Helpers::validEnum;
        using Internal::Helpers::validLimits;
        using Internal::Helpers::validModeRequest;
        using Internal::Helpers::validRatio;
        using Internal::Helpers::validSize;
        using IO::Types::ErrorCode;

        /// @brief Validates a complete open description before native state allocation.
        [[nodiscard]] IO::Types::Status validateDescription(const Types::Description &description) noexcept
        {
            // U+0000 is valid Unicode, but native Window title APIs are NUL-terminated and cannot
            // represent an embedded NUL. UTF-8 validity itself is checked by the strict conversion
            // performed at the native boundary.
            if(description.title.contains('\0') || !validSize(description.windowed.clientSize) || !validEnum(description.windowed.placement.kind) ||
               (description.windowed.placement.kind != Types::PlacementKind::Centered && description.windowed.placement.monitor) ||
               !validModeRequest(description.initialWindowMode) || !validEnum(description.presentation) || !validEnum(description.decoration) ||
               !validLimits(description.resizing.limits) || !sizeWithin(description.windowed.clientSize, description.resizing.limits) ||
               !validRatio(description.resizing.aspectRatio) || !validEnum(description.cursorMode) || !validEnum(description.cursorShape) ||
               !validEnum(description.pointerInputMode) || description.pointerInputMode == Types::PointerInputMode::AcceptRegions ||
               description.pointerInputMode == Types::PointerInputMode::IgnoreRegions ||
               description.pointerInputMode == Types::PointerInputMode::HitMask || !validEnum(description.backdropEffect) ||
               !validEnum(description.dpiResizePolicy) || (!description.resizing.enabled && description.resizing.controls.maximizable) ||
               !std::isfinite(description.opacity) || description.opacity < 0.0F || description.opacity > 1.0F ||
               (!description.activation.visible &&
                (description.activation.requestFocus || description.presentation != Types::PresentationState::Normal)) ||
               (!description.activation.focusable && description.activation.requestFocus))
            {
                return error(ErrorCode::InvalidArgument);
            }
            return IO::successStatus();
        }

        /// @brief Returns whether the current thread owns either live or deferred Window state.
        [[nodiscard]] bool owned(const Internal::WindowState *state) noexcept
        {
            return state != nullptr && (state->platform ? Platform::ownedByCurrentThread(*state) : state->ownerThread == std::this_thread::get_id());
        }

        /// @brief Copies the portable description into state before native creation begins.
        void initializeCachedState(Internal::WindowState &state, const Types::Description &description)
        {
            state.ownerThread = std::this_thread::get_id();
            state.owner = description.owner.value_or(Types::WindowId::invalid());
            state.title = description.title;
            state.clientSize = description.windowed.clientSize;
            state.dpiResizePolicy = description.dpiResizePolicy;
            state.mode = std::visit(
                [](const auto &request) noexcept
                {
                    using Request = std::decay_t<decltype(request)>;
                    if constexpr(std::is_same_v<Request, Types::WindowedModeRequest>)
                    {
                        return Types::WindowMode::Windowed;
                    }
                    else if constexpr(std::is_same_v<Request, Types::BorderlessFullscreenRequest>)
                    {
                        return Types::WindowMode::BorderlessFullscreen;
                    }
                    else
                    {
                        return Types::WindowMode::ExclusiveFullscreen;
                    }
                },
                description.initialWindowMode);
            state.presentation = description.presentation;
            state.decoration = description.decoration;
            state.controls = description.resizing.controls;
            state.sizeLimits = description.resizing.limits;
            state.aspectRatio = description.resizing.aspectRatio;
            state.cursorMode = description.cursorMode;
            state.cursorShape = description.cursorShape;
            state.pointerInputMode = description.pointerInputMode;
            state.backdrop = description.backdropEffect;
            state.opacity = description.opacity;
            state.visible = false;
            state.resizable = description.resizing.enabled;
            state.focusable = description.activation.focusable;
            state.interactionEnabled = description.userInteractionEnabled;
            state.alwaysOnTop = description.alwaysOnTop;
            state.fileDropEnabled = description.fileDropEnabled;
            state.transparentFramebuffer = description.transparentFramebuffer;
            state.suppressEvents = true;
        }

        /// @brief Releases retained event payloads and caller-owned queue storage references.
        void releaseEventStorage(Internal::WindowState &state) noexcept
        {
            state.clearRetainedEvents();
        }
    } // namespace

    /// ------------------------------------------------------------
    /// Capabilities and lifecycle
    /// ------------------------------------------------------------

    Types::Result<Types::Capabilities> getCapabilities() noexcept
    {
        return Platform::getCapabilities();
    }

    bool supports(Types::Capability capability) noexcept
    {
        const auto result = getCapabilities();
        return result.ok() && result->supports(capability);
    }

    Window::Window() noexcept = default;

    Window::~Window() noexcept
    {
        // Detach publishers and callbacks before native teardown.
        if(presentationPublication_)
        {
            presentationPublication_->reset();
        }

        if(state_)
        {
            Internal::closeAccessibility(*state_);
            state_->deferredAccessibilityOwner = std::move(accessibilityState_);
            Internal::invalidatePointerHitMask(*state_);

            if(rendererIntegration_)
            {
                rendererIntegration_->finishWindowLifetime();
            }
            state_->rendererIntegration = nullptr;
            state_->presentationPublication = nullptr;

            // Defer native cleanup if teardown must run on the owner thread.
            if(!Platform::ownedByCurrentThread(*state_) && Platform::deferCleanupToOwner(state_))
            {
                return;
            }

            // Release native resources and retained event payloads.
            Platform::closeBestEffort(*state_);
            releaseEventStorage(*state_);
        }
    }

    IO::Types::Status Window::open(const Types::Description &description) noexcept
    {
        return open(description, Events::owned());
    }

    IO::Types::Status Window::open(const Types::Description &description, Types::Events::QueueConfiguration queue) noexcept
    {
        // Reject invalid lifecycle and queue configurations.
        if(state_)
        {
            return error(ErrorCode::AlreadyOpen);
        }

        if(presentationPublication_)
        {
            presentationPublication_->reset();
        }
        const auto *ownedQueue = std::get_if<Types::Events::QueueConfiguration::Owned>(&queue.storage);
        const auto *borrowedQueue = std::get_if<Types::Events::QueueConfiguration::Borrowed>(&queue.storage);

        if((ownedQueue != nullptr && ownedQueue->capacity == 0) || (borrowedQueue != nullptr && borrowedQueue->storage.empty()))
        {
            return error(ErrorCode::InvalidArgument);
        }

        // Validate before allocating candidate state.
        const IO::Types::Status validation = validateDescription(description);
        if(!validation.ok())
        {
            return validation;
        }

        if(Internal::consumeFailure(Internal::TestHooks::FailurePoint::Allocation))
        {
            return error(ErrorCode::OutOfMemory);
        }

        try
        {
            // Build candidate state and configure its event storage.
            auto candidate = std::make_unique<Internal::WindowState>();
            Internal::WindowAccess::bindRendererIntegration(*this, *candidate);
            initializeCachedState(*candidate, description);

            if(ownedQueue != nullptr && ownedQueue->capacity > candidate->internalEvents.max_size())
            {
                if(presentationPublication_)
                {
                    presentationPublication_->reset();
                }

                return error(ErrorCode::InvalidArgument);
            }

            if(ownedQueue != nullptr)
            {
                candidate->internalEvents.resize(ownedQueue->capacity);
                candidate->eventStorage = candidate->internalEvents;
                candidate->eventStorageKind = Types::Events::StorageKind::Internal;
            }
            else
            {
                candidate->eventStorage = borrowedQueue->storage;
                candidate->eventStorageKind = Types::Events::StorageKind::External;
            }

            // Open the native window before publishing candidate state.
            IO::Types::Status status = Platform::open(*candidate, description);
            if(!status.ok())
            {
                Platform::closeBestEffort(*candidate);

                if(presentationPublication_)
                {
                    presentationPublication_->reset();
                }

                return status;
            }

            // Publish cached state only after native creation succeeds.
            candidate->suppressEvents = false;

            if(presentationPublication_)
            {
                Internal::WindowAccess::bindPresentationPublication(*this, *candidate);
                Internal::publishCachedPresentationState(*candidate);
            }

            state_ = std::move(candidate);
            return IO::successStatus();
        }
        catch(const std::bad_alloc &)
        {
            if(presentationPublication_)
            {
                presentationPublication_->reset();
            }

            return error(ErrorCode::OutOfMemory);
        }
        catch(...)
        {
            if(presentationPublication_)
            {
                presentationPublication_->reset();
            }

            return error(ErrorCode::Unknown);
        }
    }

    bool Window::isOpen() const noexcept
    {
        return state_ != nullptr && state_->platform != nullptr && Platform::hasLiveNativeWindow(*state_);
    }

    Types::LifetimeState Window::lifetimeState() const noexcept
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

    IO::Types::Status Window::close() noexcept
    {
        // Release cached resources when no native close is pending.
        if(!isOpen() && (!state_ || !state_->nativeDestroyedPendingFinalize))
        {
            if(state_)
            {
                Internal::closeAccessibility(*state_);
                releaseEventStorage(*state_);
            }

            if(rendererIntegration_)
            {
                rendererIntegration_->finishWindowLifetime();
            }

            if(presentationPublication_)
            {
                presentationPublication_->reset();
            }
            state_.reset();
            return IO::successStatus();
        }

        // Native teardown must run on the Window owner thread.
        if(state_ && !Platform::ownedByCurrentThread(*state_))
        {
            return error(ErrorCode::ResourceBusy);
        }

        // Invalidate transient input state before closing the native handle.
        Internal::invalidatePointerHitMask(*state_);
        Platform::CloseResult result = Platform::close(*state_);

        if(result.resourceClosed)
        {
            // Release cached resources only after native closure succeeds.
            Internal::closeAccessibility(*state_);
            releaseEventStorage(*state_);

            if(rendererIntegration_)
            {
                rendererIntegration_->finishWindowLifetime();
            }

            if(presentationPublication_)
            {
                presentationPublication_->reset();
            }
            state_.reset();
        }

        return result.status;
    }

    /// ------------------------------------------------------------
    /// Identity, close requests, and events
    /// ------------------------------------------------------------

    Types::WindowId Window::id() const noexcept
    {
        return state_ ? state_->id : Types::WindowId::invalid();
    }

    std::optional<Types::WindowId> Window::ownerId() const noexcept
    {
        if(!state_ || !state_->owner.isValid())
        {
            return std::nullopt;
        }
        return state_->owner;
    }

    bool Window::ownedByCurrentThread() const noexcept
    {
        return owned(state_.get());
    }

    bool Window::supports(Types::Capability capability) const noexcept
    {
        return ::GameWIP::Desktop::supports(capability);
    }

    IO::Types::Status Window::setOwner(std::optional<Types::WindowId> owner) noexcept
    {
        IO::Types::Status status = requireState(state_.get());

        if(!status.ok())
        {
            return status;
        }

        if(owner && *owner == state_->id)
        {
            return error(ErrorCode::InvalidArgument);
        }
        return Platform::setOwner(*state_, owner.value_or(Types::WindowId::invalid()));
    }

    bool Window::hasCloseRequest() const noexcept
    {
        return state_ && state_->closeRequested;
    }

    IO::Types::Status Window::requestClose() noexcept
    {
        IO::Types::Status status = requireState(state_.get());

        if(!status.ok())
        {
            return status;
        }

        static_cast<void>(Internal::requestClose(*state_, Types::Events::CloseRequestSource::Programmatic));
        return IO::successStatus();
    }

    IO::Types::Status Window::clearCloseRequest() noexcept
    {
        IO::Types::Status status = requireState(state_.get());

        if(!status.ok())
        {
            return status;
        }

        state_->closeRequested = false;
        return IO::successStatus();
    }

    bool Window::popEvent(Types::Event &outEvent) noexcept
    {
        if(!owned(state_.get()) || state_->eventCount == 0)
        {
            return false;
        }
        Types::Event &slot = state_->eventStorage[state_->eventHead];
        outEvent = std::move(slot);
        slot = {};
        state_->eventHead = (state_->eventHead + 1) % state_->eventStorage.size();
        --state_->eventCount;

        if(state_->eventCount == 0)
        {
            state_->eventHead = 0;
        }

        return true;
    }

    std::size_t Window::popEvents(std::span<Types::Event> destination) noexcept
    {
        std::size_t count = 0;

        while(count < destination.size() && popEvent(destination[count]))
        {
            ++count;
        }

        return count;
    }

    void Window::clearEvents() noexcept
    {
        if(!owned(state_.get()))
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

    Types::Events::QueueInfo Window::eventQueueInfo() const noexcept
    {
        if(!state_)
        {
            return {};
        }

        return {
            .storage = state_->eventStorageKind,
            .capacity = state_->eventStorage.size(),
            .pendingEvents = state_->eventCount,
            .droppedEvents = state_->droppedEvents};
    }

    void Window::clearDroppedEventCount() noexcept
    {
        if(owned(state_.get()))
        {
            state_->droppedEvents = 0;
        }
    }

    IO::Types::Status Window::wakeEventWait() const noexcept
    {
        if(!isOpen())
        {
            return error(ErrorCode::NotOpen);
        }

        return Platform::wakeEventWait(*state_);
    }

} // namespace GameWIP::Desktop
