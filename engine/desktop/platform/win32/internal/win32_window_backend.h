/// @file win32_window_backend.h
/// @brief Shared private Win32 Window backend state and helper declarations.

#pragma once

#include "desktop/internal/desktop_test_hooks.h"

#include "desktop/internal/child_surface_state.h"
#include "desktop/internal/window_platform.h"

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

namespace GameWIP::Desktop::Detail::Platform
{
    /// @brief Backend-owned state for one native child-host HWND.
    struct ChildSurfaceData
    {
        ChildSurfaceState *owner = nullptr;
        HINSTANCE instance = nullptr;
        HWND handle = nullptr;
        DWORD ownerThreadId = 0;
        bool classReferenceHeld = false;
        bool registered = false;
        bool destroying = false;
    };

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
        Types::Display::Mode activeDisplayMode;
        bool hasSavedDisplayMode = false;
        bool exclusiveSuspended = false;
        bool exactDisplayMode = false;
        std::uint32_t modeTransitionDepth = 0; ///< Guards synchronous native messages while one mode transition owns geometry.

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

        DWORD threadId = 0;                                          ///< Native identity of the owning thread.
        std::vector<WindowState *> windows;                          ///< Non-owning registered states on this thread.
        std::vector<ChildSurfaceState *> childSurfaces;              ///< Lazily registered native child-host states.
        std::mutex deferredMutex;                                    ///< Synchronizes cross-thread cleanup transfer.
        std::unique_ptr<WindowState> deferredCleanupHead;            ///< Intrusive chain awaiting owner-thread cleanup.
        std::unique_ptr<ChildSurfaceState> deferredChildCleanupHead; ///< Child hosts awaiting owner-thread cleanup.
        bool pumping = false;                                        ///< Reentrancy guard for the native message pump.
        Types::Events::PumpResult *activeResult = nullptr;           ///< Call-scoped accumulator during a pump.
    };

    /// @name Dispatcher and routing helpers
    /// These helpers operate on the calling thread's dispatcher. Registered WindowState pointers
    /// remain valid until explicit unregister or deferred owner-thread cleanup.
    /// @{

    [[nodiscard]] Dispatcher &dispatcher() noexcept;
    [[nodiscard]] IO::Types::Status acquireWindowClass(HINSTANCE instance) noexcept;
    [[nodiscard]] IO::Types::Status releaseWindowClass() noexcept;
    void registerWindowId(WindowState &state);
    void unregisterWindowId(WindowState &state) noexcept;
    [[nodiscard]] DWORD styleFor(const WindowState &state) noexcept;
    [[nodiscard]] DWORD extendedStyleFor(const WindowState &state) noexcept;
    [[nodiscard]] UINT wakeMessage() noexcept;
    void routeEvent(WindowState &state, Types::Events::Payload data) noexcept;
    void recordPumpFailure(IO::Types::Status status) noexcept;
    void registerOpenState(WindowState &state);
    void unregisterOpenState(WindowState &state) noexcept;
    void registerOpenChildSurface(ChildSurfaceState &state);
    void unregisterOpenChildSurface(ChildSurfaceState &state) noexcept;
    void routeChildSurfaceEvent(ChildSurfaceState &state, Types::ChildSurface::Events::Payload data) noexcept;
    void refreshChildSurfaceScreenRect(ChildSurfaceState &state) noexcept;
    void refreshChildSurfaceScreenRectsForParent(Types::WindowId parentId) noexcept;
    void pruneAbandonedStates(Dispatcher &current) noexcept;
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
    void releaseCustomCursorBinding(HWND window) noexcept;
    void refreshCustomCursorForDpi(WindowState &state, std::uint32_t dpi) noexcept;
    [[nodiscard]] IO::Types::Status replaceCustomCursorWithSystem(WindowState &state, HCURSOR cursor) noexcept;
    [[nodiscard]] bool pointInRect(Types::LogicalPosition point, const Types::LogicalRect &rect) noexcept;

    [[nodiscard]] IO::Types::Status refreshCachedGeometry(WindowState &state) noexcept;
    void updateCurrentMonitor(WindowState &state) noexcept;
    [[nodiscard]] IO::Types::Status applyCursorState(WindowState &state) noexcept;
    [[nodiscard]] IO::Types::Status applyStyle(WindowState &state) noexcept;
    [[nodiscard]] IO::Types::Status placeFullscreenOnMonitor(WindowState &state, HMONITOR monitor, bool preserveZOrder = false) noexcept;
    [[nodiscard]] IO::Types::Status leaveExclusive(WindowState &state) noexcept;
    [[nodiscard]] IO::Types::Status suspendExclusive(WindowState &state) noexcept;
    [[nodiscard]] IO::Types::Status resumeExclusive(WindowState &state) noexcept;
    [[nodiscard]] IO::Types::Status applyMode(WindowState &state, const Types::ModeRequest &request) noexcept;
    [[nodiscard]] IO::Types::Status recoverAfterDisplayChange(WindowState &state, bool forceRemovedMonitor = false) noexcept;

    [[nodiscard]] Types::Display::InfoResult monitorFromNative(HMONITOR monitor) noexcept;
    [[nodiscard]] HMONITOR nativeMonitor(Types::Display::MonitorId id) noexcept;
    [[nodiscard]] std::wstring monitorDeviceName(Types::Display::MonitorId id) noexcept;
    [[nodiscard]] std::uint32_t runtimeWindowsBuild() noexcept;
    [[nodiscard]] bool supportsSystemBackdrop() noexcept;
    [[nodiscard]] bool supportsTransparentFramebuffer() noexcept;

    [[nodiscard]] LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
} // namespace GameWIP::Desktop::Detail::Platform
