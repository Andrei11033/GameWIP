/// @file window.cpp
/// @brief Portable Window validation, lifecycle, cached state, and backend forwarding.

#include "desktop/window.h"
#include "base/checked_arithmetic.h"

#include "desktop/internal/window_platform.h"
#include "desktop/internal/desktop_test_hooks.h"
#include "desktop/internal/presentation_publication_state.h"

#include <cmath>
#include <limits>
#include <new>
#include <thread>
#include <utility>
#include <vector>

namespace GameWIP::Desktop
{
    // ------------------------------------------------------------
    // Validation and state construction
    // ------------------------------------------------------------
    namespace
    {
        using IO::Types::ErrorCode;

        [[nodiscard]] IO::Types::Status error(ErrorCode code) noexcept
        {
            return IO::makeStatus(code);
        }

        [[nodiscard]] bool validSize(Types::LogicalSize size) noexcept
        {
            constexpr auto nativeMaximum = static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
            return size.width != 0 && size.height != 0 && size.width <= nativeMaximum && size.height <= nativeMaximum;
        }

        [[nodiscard]] bool validPixelSize(Types::PixelSize size) noexcept
        {
            constexpr auto nativeMaximum = static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
            return size.width != 0 && size.height != 0 && size.width <= nativeMaximum && size.height <= nativeMaximum;
        }

        [[nodiscard]] bool validRect(const Types::LogicalRect &rect) noexcept
        {
            return validSize(rect.size);
        }

        [[nodiscard]] bool validLimits(const Types::SizeLimits &limits) noexcept
        {
            if ((limits.minimum && !validSize(*limits.minimum)) || (limits.maximum && !validSize(*limits.maximum)))
                return false;
            return !limits.minimum || !limits.maximum ||
                   (limits.minimum->width <= limits.maximum->width && limits.minimum->height <= limits.maximum->height);
        }

        [[nodiscard]] bool sizeWithin(Types::LogicalSize size, const Types::SizeLimits &limits) noexcept
        {
            return (!limits.minimum || (size.width >= limits.minimum->width && size.height >= limits.minimum->height)) &&
                   (!limits.maximum || (size.width <= limits.maximum->width && size.height <= limits.maximum->height));
        }

        [[nodiscard]] bool validRatio(const std::optional<Types::AspectRatio> &ratio) noexcept
        {
            return !ratio || (ratio->numerator != 0 && ratio->denominator != 0);
        }

        template <typename Enum> [[nodiscard]] bool validEnum(Enum value) noexcept;

        template <> bool validEnum(Types::Mode value) noexcept
        {
            return value == Types::Mode::Windowed || value == Types::Mode::BorderlessFullscreen || value == Types::Mode::ExclusiveFullscreen;
        }

        template <> bool validEnum(Types::PresentationState value) noexcept
        {
            return value == Types::PresentationState::Normal || value == Types::PresentationState::Minimized ||
                   value == Types::PresentationState::Maximized;
        }

        template <> bool validEnum(Types::DecorationMode value) noexcept
        {
            return value == Types::DecorationMode::System || value == Types::DecorationMode::Borderless || value == Types::DecorationMode::Custom;
        }

        template <> bool validEnum(Types::PlacementKind value) noexcept
        {
            return value == Types::PlacementKind::PlatformDefault || value == Types::PlacementKind::Centered ||
                   value == Types::PlacementKind::Explicit;
        }

        template <> bool validEnum(Types::CursorMode value) noexcept
        {
            return value == Types::CursorMode::Normal || value == Types::CursorMode::Hidden || value == Types::CursorMode::Confined ||
                   value == Types::CursorMode::HiddenConfined || value == Types::CursorMode::Relative;
        }

        template <> bool validEnum(Types::CursorShape value) noexcept
        {
            return static_cast<unsigned int>(value) <= static_cast<unsigned int>(Types::CursorShape::NotAllowed);
        }

        template <> bool validEnum(Types::PointerInputMode value) noexcept
        {
            return value == Types::PointerInputMode::Normal || value == Types::PointerInputMode::ClickThrough ||
                   value == Types::PointerInputMode::AcceptRegions || value == Types::PointerInputMode::IgnoreRegions ||
                   value == Types::PointerInputMode::HitMask;
        }

        template <> bool validEnum(Types::BackdropEffect value) noexcept
        {
            return value == Types::BackdropEffect::None || value == Types::BackdropEffect::Automatic || value == Types::BackdropEffect::MainWindow ||
                   value == Types::BackdropEffect::TransientWindow || value == Types::BackdropEffect::TabbedWindow;
        }

        template <> bool validEnum(Types::DpiResizePolicy value) noexcept
        {
            return value == Types::DpiResizePolicy::PreserveLogicalClientSize || value == Types::DpiResizePolicy::PreservePhysicalClientSize;
        }

        [[nodiscard]] bool validDisplayMode(const Types::Display::Mode &mode) noexcept
        {
            return validPixelSize(mode.resolution) && mode.refreshRateMillihertz != 0 && mode.bitsPerPixel != 0;
        }

        [[nodiscard]] bool validModeRequest(const Types::ModeRequest &request) noexcept
        {
            if (!validEnum(request.mode))
                return false;
            if (request.mode != Types::Mode::ExclusiveFullscreen && request.displayMode)
                return false;
            if (request.mode == Types::Mode::Windowed && request.monitor.isValid())
                return false;
            return !request.displayMode || validDisplayMode(*request.displayMode);
        }

        [[nodiscard]] IO::Types::Status validateDescription(const Types::Description &description) noexcept
        {
            // U+0000 is valid Unicode, but native Window title APIs are NUL-terminated and cannot
            // represent an embedded NUL. UTF-8 validity itself is checked by the strict conversion
            // performed at the native boundary.
            if (description.title.find('\0') != std::string::npos || !validSize(description.clientSize) || !validEnum(description.placement.kind) ||
                (description.placement.kind != Types::PlacementKind::Centered && description.placement.monitor.isValid()) ||
                !validModeRequest(description.mode) || !validEnum(description.presentation) || !validEnum(description.decoration) ||
                !validLimits(description.sizeLimits) || !sizeWithin(description.clientSize, description.sizeLimits) ||
                !validRatio(description.aspectRatio) || !validEnum(description.cursorMode) || !validEnum(description.cursorShape) ||
                !validEnum(description.pointerInputMode) || description.pointerInputMode == Types::PointerInputMode::AcceptRegions ||
                description.pointerInputMode == Types::PointerInputMode::IgnoreRegions ||
                description.pointerInputMode == Types::PointerInputMode::HitMask || !validEnum(description.backdropEffect) ||
                !validEnum(description.dpiResizePolicy) || (!description.resizable && description.controls.maximizable) ||
                !std::isfinite(description.opacity) || description.opacity < 0.0F || description.opacity > 1.0F ||
                (!description.visible && (description.requestFocus || description.presentation != Types::PresentationState::Normal)) ||
                (!description.focusable && description.requestFocus))
            {
                return error(ErrorCode::InvalidArgument);
            }
            return IO::successStatus();
        }

        [[nodiscard]] IO::Types::Status requireState(Detail::WindowState *state) noexcept
        {
            if (state == nullptr || !state->platform || !Detail::Platform::hasLiveNativeWindow(*state))
                return error(ErrorCode::NotOpen);
            if (!Detail::Platform::ownedByCurrentThread(*state))
                return error(ErrorCode::ResourceBusy);
            return IO::successStatus();
        }

        [[nodiscard]] bool owned(const Detail::WindowState *state) noexcept
        {
            return state != nullptr &&
                   (state->platform ? Detail::Platform::ownedByCurrentThread(*state) : state->ownerThread == std::this_thread::get_id());
        }

        void initializeCachedState(Detail::WindowState &state, const Types::Description &description)
        {
            state.ownerThread = std::this_thread::get_id();
            state.owner = description.owner;
            state.title = description.title;
            state.clientSize = description.clientSize;
            state.dpiResizePolicy = description.dpiResizePolicy;
            state.mode = description.mode.mode;
            state.presentation = description.presentation;
            state.decoration = description.decoration;
            state.controls = description.controls;
            state.sizeLimits = description.sizeLimits;
            state.aspectRatio = description.aspectRatio;
            state.cursorMode = description.cursorMode;
            state.cursorShape = description.cursorShape;
            state.pointerInputMode = description.pointerInputMode;
            state.backdrop = description.backdropEffect;
            state.opacity = description.opacity;
            state.visible = false;
            state.resizable = description.resizable;
            state.focusable = description.focusable;
            state.interactionEnabled = description.userInteractionEnabled;
            state.alwaysOnTop = description.alwaysOnTop;
            state.fileDropEnabled = description.fileDropEnabled;
            state.transparentFramebuffer = description.transparentFramebuffer;
            state.suppressEvents = true;
        }

        void releaseEventStorage(Detail::WindowState &state) noexcept
        {
            state.clearRetainedEvents();
        }
    } // namespace

    // ------------------------------------------------------------
    // Capabilities and lifecycle
    // ------------------------------------------------------------
    Types::CapabilitiesResult getCapabilities() noexcept
    {
        return Detail::Platform::getCapabilities();
    }

    bool supports(Types::Capability capability) noexcept
    {
        return getCapabilities().capabilities.supports(capability);
    }

    Window::Window() noexcept = default;

    Window::~Window() noexcept
    {
        if (presentationPublication_)
            presentationPublication_->reset();
        if (state_)
        {
            Detail::invalidatePointerHitMask(*state_);
            if (rendererIntegration_)
                rendererIntegration_->finishWindowLifetime();
            state_->rendererIntegration = nullptr;
            state_->presentationPublication = nullptr;
            if (!Detail::Platform::ownedByCurrentThread(*state_) && Detail::Platform::deferCleanupToOwner(state_))
                return;
            Detail::Platform::closeBestEffort(*state_);
            releaseEventStorage(*state_);
        }
    }

    IO::Types::Status Window::open(const Types::Description &description) noexcept
    {
        return open(description, Events::kDefaultQueueCapacity);
    }

    IO::Types::Status Window::open(const Types::Description &description, std::size_t eventQueueCapacity) noexcept
    {
        if (state_)
            return error(ErrorCode::AlreadyOpen);
        if (presentationPublication_)
            presentationPublication_->reset();
        if (eventQueueCapacity == 0)
            return error(ErrorCode::InvalidArgument);

        const IO::Types::Status validation = validateDescription(description);
        if (!validation.ok())
            return validation;
        if (Detail::consumeFailure(TestHooks::FailurePoint::Allocation))
            return error(ErrorCode::OutOfMemory);

        try
        {
            auto candidate = std::make_unique<Detail::WindowState>();
            Detail::WindowAccess::bindRendererIntegration(*this, *candidate);
            initializeCachedState(*candidate, description);
            if (eventQueueCapacity > candidate->internalEvents.max_size())
            {
                if (presentationPublication_)
                    presentationPublication_->reset();
                return error(ErrorCode::InvalidArgument);
            }
            candidate->internalEvents.resize(eventQueueCapacity);
            candidate->eventStorage = candidate->internalEvents;
            candidate->eventStorageKind = Types::Events::StorageKind::Internal;

            IO::Types::Status status = Detail::Platform::open(*candidate, description);
            if (!status.ok())
            {
                Detail::Platform::closeBestEffort(*candidate);
                if (presentationPublication_)
                    presentationPublication_->reset();
                return status;
            }
            candidate->suppressEvents = false;
            if (presentationPublication_)
            {
                Detail::WindowAccess::bindPresentationPublication(*this, *candidate);
                Detail::publishCachedPresentationState(*candidate);
            }
            state_ = std::move(candidate);
            return IO::successStatus();
        }
        catch (const std::bad_alloc &)
        {
            if (presentationPublication_)
                presentationPublication_->reset();
            return error(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            if (presentationPublication_)
                presentationPublication_->reset();
            return error(ErrorCode::Unknown);
        }
    }

    IO::Types::Status Window::open(const Types::Description &description, std::span<Types::Event> eventStorage) noexcept
    {
        if (state_)
            return error(ErrorCode::AlreadyOpen);
        if (presentationPublication_)
            presentationPublication_->reset();
        if (eventStorage.empty())
            return error(ErrorCode::InvalidArgument);

        const IO::Types::Status validation = validateDescription(description);
        if (!validation.ok())
            return validation;
        if (Detail::consumeFailure(TestHooks::FailurePoint::Allocation))
            return error(ErrorCode::OutOfMemory);

        try
        {
            auto candidate = std::make_unique<Detail::WindowState>();
            Detail::WindowAccess::bindRendererIntegration(*this, *candidate);
            initializeCachedState(*candidate, description);
            candidate->eventStorage = eventStorage;
            candidate->eventStorageKind = Types::Events::StorageKind::External;
            IO::Types::Status status = Detail::Platform::open(*candidate, description);
            if (!status.ok())
            {
                Detail::Platform::closeBestEffort(*candidate);
                if (presentationPublication_)
                    presentationPublication_->reset();
                return status;
            }
            candidate->suppressEvents = false;
            if (presentationPublication_)
            {
                Detail::WindowAccess::bindPresentationPublication(*this, *candidate);
                Detail::publishCachedPresentationState(*candidate);
            }
            state_ = std::move(candidate);
            return IO::successStatus();
        }
        catch (const std::bad_alloc &)
        {
            if (presentationPublication_)
                presentationPublication_->reset();
            return error(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            if (presentationPublication_)
                presentationPublication_->reset();
            return error(ErrorCode::Unknown);
        }
    }

    bool Window::isOpen() const noexcept
    {
        return state_ != nullptr && state_->platform != nullptr && Detail::Platform::hasLiveNativeWindow(*state_);
    }

    Types::LifetimeState Window::lifetimeState() const noexcept
    {
        if (!state_)
            return Types::LifetimeState::Closed;
        if (state_->nativeDestroyedPendingFinalize)
            return Types::LifetimeState::NativeDestroyedPendingFinalize;
        return isOpen() ? Types::LifetimeState::Open : Types::LifetimeState::Closed;
    }

    IO::Types::Status Window::close() noexcept
    {
        if (!isOpen() && (!state_ || !state_->nativeDestroyedPendingFinalize))
        {
            if (state_)
                releaseEventStorage(*state_);
            if (rendererIntegration_)
                rendererIntegration_->finishWindowLifetime();
            if (presentationPublication_)
                presentationPublication_->reset();
            state_.reset();
            return IO::successStatus();
        }
        if (state_ && !Detail::Platform::ownedByCurrentThread(*state_))
            return error(ErrorCode::ResourceBusy);

        Detail::invalidatePointerHitMask(*state_);
        Detail::Platform::CloseResult result = Detail::Platform::close(*state_);
        if (result.resourceClosed)
        {
            releaseEventStorage(*state_);
            if (rendererIntegration_)
                rendererIntegration_->finishWindowLifetime();
            if (presentationPublication_)
                presentationPublication_->reset();
            state_.reset();
        }
        return result.status;
    }

    // ------------------------------------------------------------
    // Identity, close requests, and events
    // ------------------------------------------------------------
    Types::WindowId Window::id() const noexcept
    {
        return state_ ? state_->id : Types::WindowId{};
    }

    Types::WindowId Window::ownerId() const noexcept
    {
        return state_ ? state_->owner : Types::WindowId{};
    }

    bool Window::ownedByCurrentThread() const noexcept
    {
        return owned(state_.get());
    }

    bool Window::supports(Types::Capability capability) const noexcept
    {
        return ::GameWIP::Desktop::supports(capability);
    }

    IO::Types::Status Window::setOwner(Types::WindowId owner) noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        if (!status.ok())
            return status;
        if (owner == state_->id)
            return error(ErrorCode::InvalidArgument);
        return Detail::Platform::setOwner(*state_, owner);
    }

    bool Window::hasCloseRequest() const noexcept
    {
        return state_ && state_->closeRequested;
    }

    IO::Types::Status Window::requestClose() noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        if (!status.ok())
            return status;
        static_cast<void>(Detail::requestClose(*state_, Types::Events::CloseRequestSource::Programmatic));
        return IO::successStatus();
    }

    IO::Types::Status Window::clearCloseRequest() noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        if (!status.ok())
            return status;
        state_->closeRequested = false;
        return IO::successStatus();
    }

    bool Window::popEvent(Types::Event &outEvent) noexcept
    {
        if (!owned(state_.get()) || state_->eventCount == 0)
            return false;
        Types::Event &slot = state_->eventStorage[state_->eventHead];
        outEvent = std::move(slot);
        slot = {};
        state_->eventHead = (state_->eventHead + 1) % state_->eventStorage.size();
        --state_->eventCount;
        if (state_->eventCount == 0)
            state_->eventHead = 0;
        return true;
    }

    std::size_t Window::popEvents(std::span<Types::Event> destination) noexcept
    {
        std::size_t count = 0;
        while (count < destination.size() && popEvent(destination[count]))
            ++count;
        return count;
    }

    void Window::clearEvents() noexcept
    {
        if (!owned(state_.get()))
            return;
        for (std::size_t index = 0; index < state_->eventCount; ++index)
            state_->eventStorage[(state_->eventHead + index) % state_->eventStorage.size()] = {};
        state_->eventHead = 0;
        state_->eventCount = 0;
    }

    Types::Events::QueueInfo Window::eventQueueInfo() const noexcept
    {
        if (!state_)
            return {};
        return {
            .storage = state_->eventStorageKind,
            .capacity = state_->eventStorage.size(),
            .pendingEvents = state_->eventCount,
            .droppedEvents = state_->droppedEvents};
    }

    void Window::clearDroppedEventCount() noexcept
    {
        if (owned(state_.get()))
            state_->droppedEvents = 0;
    }

    IO::Types::Status Window::wakeEventWait() const noexcept
    {
        if (!isOpen())
            return error(ErrorCode::NotOpen);
        return Detail::Platform::wakeEventWait(*state_);
    }

    // ------------------------------------------------------------
    // Cached state
    // ------------------------------------------------------------
    std::string_view Window::title() const noexcept
    {
        return state_ ? std::string_view(state_->title) : std::string_view{};
    }
    Types::LogicalSize Window::clientSize() const noexcept
    {
        return presentationPublication_ ? presentationPublication_->clientSize() : state_ ? state_->clientSize : Types::LogicalSize{};
    }
    Types::PixelSize Window::framebufferSize() const noexcept
    {
        return presentationPublication_ ? presentationPublication_->framebufferSize() : state_ ? state_->framebufferSize : Types::PixelSize{};
    }
    Types::ScreenPosition Window::clientPosition() const noexcept
    {
        return state_ ? state_->clientPosition : Types::ScreenPosition{};
    }
    Types::ScreenRect Window::frameRect() const noexcept
    {
        return state_ ? state_->frameRect : Types::ScreenRect{};
    }
    Types::Insets Window::frameInsets() const noexcept
    {
        return state_ ? state_->frameInsets : Types::Insets{};
    }
    Types::ContentScale Window::contentScale() const noexcept
    {
        return presentationPublication_ ? presentationPublication_->contentScale() : state_ ? state_->contentScale : Types::ContentScale{};
    }
    Types::Dpi Window::effectiveDpi() const noexcept
    {
        return presentationPublication_ ? presentationPublication_->dpi() : state_ ? state_->dpi : Types::Dpi{};
    }
    Types::DpiResizePolicy Window::dpiResizePolicy() const noexcept
    {
        return state_ ? state_->dpiResizePolicy : Types::DpiResizePolicy::PreserveLogicalClientSize;
    }
    Types::Display::MonitorId Window::currentMonitor() const noexcept
    {
        return presentationPublication_ ? presentationPublication_->monitor() : state_ ? state_->monitor : Types::Display::MonitorId{};
    }
    Types::Mode Window::mode() const noexcept
    {
        return state_ ? state_->mode : Types::Mode::Windowed;
    }
    Types::FullscreenInfo Window::fullscreenInfo() const noexcept
    {
        return state_ ? state_->fullscreen : Types::FullscreenInfo{};
    }
    Types::PresentationState Window::presentationState() const noexcept
    {
        return presentationPublication_ ? presentationPublication_->presentation() : state_ ? state_->presentation : Types::PresentationState::Normal;
    }
    Types::DecorationMode Window::decorationMode() const noexcept
    {
        return state_ ? state_->decoration : Types::DecorationMode::System;
    }
    Types::Controls Window::controls() const noexcept
    {
        return state_ ? state_->controls : Types::Controls{};
    }
    Types::SizeLimits Window::sizeLimits() const noexcept
    {
        return state_ ? state_->sizeLimits : Types::SizeLimits{};
    }
    std::optional<Types::AspectRatio> Window::aspectRatio() const noexcept
    {
        return state_ ? state_->aspectRatio : std::nullopt;
    }
    Types::CursorMode Window::cursorMode() const noexcept
    {
        return state_ ? state_->cursorMode : Types::CursorMode::Normal;
    }
    Types::CursorShape Window::cursorShape() const noexcept
    {
        return state_ ? state_->cursorShape : Types::CursorShape::Arrow;
    }
    Types::PointerInputMode Window::pointerInputMode() const noexcept
    {
        return state_ ? state_->pointerInputMode : Types::PointerInputMode::Normal;
    }
    std::size_t Window::pointerInputRegionCount() const noexcept
    {
        return state_ ? state_->pointerInputRegions.size() : 0;
    }
    Types::BackdropEffect Window::backdropEffect() const noexcept
    {
        return state_ ? state_->backdrop : Types::BackdropEffect::None;
    }
    float Window::opacity() const noexcept
    {
        return state_ ? state_->opacity : 1.0F;
    }
    bool Window::visible() const noexcept
    {
        return presentationPublication_ ? presentationPublication_->visible() : state_ && state_->visible;
    }
    bool Window::focused() const noexcept
    {
        return state_ && state_->focused;
    }
    bool Window::interactiveMoveResizeActive() const noexcept
    {
        return presentationPublication_ ? presentationPublication_->interactiveMoveResizeActive() : state_ && state_->interactiveMoveResizeActive;
    }
    bool Window::minimized() const noexcept
    {
        return presentationState() == Types::PresentationState::Minimized;
    }
    bool Window::maximized() const noexcept
    {
        return presentationState() == Types::PresentationState::Maximized;
    }
    bool Window::occluded() const noexcept
    {
        if (presentationPublication_)
            return presentationPublication_->occluded();
        const Detail::RendererIntegrationState *renderer = Detail::WindowAccess::rendererIntegration(*this);
        return state_ && renderer != nullptr && renderer->occluded;
    }
    bool Window::cursorInside() const noexcept
    {
        return state_ && state_->cursorInside;
    }
    bool Window::resizable() const noexcept
    {
        return state_ && state_->resizable;
    }
    bool Window::focusable() const noexcept
    {
        return state_ && state_->focusable;
    }
    bool Window::userInteractionEnabled() const noexcept
    {
        return state_ && state_->interactionEnabled;
    }
    bool Window::alwaysOnTop() const noexcept
    {
        return state_ && state_->alwaysOnTop;
    }
    bool Window::fileDropEnabled() const noexcept
    {
        return state_ && state_->fileDropEnabled;
    }
    bool Window::transparentFramebuffer() const noexcept
    {
        return state_ && state_->transparentFramebuffer;
    }

    // ------------------------------------------------------------
    // Geometry and content mutations
    // ------------------------------------------------------------
    IO::Types::Status Window::setTitle(std::string_view utf8Title) noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        if (!status.ok())
            return status;
        if (utf8Title.find('\0') != std::string_view::npos)
            return error(ErrorCode::InvalidArgument);
        return Detail::Platform::setTitle(*state_, utf8Title);
    }

    IO::Types::Status Window::setIcon(std::span<const Types::IconImageView> images) noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        if (!status.ok())
            return status;
        if (images.empty())
            return error(ErrorCode::InvalidArgument);

        for (const Types::IconImageView &image : images)
        {
            if (!validPixelSize(image.size))
                return error(ErrorCode::InvalidArgument);
            constexpr std::size_t channels = 4;
            const std::size_t width = static_cast<std::size_t>(image.size.width);
            const std::size_t height = static_cast<std::size_t>(image.size.height);
            if (width != image.size.width || height != image.size.height || GameWIP::Base::wouldMultiplyOverflow(width, height))
                return error(ErrorCode::InvalidArgument);
            const std::size_t pixels = width * height;
            if (GameWIP::Base::wouldMultiplyOverflow(pixels, channels))
                return error(ErrorCode::InvalidArgument);
            if (image.rgba8.size() != pixels * channels)
                return error(ErrorCode::InvalidArgument);
        }
        return Detail::Platform::setIcon(*state_, images);
    }

    IO::Types::Status Window::clearIcon() noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        return status.ok() ? Detail::Platform::clearIcon(*state_) : status;
    }

    IO::Types::Status Window::setClientSize(Types::LogicalSize size) noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        if (!status.ok())
            return status;
        if (state_->mode != Types::Mode::Windowed)
            return error(ErrorCode::ResourceBusy);
        if (!validSize(size) || !sizeWithin(size, state_->sizeLimits))
            return error(ErrorCode::InvalidArgument);
        return Detail::Platform::setClientSize(*state_, size);
    }

    IO::Types::Status Window::setClientPosition(Types::ScreenPosition position) noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        if (!status.ok())
            return status;
        return state_->mode == Types::Mode::Windowed ? Detail::Platform::setClientPosition(*state_, position) : error(ErrorCode::ResourceBusy);
    }

    IO::Types::Status Window::setClientRect(Types::ScreenPosition position, Types::LogicalSize size) noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        if (!status.ok())
            return status;
        if (state_->mode != Types::Mode::Windowed)
            return error(ErrorCode::ResourceBusy);
        if (!validSize(size) || !sizeWithin(size, state_->sizeLimits))
            return error(ErrorCode::InvalidArgument);
        return Detail::Platform::setClientRect(*state_, position, size);
    }

    IO::Types::Status Window::centerOn(Types::Display::MonitorId monitor) noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        if (!status.ok())
            return status;
        return state_->mode == Types::Mode::Windowed ? Detail::Platform::centerOn(*state_, monitor) : error(ErrorCode::ResourceBusy);
    }

    IO::Types::Status Window::setSizeLimits(const Types::SizeLimits &limits) noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        if (!status.ok())
            return status;
        if (state_->mode != Types::Mode::Windowed)
            return error(ErrorCode::ResourceBusy);
        if (!validLimits(limits))
            return error(ErrorCode::InvalidArgument);
        return Detail::Platform::setSizeLimits(*state_, limits);
    }

    IO::Types::Status Window::setAspectRatio(std::optional<Types::AspectRatio> ratio) noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        if (!status.ok())
            return status;
        if (state_->mode != Types::Mode::Windowed)
            return error(ErrorCode::ResourceBusy);
        if (!validRatio(ratio))
            return error(ErrorCode::InvalidArgument);
        return Detail::Platform::setAspectRatio(*state_, ratio);
    }

    Types::ScreenPositionResult Window::clientToScreen(Types::LogicalPosition position) const noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        return status.ok() ? Detail::Platform::clientToScreen(*state_, position) : Types::ScreenPositionResult{status, {}};
    }

    Types::LogicalPositionResult Window::screenToClient(Types::ScreenPosition position) const noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        return status.ok() ? Detail::Platform::screenToClient(*state_, position) : Types::LogicalPositionResult{status, {}};
    }

    IO::Types::Status Window::setDpiResizePolicy(Types::DpiResizePolicy policy) noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        if (!status.ok())
            return status;
        if (!validEnum(policy))
            return error(ErrorCode::InvalidArgument);
        state_->dpiResizePolicy = policy;
        return IO::successStatus();
    }

    // ------------------------------------------------------------
    // Presentation and interaction
    // ------------------------------------------------------------
    IO::Types::Status Window::show() noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        return status.ok() ? Detail::Platform::show(*state_) : status;
    }
    IO::Types::Status Window::hide() noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        return status.ok() ? Detail::Platform::hide(*state_) : status;
    }
    IO::Types::Status Window::requestFocus() noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        return status.ok() ? Detail::Platform::requestFocus(*state_) : status;
    }
    IO::Types::Status Window::requestAttention() noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        return status.ok() ? Detail::Platform::requestAttention(*state_) : status;
    }
    IO::Types::Status Window::minimize() noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        return status.ok() ? Detail::Platform::minimize(*state_) : status;
    }
    IO::Types::Status Window::maximize() noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        return status.ok() ? Detail::Platform::maximize(*state_) : status;
    }
    IO::Types::Status Window::restore() noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        return status.ok() ? Detail::Platform::restore(*state_) : status;
    }

    IO::Types::Status Window::setMode(const Types::ModeRequest &request) noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        if (!status.ok())
            return status;
        if (!validModeRequest(request))
            return error(ErrorCode::InvalidArgument);
        return Detail::Platform::setMode(*state_, request);
    }

    IO::Types::Status Window::setResizable(bool resizable) noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        if (!status.ok())
            return status;
        if (!resizable && state_->controls.maximizable)
            return error(ErrorCode::InvalidArgument);
        return Detail::Platform::setResizable(*state_, resizable);
    }

    IO::Types::Status Window::setDecorationMode(Types::DecorationMode mode) noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        if (!status.ok())
            return status;
        if (!validEnum(mode))
            return error(ErrorCode::InvalidArgument);
        return Detail::Platform::setDecorationMode(*state_, mode);
    }

    IO::Types::Status Window::setControls(const Types::Controls &controls) noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        if (!status.ok())
            return status;
        if (controls.maximizable && !state_->resizable)
            return error(ErrorCode::InvalidArgument);
        return Detail::Platform::setControls(*state_, controls);
    }

    IO::Types::Status Window::setFocusable(bool focusable) noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        return status.ok() ? Detail::Platform::setFocusable(*state_, focusable) : status;
    }
    IO::Types::Status Window::setUserInteractionEnabled(bool enabled) noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        return status.ok() ? Detail::Platform::setUserInteractionEnabled(*state_, enabled) : status;
    }
    IO::Types::Status Window::setAlwaysOnTop(bool alwaysOnTop) noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        return status.ok() ? Detail::Platform::setAlwaysOnTop(*state_, alwaysOnTop) : status;
    }

    IO::Types::Status Window::setOpacity(float opacity) noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        if (!status.ok())
            return status;
        if (!std::isfinite(opacity) || opacity < 0.0F || opacity > 1.0F)
            return error(ErrorCode::InvalidArgument);
        return Detail::Platform::setOpacity(*state_, opacity);
    }

    IO::Types::Status Window::setBackdropEffect(Types::BackdropEffect effect) noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        if (!status.ok())
            return status;
        if (!validEnum(effect))
            return error(ErrorCode::InvalidArgument);
        return Detail::Platform::setBackdropEffect(*state_, effect);
    }

    IO::Types::Status Window::setFileDropEnabled(bool enabled) noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        return status.ok() ? Detail::Platform::setFileDropEnabled(*state_, enabled) : status;
    }

    IO::Types::Status Window::setCustomChromeLayout(const Types::CustomChromeLayout &layout) noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        if (!status.ok())
            return status;

        const std::size_t regionCount = layout.draggableRegions.size() + static_cast<std::size_t>(layout.systemMenuRegion.has_value()) +
                                        static_cast<std::size_t>(layout.minimizeButtonRegion.has_value()) +
                                        static_cast<std::size_t>(layout.maximizeButtonRegion.has_value()) +
                                        static_cast<std::size_t>(layout.closeButtonRegion.has_value());
        const Types::CapabilitiesResult capabilities = getCapabilities();
        if (!capabilities.status.ok() || regionCount > capabilities.capabilities.maximumCustomChromeRegions)
            return capabilities.status.ok() ? error(ErrorCode::InvalidArgument) : capabilities.status;
        for (const Types::LogicalRect &rect : layout.draggableRegions)
        {
            if (!validRect(rect))
                return error(ErrorCode::InvalidArgument);
        }
        if ((layout.systemMenuRegion && !validRect(*layout.systemMenuRegion)) ||
            (layout.minimizeButtonRegion && !validRect(*layout.minimizeButtonRegion)) ||
            (layout.maximizeButtonRegion && !validRect(*layout.maximizeButtonRegion)) ||
            (layout.closeButtonRegion && !validRect(*layout.closeButtonRegion)))
        {
            return error(ErrorCode::InvalidArgument);
        }

        try
        {
            if (Detail::consumeFailure(TestHooks::FailurePoint::RegionCopy))
                return error(ErrorCode::OutOfMemory);
            std::vector<Types::LogicalRect> copied(layout.draggableRegions.begin(), layout.draggableRegions.end());
            std::vector<Types::LogicalRect> previous = std::move(state_->draggableRegions);
            const auto previousSystem = state_->systemMenuRegion;
            const auto previousMinimize = state_->minimizeButtonRegion;
            const auto previousMaximize = state_->maximizeButtonRegion;
            const auto previousClose = state_->closeButtonRegion;
            state_->draggableRegions = std::move(copied);
            state_->systemMenuRegion = layout.systemMenuRegion;
            state_->minimizeButtonRegion = layout.minimizeButtonRegion;
            state_->maximizeButtonRegion = layout.maximizeButtonRegion;
            state_->closeButtonRegion = layout.closeButtonRegion;
            status = Detail::Platform::setCustomChromeLayout(*state_);
            if (!status.ok())
            {
                state_->draggableRegions = std::move(previous);
                state_->systemMenuRegion = previousSystem;
                state_->minimizeButtonRegion = previousMinimize;
                state_->maximizeButtonRegion = previousMaximize;
                state_->closeButtonRegion = previousClose;
            }
            return status;
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

    IO::Types::Status Window::clearCustomChromeLayout() noexcept
    {
        return setCustomChromeLayout({});
    }

    IO::Types::Status Window::setPointerInputLayout(const Types::PointerInputLayout &layout) noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        if (!status.ok())
            return status;
        if (!validEnum(layout.mode))
            return error(ErrorCode::InvalidArgument);

        const bool regionMode = layout.mode == Types::PointerInputMode::AcceptRegions || layout.mode == Types::PointerInputMode::IgnoreRegions;
        if (regionMode == layout.regions.empty())
            return error(ErrorCode::InvalidArgument);
        const Types::CapabilitiesResult capabilities = getCapabilities();
        if (!capabilities.status.ok())
            return capabilities.status;
        if (layout.mode == Types::PointerInputMode::ClickThrough && !capabilities.capabilities.supports(Types::Capability::PointerClickThrough))
            return error(ErrorCode::Unsupported);
        if (regionMode && !capabilities.capabilities.supports(Types::Capability::PointerRegions))
            return error(ErrorCode::Unsupported);
        if (layout.mode == Types::PointerInputMode::HitMask && !capabilities.capabilities.supports(Types::Capability::PointerHitMask))
            return error(ErrorCode::Unsupported);
        if (layout.regions.size() > capabilities.capabilities.maximumPointerInputRegions)
            return error(ErrorCode::InvalidArgument);
        for (const Types::LogicalRect &rect : layout.regions)
        {
            if (!validRect(rect))
                return error(ErrorCode::InvalidArgument);
        }

        try
        {
            if (Detail::consumeFailure(TestHooks::FailurePoint::RegionCopy))
                return error(ErrorCode::OutOfMemory);
            std::vector<Types::LogicalRect> copied(layout.regions.begin(), layout.regions.end());
            std::vector<Types::LogicalRect> previous = std::move(state_->pointerInputRegions);
            const Types::PointerInputMode previousMode = state_->pointerInputMode;
            state_->pointerInputRegions = std::move(copied);
            state_->pointerInputMode = layout.mode;
            status = Detail::Platform::setPointerInputLayout(*state_);
            if (!status.ok())
            {
                state_->pointerInputRegions = std::move(previous);
                state_->pointerInputMode = previousMode;
                static_cast<void>(Detail::Platform::setPointerInputLayout(*state_));
            }
            return status;
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

    // ------------------------------------------------------------
    // Cursor controls
    // ------------------------------------------------------------
    IO::Types::Status Window::setCursorMode(Types::CursorMode mode) noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        if (!status.ok())
            return status;
        if (!validEnum(mode))
            return error(ErrorCode::InvalidArgument);
        return Detail::Platform::setCursorMode(*state_, mode);
    }

    IO::Types::Status Window::setCursorShape(Types::CursorShape shape) noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        if (!status.ok())
            return status;
        if (!validEnum(shape))
            return error(ErrorCode::InvalidArgument);
        return Detail::Platform::setCursorShape(*state_, shape);
    }

    IO::Types::Status Window::setCursorPosition(Types::LogicalPosition position) noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        return status.ok() ? Detail::Platform::setCursorPosition(*state_, position) : status;
    }

    Types::LogicalPositionResult Window::cursorPosition() const noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        return status.ok() ? Detail::Platform::cursorPosition(*state_) : Types::LogicalPositionResult{status, {}};
    }
} // namespace GameWIP::Desktop
