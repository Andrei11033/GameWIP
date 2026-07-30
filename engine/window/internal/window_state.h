/// @file window_state.h
/// @brief Private stable Window state and fixed-capacity event queue.

#pragma once

#include "window/window.h"

#include <memory>
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

    /// @brief Stable state addressed by native callbacks across Window moves.
    struct WindowState
    {
        std::unique_ptr<Platform::WindowData, Platform::WindowDataDeleter> platform;
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
        Types::Size clientSize;
        Types::PixelSize framebufferSize;
        Types::Position clientPosition;
        Types::Rect frameRect;
        Types::Insets frameInsets;
        Types::ContentScale contentScale;
        Types::Dpi dpi;
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
        std::vector<Types::Rect> pointerInputRegions;
        std::vector<Types::Rect> draggableRegions;
        std::optional<Types::Rect> systemMenuRegion;
        std::optional<Types::Rect> minimizeButtonRegion;
        std::optional<Types::Rect> maximizeButtonRegion;
        std::optional<Types::Rect> closeButtonRegion;
        Types::BackdropEffect backdrop = Types::BackdropEffect::None;
        float opacity = 1.0F;
        bool closeRequested = false;
        bool visible = false;
        bool focused = false;
        bool occluded = false;
        bool occlusionProviderAttached = false;
        bool cursorInside = false;
        bool resizable = true;
        bool focusable = true;
        bool interactionEnabled = true;
        bool alwaysOnTop = false;
        bool fileDropEnabled = false;
        bool transparentFramebuffer = false;
        bool suppressEvents = false;
    };

    /// @brief Returns whether an event payload participates in compatible coalescing.
    [[nodiscard]] bool isCoalescible(const Types::EventData &data) noexcept;
    /// @brief Routes an event after authoritative state has been updated.
    [[nodiscard]] EnqueueResult enqueueEvent(WindowState &state, Types::EventData data) noexcept;
    /// @brief Sets sticky close intent and queues the first corresponding event.
    [[nodiscard]] EnqueueResult requestClose(WindowState &state, Types::CloseRequestSource source) noexcept;

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
    };
} // namespace GameWIP::Window::Detail
