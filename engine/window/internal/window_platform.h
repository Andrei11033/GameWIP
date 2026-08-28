/// @file window_platform.h
/// @brief Internal portable-to-native backend contract for Window.

#pragma once

#include "window/display_info.h"
#include "window/internal/window_state.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace GameWIP::Window::Detail::Platform
{
    struct DisplayColorSnapshot
    {
        Types::Display::ColorSpace activeColorSpace = Types::Display::ColorSpace::Unknown;
        bool wideColorGamutSupported = false;
        bool hdrSupported = false;
        bool hdrEnabled = false;
        std::uint32_t bitsPerColorChannel = 0;
        float minimumLuminanceNits = 0.0F;
        float maximumLuminanceNits = 0.0F;
        float maximumFullFrameLuminanceNits = 0.0F;
        std::uint32_t sdrWhiteLevelMilli80Nits = 0;
    };

    struct CloseResult
    {
        IO::Types::Status status;
        bool resourceClosed = false;
    };

    struct NativeHandleView
    {
        void *instance = nullptr;
        void *window = nullptr;
    };

    [[nodiscard]] Types::CapabilitiesResult getCapabilities() noexcept;
    [[nodiscard]] Types::Events::PumpResult pumpEvents(std::chrono::milliseconds timeout, bool wait) noexcept;
    [[nodiscard]] Types::Display::MonitorsResult getMonitors() noexcept;
    [[nodiscard]] Types::Display::InfoResult getPrimaryMonitor() noexcept;
    [[nodiscard]] Types::Display::InfoResult getMonitor(Types::Display::MonitorId monitor) noexcept;
    [[nodiscard]] Types::Display::ModesResult getModes(Types::Display::MonitorId monitor) noexcept;
    [[nodiscard]] Types::Display::ModeResult getCurrentMode(Types::Display::MonitorId monitor) noexcept;
    [[nodiscard]] Types::Display::ModeResult getPreferredMode(Types::Display::MonitorId monitor) noexcept;
    [[nodiscard]] Types::Display::ColorInfoResult getColorInfo(Types::Display::MonitorId monitor) noexcept;
    [[nodiscard]] Types::Display::ColorInfo makeDisplayColorInfo(Types::Display::MonitorId monitor, const DisplayColorSnapshot &snapshot) noexcept;
    [[nodiscard]] bool consumeDisplayColorConfigurationChange() noexcept;

    [[nodiscard]] IO::Types::Status open(WindowState &state, const Types::Description &description) noexcept;
    [[nodiscard]] CloseResult close(WindowState &state) noexcept;
    void closeBestEffort(WindowState &state) noexcept;
    [[nodiscard]] bool deferCleanupToOwner(std::unique_ptr<WindowState> &state) noexcept;
    [[nodiscard]] bool ownedByCurrentThread(const WindowState &state) noexcept;
    [[nodiscard]] bool hasLiveNativeWindow(const WindowState &state) noexcept;
    [[nodiscard]] IO::Types::Status wakeEventWait(const WindowState &state) noexcept;
    [[nodiscard]] NativeHandleView nativeHandle(const WindowState &state) noexcept;

    [[nodiscard]] IO::Types::Status setOwner(WindowState &state, Types::WindowId owner) noexcept;
    [[nodiscard]] IO::Types::Status setTitle(WindowState &state, std::string_view utf8Title) noexcept;
    [[nodiscard]] IO::Types::Status setIcon(WindowState &state, std::span<const Types::IconImageView> images) noexcept;
    [[nodiscard]] IO::Types::Status clearIcon(WindowState &state) noexcept;
    [[nodiscard]] IO::Types::Status setClientSize(WindowState &state, Types::LogicalSize size) noexcept;
    [[nodiscard]] IO::Types::Status setClientPosition(WindowState &state, Types::ScreenPosition position) noexcept;
    [[nodiscard]] IO::Types::Status setClientRect(WindowState &state, Types::ScreenPosition position, Types::LogicalSize size) noexcept;
    [[nodiscard]] IO::Types::Status centerOn(WindowState &state, Types::Display::MonitorId monitor) noexcept;
    [[nodiscard]] IO::Types::Status setSizeLimits(WindowState &state, const Types::SizeLimits &limits) noexcept;
    [[nodiscard]] IO::Types::Status setAspectRatio(WindowState &state, std::optional<Types::AspectRatio> ratio) noexcept;
    [[nodiscard]] Types::ScreenPositionResult clientToScreen(const WindowState &state, Types::LogicalPosition position) noexcept;
    [[nodiscard]] Types::LogicalPositionResult screenToClient(const WindowState &state, Types::ScreenPosition position) noexcept;
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
    [[nodiscard]] IO::Types::Status setControls(WindowState &state, const Types::Controls &controls) noexcept;
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
    [[nodiscard]] IO::Types::Status setCursorPosition(WindowState &state, Types::LogicalPosition position) noexcept;
    [[nodiscard]] Types::LogicalPositionResult cursorPosition(const WindowState &state) noexcept;
} // namespace GameWIP::Window::Detail::Platform
