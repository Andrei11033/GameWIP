/// @file window_platform.h
/// @brief Internal portable-to-native backend contract for Window.

#pragma once

#include "window/renderer.h"
#include "window/internal/window_state.h"

#include <chrono>
#include <cstdint>
#include <span>
#include <string_view>

namespace GameWIP::Window::Detail::Platform
{
    /// @brief Backend-neutral native facts converted into the public display-color result.
    struct DisplayColorSnapshot
    {
        Types::DisplayColorSpace activeColorSpace = Types::DisplayColorSpace::Unknown; ///< Classified active output mode.
        bool wideColorGamutSupported = false;                                          ///< Native advanced-color support.
        bool hdrSupported = false;                                                     ///< Native HDR support independent of enablement.
        bool hdrEnabled = false;                                                       ///< Current operating-system HDR state.
        std::uint32_t bitsPerColorChannel = 0;                                         ///< Native precision before public saturation.
        float minimumLuminanceNits = 0.0F;                                             ///< Sanitized minimum luminance, or zero.
        float maximumLuminanceNits = 0.0F;                                             ///< Sanitized peak luminance, or zero.
        float maximumFullFrameLuminanceNits = 0.0F;                                    ///< Sanitized full-frame luminance, or zero.
        std::uint32_t sdrWhiteLevelMilli80Nits = 0;                                    ///< DisplayConfig white level in 1/1000 of 80 nits.
    };

    /// @brief Close result distinguishes retryable ownership from late cleanup diagnostics.
    struct CloseResult
    {
        IO::Types::Status status;    ///< Cleanup result or late diagnostic.
        bool resourceClosed = false; ///< Whether native ownership ended despite the status.
    };

    /// @brief Internal native handles represented without native public-header dependencies.
    struct NativeHandleView
    {
        void *instance = nullptr; ///< Non-owning platform module handle.
        void *window = nullptr;   ///< Non-owning native window handle.
    };

    /// @name Process and monitor operations
    /// These operations translate native failures into public result values and retain no
    /// caller-owned views after returning.
    /// @{
    [[nodiscard]] Types::CapabilitiesResult getCapabilities() noexcept;
    [[nodiscard]] Types::EventPumpResult pumpEvents(std::chrono::milliseconds timeout, bool wait) noexcept;
    [[nodiscard]] Types::MonitorListResult getMonitors() noexcept;
    [[nodiscard]] Types::MonitorInfoResult getPrimaryMonitor() noexcept;
    [[nodiscard]] Types::MonitorInfoResult getMonitor(Types::MonitorId monitor) noexcept;
    [[nodiscard]] Types::DisplayModeListResult getDisplayModes(Types::MonitorId monitor) noexcept;
    [[nodiscard]] Types::DisplayModeResult getCurrentDisplayMode(Types::MonitorId monitor) noexcept;
    [[nodiscard]] Types::DisplayModeResult getPreferredDisplayMode(Types::MonitorId monitor) noexcept;
    [[nodiscard]] Types::DisplayColorInfoResult getDisplayColorInfo(Types::MonitorId monitor) noexcept;
    [[nodiscard]] Types::DisplayColorInfo makeDisplayColorInfo(Types::MonitorId monitor, const DisplayColorSnapshot &snapshot) noexcept;
    [[nodiscard]] bool consumeDisplayColorConfigurationChange() noexcept;
    /// @}

    /// @name Native lifetime operations
    /// Open-state operations require the owning thread. Cleanup either leaves a retryable live
    /// resource, completes synchronously, or transfers the complete state owner to its dispatcher.
    /// @{
    [[nodiscard]] IO::Types::Status open(WindowState &state, const Types::Description &description) noexcept;
    [[nodiscard]] CloseResult close(WindowState &state) noexcept;
    void closeBestEffort(WindowState &state) noexcept;
    [[nodiscard]] bool deferCleanupToOwner(std::unique_ptr<WindowState> &state) noexcept;
    [[nodiscard]] bool isOwnedByCurrentThread(const WindowState &state) noexcept;
    [[nodiscard]] bool hasLiveNativeWindow(const WindowState &state) noexcept;
    [[nodiscard]] IO::Types::Status wakeEventWait(const WindowState &state) noexcept;
    [[nodiscard]] NativeHandleView nativeHandle(const WindowState &state) noexcept;
    /// @}

    /// @name Native state mutations
    /// Inputs are validated and copied by portable core code before this boundary. Backends update
    /// the authoritative WindowState only after the corresponding native transition succeeds.
    /// @{
    [[nodiscard]] IO::Types::Status setOwner(WindowState &state, Types::WindowId owner) noexcept;
    [[nodiscard]] IO::Types::Status setTitle(WindowState &state, std::string_view utf8Title) noexcept;
    [[nodiscard]] IO::Types::Status setIcon(WindowState &state, std::span<const Types::IconImageView> images) noexcept;
    [[nodiscard]] IO::Types::Status clearIcon(WindowState &state) noexcept;
    [[nodiscard]] IO::Types::Status setClientSize(WindowState &state, Types::LogicalSize size) noexcept;
    [[nodiscard]] IO::Types::Status setClientPosition(WindowState &state, Types::ScreenPosition position) noexcept;
    [[nodiscard]] IO::Types::Status setClientRect(WindowState &state, Types::ScreenPosition position, Types::LogicalSize size) noexcept;
    [[nodiscard]] IO::Types::Status centerOn(WindowState &state, Types::MonitorId monitor) noexcept;
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
    [[nodiscard]] IO::Types::Status setCursorPosition(WindowState &state, Types::LogicalPosition position) noexcept;
    [[nodiscard]] Types::LogicalPositionResult cursorPosition(const WindowState &state) noexcept;
    /// @}
} // namespace GameWIP::Window::Detail::Platform
