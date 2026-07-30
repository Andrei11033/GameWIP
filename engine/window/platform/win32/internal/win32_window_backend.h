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
    inline constexpr UINT kBaselineDpi = 96;
    inline constexpr wchar_t kWindowClassName[] = L"GameWIP.Window.TopLevel";
    inline constexpr std::uint32_t kMaximumChromeRegions = 256;
    inline constexpr std::uint32_t kMaximumPointerRegions = 256;

    /// @brief Backend-owned state for one native HWND.
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
    struct Dispatcher
    {
        explicit Dispatcher(DWORD owningThreadId) noexcept;
        ~Dispatcher() noexcept;
        Dispatcher(const Dispatcher &) = delete;
        Dispatcher &operator=(const Dispatcher &) = delete;

        DWORD threadId = 0;
        std::vector<WindowState *> windows;
        std::mutex deferredMutex;
        std::unique_ptr<WindowState> deferredCleanupHead;
        bool pumping = false;
        Types::EventPumpResult *activeResult = nullptr;
    };

    [[nodiscard]] Dispatcher &dispatcher() noexcept;
    [[nodiscard]] UINT wakeMessage() noexcept;
    void routeEvent(WindowState &state, Types::EventData data) noexcept;
    void recordPumpFailure(IO::Types::Status status) noexcept;
    void registerOpenState(WindowState &state);
    void unregisterOpenState(WindowState &state) noexcept;
    [[nodiscard]] WindowState *resolveWindowId(Types::WindowId id) noexcept;

    [[nodiscard]] IO::Types::Status statusFromWin32(IO::Types::ErrorCode fallback, DWORD nativeCode, std::string_view operation) noexcept;
    [[nodiscard]] IO::Types::Status statusFromDisplayChange(LONG nativeCode, std::string_view operation) noexcept;
    [[nodiscard]] bool utf8ToUtf16(std::string_view text, std::wstring &output, DWORD &nativeCode);
    [[nodiscard]] bool utf16ToUtf8(std::wstring_view text, std::string &output, DWORD &nativeCode);
    [[nodiscard]] UINT dpiForWindow(HWND window) noexcept;
    [[nodiscard]] bool logicalToPhysicalChecked(std::int32_t value, UINT dpi, LONG &output) noexcept;
    [[nodiscard]] LONG logicalToPhysical(std::int32_t value, UINT dpi) noexcept;
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
