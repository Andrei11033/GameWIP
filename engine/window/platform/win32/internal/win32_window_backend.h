/// @file win32_window_backend.h
/// @brief Shared private Win32 Window backend state and helper declarations.

#pragma once

#include "window/internal/window_test_hooks.h"

#include "window/internal/window_platform.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <windows.h>
#include <windowsx.h>

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace GameWIP::Window::Detail::Platform
{
    // Backend sources predate the public domain split. Keep these aliases private to the Win32
    // implementation so public headers expose only the standardized Types tree while the native
    // implementation can be migrated independently from platform behavior changes.
    namespace Types
    {
        using WindowId = ::GameWIP::Window::Types::WindowId;
        using MonitorId = ::GameWIP::Window::Types::Display::MonitorId;
        using LogicalPosition = ::GameWIP::Window::Types::LogicalPosition;
        using LogicalSize = ::GameWIP::Window::Types::LogicalSize;
        using PixelSize = ::GameWIP::Window::Types::PixelSize;
        using LogicalRect = ::GameWIP::Window::Types::LogicalRect;
        using ScreenPosition = ::GameWIP::Window::Types::ScreenPosition;
        using ScreenRect = ::GameWIP::Window::Types::ScreenRect;
        using Insets = ::GameWIP::Window::Types::Insets;
        using ContentScale = ::GameWIP::Window::Types::ContentScale;
        using Dpi = ::GameWIP::Window::Types::Dpi;
        using SizeLimits = ::GameWIP::Window::Types::SizeLimits;
        using AspectRatio = ::GameWIP::Window::Types::AspectRatio;
        using WindowMode = ::GameWIP::Window::Types::Mode;
        using LifetimeState = ::GameWIP::Window::Types::LifetimeState;
        using PresentationState = ::GameWIP::Window::Types::PresentationState;
        using DecorationMode = ::GameWIP::Window::Types::DecorationMode;
        using PlacementKind = ::GameWIP::Window::Types::PlacementKind;
        using Placement = ::GameWIP::Window::Types::Placement;
        using DisplayMode = ::GameWIP::Window::Types::Display::Mode;
        using ModeRequest = ::GameWIP::Window::Types::ModeRequest;
        using FullscreenInfo = ::GameWIP::Window::Types::FullscreenInfo;
        using Controls = ::GameWIP::Window::Types::Controls;
        using WindowControls = Controls;
        using DpiResizePolicy = ::GameWIP::Window::Types::DpiResizePolicy;
        using CursorMode = ::GameWIP::Window::Types::CursorMode;
        using CursorShape = ::GameWIP::Window::Types::CursorShape;
        using PointerInputMode = ::GameWIP::Window::Types::PointerInputMode;
        using BackdropEffect = ::GameWIP::Window::Types::BackdropEffect;
        using Description = ::GameWIP::Window::Types::Description;
        using CustomChromeLayout = ::GameWIP::Window::Types::CustomChromeLayout;
        using PointerInputLayout = ::GameWIP::Window::Types::PointerInputLayout;
        using IconImageView = ::GameWIP::Window::Types::IconImageView;
        using MonitorInfo = ::GameWIP::Window::Types::Display::Info;
        using Capability = ::GameWIP::Window::Types::Capability;
        using Capabilities = ::GameWIP::Window::Types::Capabilities;
        using CloseRequestSource = ::GameWIP::Window::Types::Events::CloseRequestSource;
        using CloseRequestedEvent = ::GameWIP::Window::Types::Events::CloseRequested;
        using ClosedEvent = ::GameWIP::Window::Types::Events::NativeDestroyed;
        using VisibilityChangedEvent = ::GameWIP::Window::Types::Events::VisibilityChanged;
        using MovedEvent = ::GameWIP::Window::Types::Events::ClientPositionChanged;
        using ClientSizeChangedEvent = ::GameWIP::Window::Types::Events::ClientSizeChanged;
        using FramebufferSizeChangedEvent = ::GameWIP::Window::Types::Events::FramebufferSizeChanged;
        using FocusChangedEvent = ::GameWIP::Window::Types::Events::FocusChanged;
        using PresentationStateChangedEvent = ::GameWIP::Window::Types::Events::PresentationStateChanged;
        using ContentScaleChangedEvent = ::GameWIP::Window::Types::Events::ContentScaleChanged;
        using MonitorChangedEvent = ::GameWIP::Window::Types::Events::MonitorChanged;
        using ModeChangedEvent = ::GameWIP::Window::Types::Events::ModeChanged;
        using OwnerChangedEvent = ::GameWIP::Window::Types::Events::OwnerChanged;
        using DisplayConfigurationChangedEvent = ::GameWIP::Window::Types::Events::DisplayConfigurationChanged;
        using CursorPresenceChangedEvent = ::GameWIP::Window::Types::Events::CursorPresenceChanged;
        using FilesDroppedEvent = ::GameWIP::Window::Types::Events::FilesDropped;
        using OcclusionChangedEvent = ::GameWIP::Window::Types::Events::OcclusionChanged;
        using RedrawRequestedEvent = ::GameWIP::Window::Types::Events::RedrawRequested;
        using EventData = ::GameWIP::Window::Types::Events::Payload;
        using Event = ::GameWIP::Window::Types::Event;
        using EventStorageKind = ::GameWIP::Window::Types::Events::StorageKind;
        using EventQueueInfo = ::GameWIP::Window::Types::Events::QueueInfo;
        using EventPumpResult = ::GameWIP::Window::Types::Events::PumpResult;
        using CapabilitiesResult = ::GameWIP::Window::Types::CapabilitiesResult;
        using ScreenPositionResult = ::GameWIP::Window::Types::ScreenPositionResult;
        using LogicalPositionResult = ::GameWIP::Window::Types::LogicalPositionResult;
        using MonitorListResult = ::GameWIP::Window::Types::Display::MonitorsResult;
        using MonitorInfoResult = ::GameWIP::Window::Types::Display::InfoResult;
        using DisplayModeListResult = ::GameWIP::Window::Types::Display::ModesResult;
        using DisplayModeResult = ::GameWIP::Window::Types::Display::ModeResult;
        using DisplayColorSpace = ::GameWIP::Window::Types::Display::ColorSpace;
        using DisplayColorInfo = ::GameWIP::Window::Types::Display::ColorInfo;
        using DisplayColorInfoResult = ::GameWIP::Window::Types::Display::ColorInfoResult;
    } // namespace Types

    inline constexpr UINT kBaselineDpi = 96;                                  ///< Win32 logical-coordinate baseline.
    inline constexpr wchar_t kWindowClassName[] = L"GameWIP.Window.TopLevel"; ///< Process-wide registered class name.
    inline constexpr std::uint32_t kMaximumChromeRegions = 256;               ///< Copied custom-chrome region limit.
    inline constexpr std::uint32_t kMaximumPointerRegions = 256;              ///< Copied pointer-region limit.

    /// @brief Backend-owned state for one native HWND.
    /// @details The owning WindowState has a stable address while this record is registered.
    /// Native handles are released by the owner thread; icon and display-mode members record
    /// resources that must be restored or destroyed during rollback and close.
    struct WindowData
    {
        WindowState *owner = nullptr;
        HINSTANCE instance = nullptr;
        HWND handle = nullptr;
        DWORD ownerThreadId = 0;
        HCURSOR cursor = nullptr;
        HICON largeIcon = nullptr;
        HICON smallIcon = nullptr;
        bool classReferenceHeld = false;
        bool cursorClipApplied = false;
        bool cursorTracking = false;
        bool destroying = false;

        DWORD windowedStyle = 0;
        DWORD windowedExtendedStyle = 0;
        WINDOWPLACEMENT windowedPlacement{};
        bool hasWindowedPlacement = false;

        std::wstring exclusiveDevice;
        DEVMODEW savedDisplayMode{};
        DEVMODEW activeNativeDisplayMode{};
        Types::DisplayMode activeDisplayMode;
        bool hasSavedDisplayMode = false;
        bool exclusiveSuspended = false;
        bool exactDisplayMode = false;

        std::wstring utf16Scratch;
    };

    /// @brief One native message dispatcher per Window-owning thread.
    /// @details The thread-local dispatcher owns deferred cleanup transferred from wrong-thread
    /// destructors. deferredMutex protects only the transfer list; the Window list, pump state,
    /// and active result remain owner-thread-only.
    struct Dispatcher
    {
        explicit Dispatcher(DWORD owningThreadId) noexcept;
        ~Dispatcher() noexcept;
        Dispatcher(const Dispatcher &) = delete;
        Dispatcher &operator=(const Dispatcher &) = delete;

        DWORD threadId = 0;                               ///< Native identity of the owning thread.
        std::vector<WindowState *> windows;               ///< Non-owning registered states on this thread.
        std::mutex deferredMutex;                         ///< Synchronizes cross-thread cleanup transfer.
        std::unique_ptr<WindowState> deferredCleanupHead; ///< Intrusive chain awaiting owner-thread cleanup.
        bool pumping = false;                             ///< Reentrancy guard for the native message pump.
        Types::EventPumpResult *activeResult = nullptr;   ///< Call-scoped accumulator during a pump.
    };

    /// @name Dispatcher and routing helpers
    /// These helpers operate on the calling thread's dispatcher. Registered WindowState pointers
    /// remain valid until explicit unregister or deferred owner-thread cleanup.
    /// @{
    [[nodiscard]] Dispatcher &dispatcher() noexcept;
    [[nodiscard]] UINT wakeMessage() noexcept;
    void routeEvent(WindowState &state, Types::EventData data) noexcept;
    void recordPumpFailure(IO::Types::Status status) noexcept;
    void registerOpenState(WindowState &state);
    void unregisterOpenState(WindowState &state) noexcept;
    [[nodiscard]] WindowState *resolveWindowId(Types::WindowId id) noexcept;
    /// @}

    /// @name Native conversion helpers
    /// Conversion functions preserve the most useful Win32 code for translation at the portable
    /// boundary and never expose partially converted output as a successful result.
    /// @{
    [[nodiscard]] IO::Types::Status statusFromWin32(IO::Types::ErrorCode fallback, DWORD nativeCode, std::string_view operation) noexcept;
    [[nodiscard]] IO::Types::Status statusFromDisplayChange(LONG nativeCode, std::string_view operation) noexcept;
    [[nodiscard]] bool utf8ToUtf16(std::string_view text, std::wstring &output, DWORD &nativeCode);
    [[nodiscard]] bool utf16ToUtf8(std::wstring_view text, std::string &output, DWORD &nativeCode);
    [[nodiscard]] UINT dpiForWindow(HWND window) noexcept;
    [[nodiscard]] bool logicalToPhysicalChecked(std::int32_t value, UINT dpi, LONG &output) noexcept;
    [[nodiscard]] LONG logicalToPhysical(std::int32_t value, UINT dpi) noexcept;
    /// @}
    [[nodiscard]] std::int32_t physicalToLogical(LONG value, UINT dpi) noexcept;
    [[nodiscard]] Types::PixelSize logicalToPhysicalSize(Types::LogicalSize value, UINT dpi) noexcept;
    [[nodiscard]] Types::LogicalSize physicalToLogicalSize(std::uint32_t width, std::uint32_t height, UINT dpi) noexcept;
    [[nodiscard]] HCURSOR loadCursor(Types::CursorShape shape) noexcept;
    [[nodiscard]] bool pointInRect(Types::LogicalPosition point, const Types::LogicalRect &rect) noexcept;

    [[nodiscard]] IO::Types::Status refreshCachedGeometry(WindowState &state) noexcept;
    void updateCurrentMonitor(WindowState &state) noexcept;
    [[nodiscard]] IO::Types::Status applyCursorState(WindowState &state) noexcept;
    [[nodiscard]] IO::Types::Status applyStyle(WindowState &state) noexcept;
    [[nodiscard]] IO::Types::Status leaveExclusive(WindowState &state) noexcept;
    [[nodiscard]] IO::Types::Status suspendExclusive(WindowState &state) noexcept;
    [[nodiscard]] IO::Types::Status resumeExclusive(WindowState &state) noexcept;
    [[nodiscard]] IO::Types::Status applyMode(WindowState &state, const Types::ModeRequest &request) noexcept;
    [[nodiscard]] IO::Types::Status recoverAfterDisplayChange(WindowState &state, bool forceRemovedMonitor = false) noexcept;

    [[nodiscard]] Types::MonitorInfoResult monitorFromNative(HMONITOR monitor) noexcept;
    [[nodiscard]] HMONITOR nativeMonitor(Types::MonitorId id) noexcept;
    [[nodiscard]] std::wstring monitorDeviceName(Types::MonitorId id) noexcept;
    [[nodiscard]] std::uint32_t runtimeWindowsBuild() noexcept;
    [[nodiscard]] bool supportsSystemBackdrop() noexcept;
    [[nodiscard]] bool supportsTransparentFramebuffer() noexcept;

    [[nodiscard]] LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
} // namespace GameWIP::Window::Detail::Platform
