/// @file window_platform.h
/// @brief Internal portable-to-native backend contract for Window.

#pragma once

#include "window/internal/window_state.h"

#include <chrono>
#include <cstdint>
#include <span>
#include <string_view>

namespace GameWIP::Window::Detail::Platform
{
    /// @brief Close result distinguishes retryable ownership from late cleanup diagnostics.
    struct CloseResult
    {
        IO::Types::Status status;
        bool resourceClosed = false;
    };

    /// @brief Internal native handles represented without native public-header dependencies.
    struct NativeHandleView
    {
        void *instance = nullptr;
        void *window = nullptr;
    };

    [[nodiscard]] Types::CapabilitiesResult getCapabilities() noexcept;
    [[nodiscard]] Types::EventPumpResult pumpEvents(std::chrono::milliseconds timeout, bool wait) noexcept;
    [[nodiscard]] Types::MonitorListResult getMonitors() noexcept;
    [[nodiscard]] Types::MonitorInfoResult getPrimaryMonitor() noexcept;
    [[nodiscard]] Types::MonitorInfoResult getMonitor(Types::MonitorId monitor) noexcept;
    [[nodiscard]] Types::DisplayModeListResult getDisplayModes(Types::MonitorId monitor) noexcept;
    [[nodiscard]] Types::DisplayModeResult getCurrentDisplayMode(Types::MonitorId monitor) noexcept;
    [[nodiscard]] Types::DisplayModeResult getPreferredDisplayMode(Types::MonitorId monitor) noexcept;

    [[nodiscard]] IO::Types::Status open(WindowState &state, const Types::Description &description) noexcept;
    [[nodiscard]] CloseResult close(WindowState &state) noexcept;
    void closeBestEffort(WindowState &state) noexcept;
    [[nodiscard]] bool isOwnedByCurrentThread(const WindowState &state) noexcept;
    [[nodiscard]] IO::Types::Status wakeEventWait(const WindowState &state) noexcept;
    [[nodiscard]] NativeHandleView nativeHandle(const WindowState &state) noexcept;

    [[nodiscard]] IO::Types::Status setOwner(WindowState &state, Types::WindowId owner) noexcept;
    [[nodiscard]] IO::Types::Status setTitle(WindowState &state, std::string_view utf8Title) noexcept;
    [[nodiscard]] IO::Types::Status setIcon(WindowState &state, std::span<const Types::IconImageView> images) noexcept;
    [[nodiscard]] IO::Types::Status clearIcon(WindowState &state) noexcept;
    [[nodiscard]] IO::Types::Status setClientSize(WindowState &state, Types::Size size) noexcept;
    [[nodiscard]] IO::Types::Status setClientPosition(WindowState &state, Types::Position position) noexcept;
    [[nodiscard]] IO::Types::Status setClientRect(WindowState &state, const Types::Rect &rect) noexcept;
    [[nodiscard]] IO::Types::Status centerOn(WindowState &state, Types::MonitorId monitor) noexcept;
    [[nodiscard]] IO::Types::Status setSizeLimits(WindowState &state, const Types::SizeLimits &limits) noexcept;
    [[nodiscard]] IO::Types::Status setAspectRatio(WindowState &state, std::optional<Types::AspectRatio> ratio) noexcept;
    [[nodiscard]] Types::PositionResult clientToScreen(const WindowState &state, Types::Position position) noexcept;
    [[nodiscard]] Types::PositionResult screenToClient(const WindowState &state, Types::Position position) noexcept;
    [[nodiscard]] IO::Types::Status show(WindowState &state) noexcept;
    [[nodiscard]] IO::Types::Status hide(WindowState &state) noexcept;
    [[nodiscard]] IO::Types::Status requestFocus(WindowState &state) noexcept;
    [[nodiscard]] IO::Types::Status requestAttention(WindowState &state) noexcept;
    [[nodiscard]] IO::Types::Status minimize(WindowState &state) noexcept;
    [[nodiscard]] IO::Types::Status maximize(WindowState &state) noexcept;
    [[nodiscard]] IO::Types::Status restore(WindowState &state) noexcept;
    [[nodiscard]] IO::Types::Status setMode(WindowState &state, const Types::ModeRequest &request) noexcept;
    [[nodiscard]] IO::Types::Status setResizable(WindowState &state, bool resizable) noexcept;
    [[nodiscard]] IO::Types::Status setDecorationMode(WindowState &state, Types::DecorationMode mode) noexcept;
    [[nodiscard]] IO::Types::Status setWindowControls(WindowState &state, const Types::WindowControls &controls) noexcept;
    [[nodiscard]] IO::Types::Status setFocusable(WindowState &state, bool focusable) noexcept;
    [[nodiscard]] IO::Types::Status setUserInteractionEnabled(WindowState &state, bool enabled) noexcept;
    [[nodiscard]] IO::Types::Status setAlwaysOnTop(WindowState &state, bool alwaysOnTop) noexcept;
    [[nodiscard]] IO::Types::Status setOpacity(WindowState &state, float opacity) noexcept;
    [[nodiscard]] IO::Types::Status setBackdropEffect(WindowState &state, Types::BackdropEffect effect) noexcept;
    [[nodiscard]] IO::Types::Status setFileDropEnabled(WindowState &state, bool enabled) noexcept;
    [[nodiscard]] IO::Types::Status setCustomChromeLayout(WindowState &state) noexcept;
    [[nodiscard]] IO::Types::Status setPointerInputLayout(WindowState &state) noexcept;
    [[nodiscard]] IO::Types::Status setCursorMode(WindowState &state, Types::CursorMode mode) noexcept;
    [[nodiscard]] IO::Types::Status setCursorShape(WindowState &state, Types::CursorShape shape) noexcept;
    [[nodiscard]] IO::Types::Status setCursorPosition(WindowState &state, Types::Position position) noexcept;
    [[nodiscard]] Types::PositionResult cursorPosition(const WindowState &state) noexcept;
} // namespace GameWIP::Window::Detail::Platform
