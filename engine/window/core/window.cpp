/// @file window.cpp
/// @brief Portable Window validation, fixed event queue, cached state, and backend forwarding.

#include "window/window.h"

#include "window/internal/window_platform.h"
#include "window/internal/window_test_hooks.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <thread>
#include <type_traits>
#include <utility>

namespace GameWIP::Window::Detail
{
    namespace
    {
        [[nodiscard]] std::size_t physicalIndex(const WindowState &state, std::size_t logicalIndex) noexcept
        {
            return (state.eventHead + logicalIndex) % state.eventStorage.size();
        }

        [[nodiscard]] bool sameCoalescibleType(const Types::EventData &left, const Types::EventData &right) noexcept
        {
            return left.index() == right.index() && isCoalescible(left);
        }

        void discardAt(WindowState &state, std::size_t index) noexcept
        {
            for (std::size_t current = index; current + 1 < state.eventCount; ++current)
            {
                state.eventStorage[physicalIndex(state, current)] = std::move(state.eventStorage[physicalIndex(state, current + 1)]);
            }
            --state.eventCount;
            ++state.droppedEvents;
        }
    } // namespace

    bool isCoalescible(const Types::EventData &data) noexcept
    {
        return std::holds_alternative<Types::MovedEvent>(data) || std::holds_alternative<Types::ClientSizeChangedEvent>(data) ||
               std::holds_alternative<Types::FramebufferSizeChangedEvent>(data) || std::holds_alternative<Types::ContentScaleChangedEvent>(data);
    }

    EnqueueResult enqueueEvent(WindowState &state, Types::EventData data) noexcept
    {
        if (state.suppressEvents)
        {
            return EnqueueResult::Coalesced;
        }

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
                {
                    break;
                }
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

    EnqueueResult requestClose(WindowState &state, Types::CloseRequestSource source) noexcept
    {
        if (state.closeRequested)
        {
            return EnqueueResult::Coalesced;
        }
        state.closeRequested = true;
        return enqueueEvent(state, Types::CloseRequestedEvent{source});
    }
} // namespace GameWIP::Window::Detail

namespace GameWIP::Window
{
    namespace
    {
        using IO::Types::ErrorCode;

        [[nodiscard]] IO::Types::Status error(ErrorCode code) noexcept
        {
            return IO::makeStatus(code);
        }

        [[nodiscard]] bool validUtf8(std::string_view text) noexcept
        {
            const auto *bytes = reinterpret_cast<const unsigned char *>(text.data());
            std::size_t index = 0;
            while (index < text.size())
            {
                const unsigned char first = bytes[index++];
                if (first <= 0x7F)
                {
                    if (first == 0)
                    {
                        return false;
                    }
                    continue;
                }

                std::uint32_t codePoint = 0;
                std::size_t continuationCount = 0;
                if (first >= 0xC2 && first <= 0xDF)
                {
                    codePoint = first & 0x1F;
                    continuationCount = 1;
                }
                else if (first >= 0xE0 && first <= 0xEF)
                {
                    codePoint = first & 0x0F;
                    continuationCount = 2;
                }
                else if (first >= 0xF0 && first <= 0xF4)
                {
                    codePoint = first & 0x07;
                    continuationCount = 3;
                }
                else
                {
                    return false;
                }

                if (continuationCount > text.size() - index)
                {
                    return false;
                }
                for (std::size_t count = 0; count < continuationCount; ++count)
                {
                    const unsigned char continuation = bytes[index++];
                    if ((continuation & 0xC0) != 0x80)
                    {
                        return false;
                    }
                    codePoint = (codePoint << 6) | (continuation & 0x3F);
                }

                if ((continuationCount == 2 && codePoint < 0x800) || (continuationCount == 3 && codePoint < 0x10000) || codePoint > 0x10FFFF ||
                    (codePoint >= 0xD800 && codePoint <= 0xDFFF))
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool validSize(Types::Size size) noexcept
        {
            constexpr auto nativeMaximum = static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
            return size.width != 0 && size.height != 0 && size.width <= nativeMaximum && size.height <= nativeMaximum;
        }

        [[nodiscard]] bool validPixelSize(Types::PixelSize size) noexcept
        {
            constexpr auto nativeMaximum = static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
            return size.width != 0 && size.height != 0 && size.width <= nativeMaximum && size.height <= nativeMaximum;
        }

        [[nodiscard]] bool validRect(const Types::Rect &rect) noexcept
        {
            return validSize(rect.size);
        }

        [[nodiscard]] bool validLimits(const Types::SizeLimits &limits) noexcept
        {
            if ((limits.minimum && !validSize(*limits.minimum)) || (limits.maximum && !validSize(*limits.maximum)))
            {
                return false;
            }
            return !limits.minimum || !limits.maximum ||
                   (limits.minimum->width <= limits.maximum->width && limits.minimum->height <= limits.maximum->height);
        }

        [[nodiscard]] bool sizeWithin(Types::Size size, const Types::SizeLimits &limits) noexcept
        {
            return (!limits.minimum || (size.width >= limits.minimum->width && size.height >= limits.minimum->height)) &&
                   (!limits.maximum || (size.width <= limits.maximum->width && size.height <= limits.maximum->height));
        }

        [[nodiscard]] bool validRatio(const std::optional<Types::AspectRatio> &ratio) noexcept
        {
            return !ratio || (ratio->numerator != 0 && ratio->denominator != 0);
        }

        template <typename Enum> [[nodiscard]] bool validEnum(Enum value) noexcept;

        template <> bool validEnum(Types::WindowMode value) noexcept
        {
            return value == Types::WindowMode::Windowed || value == Types::WindowMode::BorderlessFullscreen ||
                   value == Types::WindowMode::ExclusiveFullscreen;
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
                   value == Types::PointerInputMode::AcceptRegions || value == Types::PointerInputMode::IgnoreRegions;
        }

        template <> bool validEnum(Types::BackdropEffect value) noexcept
        {
            return value == Types::BackdropEffect::None || value == Types::BackdropEffect::BlurBehind;
        }

        [[nodiscard]] bool validDisplayMode(const Types::DisplayMode &mode) noexcept
        {
            return validPixelSize(mode.resolution) && mode.refreshRateMillihertz != 0 && mode.bitsPerPixel != 0;
        }

        [[nodiscard]] bool validModeRequest(const Types::ModeRequest &request) noexcept
        {
            if (!validEnum(request.mode))
            {
                return false;
            }
            if (request.mode != Types::WindowMode::ExclusiveFullscreen && request.displayMode)
            {
                return false;
            }
            if (request.mode == Types::WindowMode::Windowed && request.monitor.valid())
            {
                return false;
            }
            return !request.displayMode || validDisplayMode(*request.displayMode);
        }

        [[nodiscard]] IO::Types::Status validateDescription(const Types::Description &description) noexcept
        {
            if (!validUtf8(description.title) || !validSize(description.clientSize) || !validEnum(description.placement.kind) ||
                (description.placement.kind != Types::PlacementKind::Centered && description.placement.monitor.valid()) ||
                !validModeRequest(description.mode) || !validEnum(description.presentation) || !validEnum(description.decoration) ||
                !validLimits(description.sizeLimits) || !sizeWithin(description.clientSize, description.sizeLimits) ||
                !validRatio(description.aspectRatio) || !validEnum(description.cursorMode) || !validEnum(description.cursorShape) ||
                !validEnum(description.pointerInputMode) || description.pointerInputMode == Types::PointerInputMode::AcceptRegions ||
                description.pointerInputMode == Types::PointerInputMode::IgnoreRegions || !validEnum(description.backdropEffect) ||
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
            if (state == nullptr || !state->platform)
            {
                return error(ErrorCode::NotOpen);
            }
            if (!Detail::Platform::isOwnedByCurrentThread(*state))
            {
                return error(ErrorCode::ResourceBusy);
            }
            return IO::successStatus();
        }

        [[nodiscard]] bool owned(const Detail::WindowState *state) noexcept
        {
            return state != nullptr &&
                   (state->platform ? Detail::Platform::isOwnedByCurrentThread(*state) : state->ownerThread == std::this_thread::get_id());
        }

        void initializeCachedState(Detail::WindowState &state, const Types::Description &description)
        {
            state.ownerThread = std::this_thread::get_id();
            state.owner = description.owner;
            state.title = description.title;
            state.clientSize = description.clientSize;
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
            state.fileDropEnabled = description.acceptsFileDrops;
            state.transparentFramebuffer = description.transparentFramebuffer;
            state.suppressEvents = true;
        }

        void releaseEventStorage(Detail::WindowState &state) noexcept
        {
            for (std::size_t index = 0; index < state.eventCount; ++index)
            {
                state.eventStorage[(state.eventHead + index) % state.eventStorage.size()] = {};
            }
            state.eventHead = 0;
            state.eventCount = 0;
            state.eventStorage = {};
        }
    } // namespace

    Types::CapabilitiesResult getCapabilities() noexcept
    {
        return Detail::Platform::getCapabilities();
    }

    bool supports(Types::Capability capability) noexcept
    {
        return getCapabilities().capabilities.supports(capability);
    }

    Types::EventPumpResult pollEvents() noexcept
    {
        return Detail::Platform::pumpEvents(kNoWait, false);
    }

    Types::EventPumpResult waitEvents(std::chrono::milliseconds timeout) noexcept
    {
        if (timeout < kWaitForever)
        {
            return {.status = error(ErrorCode::InvalidArgument)};
        }
        return Detail::Platform::pumpEvents(timeout, true);
    }

    Types::MonitorListResult getMonitors() noexcept
    {
        return Detail::Platform::getMonitors();
    }
    Types::MonitorInfoResult getPrimaryMonitor() noexcept
    {
        return Detail::Platform::getPrimaryMonitor();
    }
    Types::MonitorInfoResult getMonitor(Types::MonitorId monitor) noexcept
    {
        return Detail::Platform::getMonitor(monitor);
    }
    Types::DisplayModeListResult getDisplayModes(Types::MonitorId monitor) noexcept
    {
        return Detail::Platform::getDisplayModes(monitor);
    }
    Types::DisplayModeResult getCurrentDisplayMode(Types::MonitorId monitor) noexcept
    {
        return Detail::Platform::getCurrentDisplayMode(monitor);
    }
    Types::DisplayModeResult getPreferredDisplayMode(Types::MonitorId monitor) noexcept
    {
        return Detail::Platform::getPreferredDisplayMode(monitor);
    }

    Window::Window() noexcept = default;
    Window::Window(Window &&other) noexcept
        : state_(std::move(other.state_))
    {
    }

    Window::~Window() noexcept
    {
        if (state_)
        {
            Detail::Platform::closeBestEffort(*state_);
            releaseEventStorage(*state_);
        }
    }

    IO::Types::Status Window::open(const Types::Description &description) noexcept
    {
        return open(description, kDefaultEventQueueCapacity);
    }

    IO::Types::Status Window::open(const Types::Description &description, std::size_t eventQueueCapacity) noexcept
    {
        if (state_)
        {
            return error(ErrorCode::AlreadyOpen);
        }
        if (eventQueueCapacity == 0)
        {
            return error(ErrorCode::InvalidArgument);
        }

        const IO::Types::Status validation = validateDescription(description);
        if (!validation.ok())
        {
            return validation;
        }
        if (Detail::consumeFailure(TestHooks::FailurePoint::Allocation))
            return error(ErrorCode::OutOfMemory);

        try
        {
            auto candidate = std::make_unique<Detail::WindowState>();
            initializeCachedState(*candidate, description);
            if (eventQueueCapacity > candidate->internalEvents.max_size())
            {
                return error(ErrorCode::InvalidArgument);
            }
            candidate->internalEvents.resize(eventQueueCapacity);
            candidate->eventStorage = std::span<Types::Event>{candidate->internalEvents.data(), candidate->internalEvents.size()};
            candidate->eventStorageKind = Types::EventStorageKind::Internal;

            IO::Types::Status status = Detail::Platform::open(*candidate, description);
            if (!status.ok())
            {
                Detail::Platform::closeBestEffort(*candidate);
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

    IO::Types::Status Window::open(const Types::Description &description, std::span<Types::Event> eventStorage) noexcept
    {
        if (state_)
        {
            return error(ErrorCode::AlreadyOpen);
        }
        if (eventStorage.empty())
        {
            return error(ErrorCode::InvalidArgument);
        }
        const IO::Types::Status validation = validateDescription(description);
        if (!validation.ok())
        {
            return validation;
        }
        if (Detail::consumeFailure(TestHooks::FailurePoint::Allocation))
            return error(ErrorCode::OutOfMemory);

        try
        {
            auto candidate = std::make_unique<Detail::WindowState>();
            initializeCachedState(*candidate, description);
            candidate->eventStorage = eventStorage;
            candidate->eventStorageKind = Types::EventStorageKind::External;
            IO::Types::Status status = Detail::Platform::open(*candidate, description);
            if (!status.ok())
            {
                Detail::Platform::closeBestEffort(*candidate);
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

    bool Window::isOpen() const noexcept
    {
        return state_ != nullptr && state_->platform != nullptr;
    }

    IO::Types::Status Window::close() noexcept
    {
        if (!isOpen())
        {
            if (state_)
                releaseEventStorage(*state_);
            state_.reset();
            return IO::successStatus();
        }
        if (!Detail::Platform::isOwnedByCurrentThread(*state_))
        {
            return error(ErrorCode::ResourceBusy);
        }

        Detail::Platform::CloseResult result = Detail::Platform::close(*state_);
        if (result.resourceClosed)
        {
            releaseEventStorage(*state_);
            state_.reset();
        }
        return result.status;
    }

    Types::WindowId Window::id() const noexcept
    {
        return state_ ? state_->id : Types::WindowId{};
    }
    Types::WindowId Window::ownerId() const noexcept
    {
        return state_ ? state_->owner : Types::WindowId{};
    }
    bool Window::isOwnedByCurrentThread() const noexcept
    {
        return owned(state_.get());
    }

    bool Window::supports(Types::Capability capability) const noexcept
    {
        if (capability == Types::Capability::OcclusionReporting)
            return state_ != nullptr && state_->occlusionProviderAttached;
        return ::GameWIP::Window::supports(capability);
    }

    IO::Types::Status Window::setOwner(Types::WindowId owner) noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        if (!status.ok())
        {
            return status;
        }
        if (owner == state_->id)
        {
            return error(ErrorCode::InvalidArgument);
        }
        return Detail::Platform::setOwner(*state_, owner);
    }

    bool Window::closeRequested() const noexcept
    {
        return state_ && state_->closeRequested;
    }

    IO::Types::Status Window::requestClose() noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        if (!status.ok())
        {
            return status;
        }
        static_cast<void>(Detail::requestClose(*state_, Types::CloseRequestSource::Programmatic));
        return IO::successStatus();
    }

    IO::Types::Status Window::clearCloseRequest() noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        if (!status.ok())
        {
            return status;
        }
        state_->closeRequested = false;
        return IO::successStatus();
    }

    bool Window::popEvent(Types::Event &outEvent) noexcept
    {
        if (!owned(state_.get()) || state_->eventCount == 0)
        {
            return false;
        }
        outEvent = std::move(state_->eventStorage[state_->eventHead]);
        state_->eventHead = (state_->eventHead + 1) % state_->eventStorage.size();
        --state_->eventCount;
        if (state_->eventCount == 0)
        {
            state_->eventHead = 0;
        }
        return true;
    }

    std::size_t Window::popEvents(std::span<Types::Event> destination) noexcept
    {
        std::size_t count = 0;
        while (count < destination.size() && popEvent(destination[count]))
        {
            ++count;
        }
        return count;
    }

    void Window::clearEvents() noexcept
    {
        if (!owned(state_.get()))
        {
            return;
        }
        for (std::size_t index = 0; index < state_->eventCount; ++index)
        {
            state_->eventStorage[(state_->eventHead + index) % state_->eventStorage.size()] = {};
        }
        state_->eventHead = 0;
        state_->eventCount = 0;
    }

    Types::EventQueueInfo Window::eventQueueInfo() const noexcept
    {
        if (!state_)
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
        if (owned(state_.get()))
        {
            state_->droppedEvents = 0;
        }
    }

    IO::Types::Status Window::wakeEventWait() const noexcept
    {
        if (!isOpen())
        {
            return error(ErrorCode::NotOpen);
        }
        return Detail::Platform::wakeEventWait(*state_);
    }

    std::string_view Window::title() const noexcept
    {
        return state_ ? std::string_view(state_->title) : std::string_view{};
    }
    Types::Size Window::clientSize() const noexcept
    {
        return state_ ? state_->clientSize : Types::Size{};
    }
    Types::PixelSize Window::framebufferSize() const noexcept
    {
        return state_ ? state_->framebufferSize : Types::PixelSize{};
    }
    Types::Position Window::clientPosition() const noexcept
    {
        return state_ ? state_->clientPosition : Types::Position{};
    }
    Types::Rect Window::frameRect() const noexcept
    {
        return state_ ? state_->frameRect : Types::Rect{};
    }
    Types::Insets Window::frameInsets() const noexcept
    {
        return state_ ? state_->frameInsets : Types::Insets{};
    }
    Types::ContentScale Window::contentScale() const noexcept
    {
        return state_ ? state_->contentScale : Types::ContentScale{};
    }
    Types::Dpi Window::effectiveDpi() const noexcept
    {
        return state_ ? state_->dpi : Types::Dpi{};
    }
    Types::MonitorId Window::currentMonitor() const noexcept
    {
        return state_ ? state_->monitor : Types::MonitorId{};
    }
    Types::WindowMode Window::mode() const noexcept
    {
        return state_ ? state_->mode : Types::WindowMode::Windowed;
    }
    Types::FullscreenInfo Window::fullscreenInfo() const noexcept
    {
        return state_ ? state_->fullscreen : Types::FullscreenInfo{};
    }
    Types::PresentationState Window::presentationState() const noexcept
    {
        return state_ ? state_->presentation : Types::PresentationState::Normal;
    }
    Types::DecorationMode Window::decorationMode() const noexcept
    {
        return state_ ? state_->decoration : Types::DecorationMode::System;
    }
    Types::WindowControls Window::windowControls() const noexcept
    {
        return state_ ? state_->controls : Types::WindowControls{};
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
    bool Window::isVisible() const noexcept
    {
        return state_ && state_->visible;
    }
    bool Window::isFocused() const noexcept
    {
        return state_ && state_->focused;
    }
    bool Window::isMinimized() const noexcept
    {
        return state_ && state_->presentation == Types::PresentationState::Minimized;
    }
    bool Window::isMaximized() const noexcept
    {
        return state_ && state_->presentation == Types::PresentationState::Maximized;
    }
    bool Window::isOccluded() const noexcept
    {
        return state_ && state_->occluded;
    }
    bool Window::isCursorInside() const noexcept
    {
        return state_ && state_->cursorInside;
    }
    bool Window::isResizable() const noexcept
    {
        return state_ && state_->resizable;
    }
    bool Window::isFocusable() const noexcept
    {
        return state_ && state_->focusable;
    }
    bool Window::isUserInteractionEnabled() const noexcept
    {
        return state_ && state_->interactionEnabled;
    }
    bool Window::isAlwaysOnTop() const noexcept
    {
        return state_ && state_->alwaysOnTop;
    }
    bool Window::acceptsFileDrops() const noexcept
    {
        return state_ && state_->fileDropEnabled;
    }
    bool Window::hasTransparentFramebuffer() const noexcept
    {
        return state_ && state_->transparentFramebuffer;
    }

    IO::Types::Status Window::setTitle(std::string_view utf8Title) noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        if (!status.ok())
            return status;
        if (!validUtf8(utf8Title))
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
            if (width != image.size.width || height != image.size.height || height > std::numeric_limits<std::size_t>::max() / width)
            {
                return error(ErrorCode::InvalidArgument);
            }
            const std::size_t pixels = width * height;
            if (pixels > std::numeric_limits<std::size_t>::max() / channels)
                return error(ErrorCode::InvalidArgument);
            const std::size_t required = pixels * channels;
            if (image.rgba8.size() != required)
                return error(ErrorCode::InvalidArgument);
        }
        return Detail::Platform::setIcon(*state_, images);
    }

    IO::Types::Status Window::clearIcon() noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        return status.ok() ? Detail::Platform::clearIcon(*state_) : status;
    }

    IO::Types::Status Window::setClientSize(Types::Size size) noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        if (!status.ok())
            return status;
        if (!validSize(size) || !sizeWithin(size, state_->sizeLimits))
            return error(ErrorCode::InvalidArgument);
        return Detail::Platform::setClientSize(*state_, size);
    }

    IO::Types::Status Window::setClientPosition(Types::Position position) noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        return status.ok() ? Detail::Platform::setClientPosition(*state_, position) : status;
    }

    IO::Types::Status Window::setClientRect(const Types::Rect &rect) noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        if (!status.ok())
            return status;
        if (!validRect(rect) || !sizeWithin(rect.size, state_->sizeLimits))
            return error(ErrorCode::InvalidArgument);
        return Detail::Platform::setClientRect(*state_, rect);
    }

    IO::Types::Status Window::centerOn(Types::MonitorId monitor) noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        return status.ok() ? Detail::Platform::centerOn(*state_, monitor) : status;
    }

    IO::Types::Status Window::setSizeLimits(const Types::SizeLimits &limits) noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        if (!status.ok())
            return status;
        if (!validLimits(limits))
            return error(ErrorCode::InvalidArgument);
        return Detail::Platform::setSizeLimits(*state_, limits);
    }

    IO::Types::Status Window::setAspectRatio(std::optional<Types::AspectRatio> ratio) noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        if (!status.ok())
            return status;
        if (!validRatio(ratio))
            return error(ErrorCode::InvalidArgument);
        return Detail::Platform::setAspectRatio(*state_, ratio);
    }

    Types::PositionResult Window::clientToScreen(Types::Position position) const noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        return status.ok() ? Detail::Platform::clientToScreen(*state_, position) : Types::PositionResult{status, {}};
    }

    Types::PositionResult Window::screenToClient(Types::Position position) const noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        return status.ok() ? Detail::Platform::screenToClient(*state_, position) : Types::PositionResult{status, {}};
    }

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
        return status.ok() ? Detail::Platform::setResizable(*state_, resizable) : status;
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

    IO::Types::Status Window::setWindowControls(const Types::WindowControls &controls) noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        return status.ok() ? Detail::Platform::setWindowControls(*state_, controls) : status;
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
        {
            return capabilities.status.ok() ? error(ErrorCode::InvalidArgument) : capabilities.status;
        }
        for (const Types::Rect &rect : layout.draggableRegions)
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
            std::vector<Types::Rect> copied(layout.draggableRegions.begin(), layout.draggableRegions.end());
            std::vector<Types::Rect> previous = std::move(state_->draggableRegions);
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
        const Types::CustomChromeLayout empty{};
        return setCustomChromeLayout(empty);
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
        if (layout.regions.size() > capabilities.capabilities.maximumPointerInputRegions)
        {
            return error(ErrorCode::InvalidArgument);
        }
        for (const Types::Rect &rect : layout.regions)
        {
            if (!validRect(rect))
                return error(ErrorCode::InvalidArgument);
        }

        try
        {
            if (Detail::consumeFailure(TestHooks::FailurePoint::RegionCopy))
                return error(ErrorCode::OutOfMemory);
            std::vector<Types::Rect> copied(layout.regions.begin(), layout.regions.end());
            std::vector<Types::Rect> previous = std::move(state_->pointerInputRegions);
            const Types::PointerInputMode previousMode = state_->pointerInputMode;
            state_->pointerInputRegions = std::move(copied);
            state_->pointerInputMode = layout.mode;
            status = Detail::Platform::setPointerInputLayout(*state_);
            if (!status.ok())
            {
                state_->pointerInputRegions = std::move(previous);
                state_->pointerInputMode = previousMode;
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

    IO::Types::Status Window::setCursorPosition(Types::Position position) noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        return status.ok() ? Detail::Platform::setCursorPosition(*state_, position) : status;
    }

    Types::PositionResult Window::cursorPosition() const noexcept
    {
        IO::Types::Status status = requireState(state_.get());
        return status.ok() ? Detail::Platform::cursorPosition(*state_) : Types::PositionResult{status, {}};
    }
} // namespace GameWIP::Window
