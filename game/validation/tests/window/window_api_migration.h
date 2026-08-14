/// @file window_api_migration.h
/// @brief Source-tree-only aliases used while the exhaustive Window validation suite keeps its scenario text stable.
/// @details This header is not part of Window and is never installed. It maps the validation fixture's historical
/// spellings to the standardized public API so behavior coverage stays intact during the API migration.

#pragma once

#include "window/display_info.h"
#include "window/renderer_bridge.h"
#include "window/window.h"

// Keep the existing source-tree validation conditionals active without exposing the old macro from Window.
#ifndef INTERNAL_WINDOW_TEST_HOOKS
#define INTERNAL_WINDOW_TEST_HOOKS WINDOW_INTERNAL_TEST_HOOKS
#endif

namespace GameWIP::Window::Types
{
    using MonitorId = Display::MonitorId;
    using DisplayMode = Display::Mode;
    using MonitorInfo = Display::Info;
    using MonitorListResult = Display::MonitorsResult;
    using MonitorInfoResult = Display::InfoResult;
    using DisplayModeListResult = Display::ModesResult;
    using DisplayModeResult = Display::ModeResult;
    using DisplayColorSpace = Display::ColorSpace;
    using DisplayColorInfo = Display::ColorInfo;
    using DisplayColorInfoResult = Display::ColorInfoResult;

    using WindowMode = Mode;
    using WindowControls = Controls;

    using CloseRequestSource = Events::CloseRequestSource;
    using CloseRequestedEvent = Events::CloseRequested;
    using ClosedEvent = Events::NativeDestroyed;
    using VisibilityChangedEvent = Events::VisibilityChanged;
    using MovedEvent = Events::ClientPositionChanged;
    using ClientSizeChangedEvent = Events::ClientSizeChanged;
    using FramebufferSizeChangedEvent = Events::FramebufferSizeChanged;
    using FocusChangedEvent = Events::FocusChanged;
    using PresentationStateChangedEvent = Events::PresentationStateChanged;
    using ContentScaleChangedEvent = Events::ContentScaleChanged;
    using MonitorChangedEvent = Events::MonitorChanged;
    using ModeChangedEvent = Events::ModeChanged;
    using OwnerChangedEvent = Events::OwnerChanged;
    using DisplayConfigurationChangedEvent = Events::DisplayConfigurationChanged;
    using CursorPresenceChangedEvent = Events::CursorPresenceChanged;
    using FilesDroppedEvent = Events::FilesDropped;
    using OcclusionChangedEvent = Events::OcclusionChanged;
    using RedrawRequestedEvent = Events::RedrawRequested;
    using EventData = Events::Payload;
    using EventStorageKind = Events::StorageKind;
    using EventQueueInfo = Events::QueueInfo;
    using EventPumpResult = Events::PumpResult;
} // namespace GameWIP::Window::Types

namespace GameWIP::Window
{
    inline constexpr std::size_t kDefaultEventQueueCapacity = Events::kDefaultQueueCapacity;
    inline constexpr auto kNoWait = Events::kNoWait;
    inline constexpr auto kWaitForever = Events::kWaitForever;

    [[nodiscard]] inline Types::Events::PumpResult pollEvents() noexcept
    {
        return Events::poll();
    }

    [[nodiscard]] inline Types::Events::PumpResult waitEvents(std::chrono::milliseconds timeout = Events::kWaitForever) noexcept
    {
        return Events::wait(timeout);
    }

    [[nodiscard]] inline Types::Display::MonitorsResult getMonitors() noexcept
    {
        return Display::getMonitors();
    }

    [[nodiscard]] inline Types::Display::InfoResult getPrimaryMonitor() noexcept
    {
        return Display::getPrimaryMonitor();
    }

    [[nodiscard]] inline Types::Display::InfoResult getMonitor(Types::Display::MonitorId monitor) noexcept
    {
        return Display::getMonitor(monitor);
    }

    [[nodiscard]] inline Types::Display::ModesResult getDisplayModes(Types::Display::MonitorId monitor) noexcept
    {
        return Display::getModes(monitor);
    }

    [[nodiscard]] inline Types::Display::ModeResult getCurrentDisplayMode(Types::Display::MonitorId monitor) noexcept
    {
        return Display::getCurrentMode(monitor);
    }

    [[nodiscard]] inline Types::Display::ModeResult getPreferredDisplayMode(Types::Display::MonitorId monitor) noexcept
    {
        return Display::getPreferredMode(monitor);
    }
} // namespace GameWIP::Window

namespace GameWIP::Window::Renderer
{
    using PointerHitMaskWord = Types::Renderer::PointerHitMaskWord;
    using PointerHitMaskTarget = Types::Renderer::PointerHitMaskTarget;
    using PointerHitMaskTargetResult = Types::Renderer::PointerHitMaskResult;

    [[nodiscard]] inline Types::Display::ColorInfoResult getDisplayColorInfo(Types::Display::MonitorId monitor) noexcept
    {
        return Display::getColorInfo(monitor);
    }

    [[nodiscard]] inline Types::Display::ColorInfoResult getWindowDisplayColorInfo(const Window &window) noexcept
    {
        return Display::getColorInfo(window);
    }
} // namespace GameWIP::Window::Renderer

// Function-like aliases only rewrite historical member calls; fields such as ManualNativeWindowState::valid are untouched.
#define valid(...) isValid(__VA_ARGS__)
#define closeRequested(...) hasCloseRequest(__VA_ARGS__)
#define windowControls(...) controls(__VA_ARGS__)
#define setWindowControls(...) setControls(__VA_ARGS__)
#define acceptsFileDrops(...) isFileDropEnabled(__VA_ARGS__)
