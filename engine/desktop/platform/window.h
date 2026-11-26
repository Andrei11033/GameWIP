/// @file window.h
/// @brief Portable backend contract for Window.

#pragma once

#include "desktop/display_info.h"
#include "desktop/internal/window_state.h"
#include "status/result.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace GameWIP::Desktop::Platform
{
    /// ------------------------------------------------------------
    /// Shared portable state
    /// ------------------------------------------------------------

    using Internal::WindowState;

    /// @brief Best-effort color metadata gathered from display configuration and native queries.
    struct DisplayColorSnapshot
    {
        // Color capabilities
        Types::Display::ColorSpace activeColorSpace = Types::Display::ColorSpace::Unknown;
        bool wideColorGamutSupported = false;
        bool hdrSupported = false;
        bool hdrEnabled = false;
        std::uint32_t bitsPerColorChannel = 0;

        // Luminance levels
        float minimumLuminanceNits = 0.0F;
        float maximumLuminanceNits = 0.0F;
        float maximumFullFrameLuminanceNits = 0.0F;
        std::uint32_t sdrWhiteLevelMilli80Nits = 0;
    };

    /// @brief Whether a close operation released its native resource.
    using CloseResult = GameWIP::Status::Types::Result<bool>;

    /// @brief Opaque native module/window pair exposed to native interoperability.
    struct NativeHandleView
    {
        void *instance = nullptr;
        void *window = nullptr;
    };

    /// ------------------------------------------------------------
    /// Process, display, and event services
    /// ------------------------------------------------------------

    [[nodiscard]] GameWIP::Status::Types::Result<Types::Capabilities> getCapabilities() noexcept;
    [[nodiscard]] Types::Events::PumpResult pumpEvents(std::chrono::milliseconds timeout, bool wait) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Result<std::vector<Types::Display::Info>> getMonitors() noexcept;
    [[nodiscard]] GameWIP::Status::Types::Result<Types::Display::Info> getPrimaryMonitor() noexcept;
    [[nodiscard]] GameWIP::Status::Types::Result<Types::Display::Info> getMonitor(Types::Display::MonitorId monitor) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Result<std::vector<Types::Display::Mode>> getModes(Types::Display::MonitorId monitor) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Result<Types::Display::Mode> getCurrentMode(Types::Display::MonitorId monitor) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Result<Types::Display::Mode> getPreferredMode(Types::Display::MonitorId monitor) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Result<Types::Display::ColorInfo> getColorInfo(Types::Display::MonitorId monitor) noexcept;
    [[nodiscard]] Types::Display::ColorInfo makeDisplayColorInfo(Types::Display::MonitorId monitor, const DisplayColorSnapshot &snapshot) noexcept;
    [[nodiscard]] bool consumeDisplayColorConfigurationChange() noexcept;
    /// @brief Releases display-color resources retained by the calling thread.
    void releaseDisplayColorResources() noexcept;
    /// @brief Drops process-reclaimable display-color state during owner-thread teardown.
    void abandonDisplayColorResources() noexcept;
    /// @brief Returns whether the calling thread owns an open top-level Window.
    [[nodiscard]] bool hasOpenWindowsOnCurrentThread() noexcept;

    /// ------------------------------------------------------------
    /// Window lifetime and ownership
    /// ------------------------------------------------------------

    [[nodiscard]] GameWIP::Status::Types::Status open(WindowState &state, const Types::Description &description) noexcept;
    [[nodiscard]] CloseResult close(WindowState &state) noexcept;
    void closeBestEffort(WindowState &state) noexcept;
    [[nodiscard]] bool deferCleanupToOwner(std::unique_ptr<WindowState> &state) noexcept;
    [[nodiscard]] bool ownedByCurrentThread(const WindowState &state) noexcept;
    [[nodiscard]] bool hasLiveNativeWindow(const WindowState &state) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Status wakeEventWait(const WindowState &state) noexcept;
    [[nodiscard]] NativeHandleView nativeHandle(const WindowState &state) noexcept;

    /// ------------------------------------------------------------
    /// Ownership, metadata, and geometry
    /// ------------------------------------------------------------

    [[nodiscard]] GameWIP::Status::Types::Status setOwner(WindowState &state, Types::WindowId owner) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Status setTitle(WindowState &state, std::string_view utf8Title) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Status setIcon(WindowState &state, std::span<const Types::IconImageView> images) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Status clearIcon(WindowState &state) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Status setClientSize(WindowState &state, Types::LogicalSize size) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Status setClientPosition(WindowState &state, Types::ScreenPosition position) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Status setClientRect(WindowState &state, Types::ScreenPosition position, Types::LogicalSize size) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Status centerOn(WindowState &state, Types::Display::MonitorId monitor) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Status setSizeLimits(WindowState &state, const Types::SizeLimits &limits) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Status setAspectRatio(WindowState &state, std::optional<Types::AspectRatio> ratio) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Result<Types::ScreenPosition> clientToScreen(const WindowState &state, Types::LogicalPosition position) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Result<Types::LogicalPosition> screenToClient(const WindowState &state, Types::ScreenPosition position) noexcept;

    /// ------------------------------------------------------------
    /// Visibility, mode, style, and cursor controls
    /// ------------------------------------------------------------

    [[nodiscard]] GameWIP::Status::Types::Status show(WindowState &state) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Status hide(WindowState &state) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Status requestFocus(WindowState &state) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Status requestAttention(WindowState &state) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Status minimize(WindowState &state) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Status maximize(WindowState &state) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Status restore(WindowState &state) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Status setWindowMode(WindowState &state, const Types::WindowModeRequest &request) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Status setResizable(WindowState &state, bool resizable) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Status setDecorationMode(WindowState &state, Types::DecorationMode mode) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Status setControls(WindowState &state, const Types::Controls &controls) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Status setFocusable(WindowState &state, bool focusable) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Status setUserInteractionEnabled(WindowState &state, bool enabled) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Status setAlwaysOnTop(WindowState &state, bool alwaysOnTop) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Status setOpacity(WindowState &state, float opacity) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Status setBackdropEffect(WindowState &state, Types::BackdropEffect effect) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Status setFileDropEnabled(WindowState &state, bool enabled) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Status setCustomChromeLayout(WindowState &state) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Status setPointerInputLayout(WindowState &state) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Status setCursorMode(WindowState &state, Types::CursorMode mode) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Status setCursorShape(WindowState &state, Types::CursorShape shape) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Status setCursorPosition(WindowState &state, Types::LogicalPosition position) noexcept;
    [[nodiscard]] GameWIP::Status::Types::Result<Types::LogicalPosition> cursorPosition(const WindowState &state) noexcept;
} // namespace GameWIP::Desktop::Platform
