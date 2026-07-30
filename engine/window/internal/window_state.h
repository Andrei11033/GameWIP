/// @file window_state.h
/// @brief Private stable Window state and fixed-capacity event queue.

#pragma once

#include "window/window.h"

#include <memory>
#include <limits>
#include <span>
#include <thread>
#include <vector>

namespace GameWIP::Window::Detail::Platform
{
    struct WindowData;

    /// @brief Deletes backend-owned state without exposing its definition to portable core code.
    struct WindowDataDeleter
    {
        void operator()(WindowData *data) const noexcept;
    };
} // namespace GameWIP::Window::Detail::Platform

namespace GameWIP::Window::Detail
{
    /// @brief Result of routing one event into fixed storage.
    enum class EnqueueResult
    {
        Queued,
        Coalesced,
        Dropped
    };

    /// @brief Stable state addressed by native callbacks for one open lifetime.
    struct WindowState
    {
        ~WindowState() noexcept
        {
            clearRetainedEvents();
        }

        void clearRetainedEvents() noexcept
        {
            if (!eventStorage.empty())
            {
                for (std::size_t index = 0; index < eventCount; ++index)
                    eventStorage[(eventHead + index) % eventStorage.size()] = {};
            }
            eventHead = 0;
            eventCount = 0;
            eventStorage = {};
        }

        std::unique_ptr<Platform::WindowData, Platform::WindowDataDeleter> platform;
        WindowState *deferredCleanupNext = nullptr;
        std::thread::id ownerThread;
        Types::WindowId id;
        Types::WindowId owner;

        std::vector<Types::Event> internalEvents;
        std::span<Types::Event> eventStorage;
        Types::EventStorageKind eventStorageKind = Types::EventStorageKind::Internal;
        std::size_t eventHead = 0;
        std::size_t eventCount = 0;
        std::uint64_t nextSequence = 1;
        std::uint64_t droppedEvents = 0;

        std::string title;
        Types::LogicalSize clientSize;
        Types::PixelSize framebufferSize;
        Types::ScreenPosition clientPosition;
        Types::ScreenRect frameRect;
        Types::Insets frameInsets;
        Types::ContentScale contentScale;
        Types::Dpi dpi;
        Types::DpiResizePolicy dpiResizePolicy = Types::DpiResizePolicy::PreserveLogicalClientSize;
        Types::MonitorId monitor;
        Types::WindowMode mode = Types::WindowMode::Windowed;
        Types::FullscreenInfo fullscreen;
        Types::PresentationState presentation = Types::PresentationState::Normal;
        Types::DecorationMode decoration = Types::DecorationMode::System;
        Types::WindowControls controls;
        Types::SizeLimits sizeLimits;
        std::optional<Types::AspectRatio> aspectRatio;
        Types::CursorMode cursorMode = Types::CursorMode::Normal;
        Types::CursorShape cursorShape = Types::CursorShape::Arrow;
        Types::PointerInputMode pointerInputMode = Types::PointerInputMode::Normal;
        std::vector<Types::LogicalRect> pointerInputRegions;
        std::vector<Types::LogicalRect> draggableRegions;
        std::optional<Types::LogicalRect> systemMenuRegion;
        std::optional<Types::LogicalRect> minimizeButtonRegion;
        std::optional<Types::LogicalRect> maximizeButtonRegion;
        std::optional<Types::LogicalRect> closeButtonRegion;
        Types::BackdropEffect backdrop = Types::BackdropEffect::None;
        float opacity = 1.0F;
        bool closeRequested = false;
        bool visible = false;
        bool focused = false;
        bool occluded = false;
        bool occlusionProviderAttached = false;
        std::vector<std::uint64_t> pointerHitMask;
        Types::PixelSize pointerHitMaskSize;
        std::uint64_t pointerHitMaskActiveGeneration = 0;
        std::uint64_t pointerHitMaskTargetGeneration = 0;
        Types::PixelSize pointerHitMaskTargetSize;
        std::size_t pointerHitMaskTargetWordCount = 0;
        std::uint64_t *pointerHitMaskGeneration = nullptr;
        bool *pointerHitMaskGenerationExhausted = nullptr;
        bool pointerHitMaskBackendSupportedForTesting = false;
        bool cursorInside = false;
        bool resizable = true;
        bool focusable = true;
        bool interactionEnabled = true;
        bool alwaysOnTop = false;
        bool fileDropEnabled = false;
        bool transparentFramebuffer = false;
        bool suppressEvents = false;
        bool nativeDestroyedPendingFinalize = false;
    };

    /// @brief Returns whether an event payload participates in compatible coalescing.
    [[nodiscard]] bool isCoalescible(const Types::EventData &data) noexcept;
    /// @brief Routes an event after authoritative state has been updated.
    [[nodiscard]] EnqueueResult enqueueEvent(WindowState &state, Types::EventData data) noexcept;
    /// @brief Sets sticky close intent and queues the first corresponding event.
    [[nodiscard]] EnqueueResult requestClose(WindowState &state, Types::CloseRequestSource source) noexcept;

    /// @brief Invalidates active and outstanding mask state while retaining allocation capacity.
    inline void invalidatePointerHitMask(WindowState &state) noexcept
    {
        state.pointerHitMask.clear();
        state.pointerHitMaskSize = {};
        state.pointerHitMaskActiveGeneration = 0;
        state.pointerHitMaskTargetGeneration = 0;
        state.pointerHitMaskTargetSize = {};
        state.pointerHitMaskTargetWordCount = 0;
        if (state.pointerHitMaskGeneration != nullptr && state.pointerHitMaskGenerationExhausted != nullptr)
        {
            if (*state.pointerHitMaskGeneration == std::numeric_limits<std::uint64_t>::max())
                *state.pointerHitMaskGenerationExhausted = true;
            else
                ++*state.pointerHitMaskGeneration;
        }
    }

    /// @brief Samples a logical client position with interactive fallback on every invalid condition.
    [[nodiscard]] inline bool pointerHitMaskAccepts(const WindowState &state, Types::LogicalPosition position) noexcept
    {
        if (state.pointerHitMask.empty() || state.pointerHitMaskActiveGeneration == 0 ||
            state.pointerHitMaskSize != state.framebufferSize || state.clientSize.width == 0 || state.clientSize.height == 0 ||
            position.x < 0 || position.y < 0 || static_cast<std::uint32_t>(position.x) >= state.clientSize.width ||
            static_cast<std::uint32_t>(position.y) >= state.clientSize.height)
            return true;

        const std::uint64_t x = static_cast<std::uint64_t>(position.x) * state.framebufferSize.width / state.clientSize.width;
        const std::uint64_t y = static_cast<std::uint64_t>(position.y) * state.framebufferSize.height / state.clientSize.height;
        if (x >= state.framebufferSize.width || y >= state.framebufferSize.height ||
            y > (std::numeric_limits<std::size_t>::max() - x) / state.framebufferSize.width)
            return true;
        const std::size_t index = static_cast<std::size_t>(y * state.framebufferSize.width + x);
        const std::size_t word = index / 64U;
        if (word >= state.pointerHitMask.size())
            return true;
        const std::uint64_t stableWord = state.pointerHitMask[word];
        return (stableWord & (std::uint64_t{1} << (index % 64U))) != 0;
    }

    /// @brief Controlled private-state access for native adapters and approved test hooks.
    struct WindowAccess
    {
        [[nodiscard]] static WindowState *state(Window &window) noexcept
        {
            return window.state_.get();
        }
        [[nodiscard]] static const WindowState *state(const Window &window) noexcept
        {
            return window.state_.get();
        }
        [[nodiscard]] static std::unique_ptr<WindowState> &stateOwner(Window &window) noexcept
        {
            return window.state_;
        }
        static void bindPointerHitMaskLifetime(Window &window, WindowState &state) noexcept
        {
            state.pointerHitMaskGeneration = &window.pointerHitMaskGeneration_;
            state.pointerHitMaskGenerationExhausted = &window.pointerHitMaskGenerationExhausted_;
        }
    };
} // namespace GameWIP::Window::Detail
