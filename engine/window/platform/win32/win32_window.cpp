#include "window/window.h"
#include "window/internal/window_message_access.h"
#include "input/platform/win32/win32_input.h"

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
    namespace Win32Input = GameWIP::Input::Platform::Win32;
    namespace Win32Internal = GameWIP::WindowInternal;
    using Window = GameWIP::Window;
    using CursorMode = GameWIP::Window::CursorMode;
    using WindowDescription = GameWIP::Window::Description;
    using DisplayMode = GameWIP::Window::DisplayMode;
    using WindowInfo = GameWIP::Window::Info;
    using MonitorInfo = GameWIP::Window::MonitorInfo;
    using Rect = GameWIP::Window::Rect;
    using WindowEvent = GameWIP::Window::Event;
    using WindowEventType = GameWIP::Window::EventType;
    using WindowMode = GameWIP::Window::Mode;
    using WindowResult = GameWIP::Window::Result;
    using WindowRole = GameWIP::Window::Role;

    // DPI helpers

    constexpr unsigned int defaultDpi = 96;                              // Win32's baseline DPI.
    constexpr int unlimitedClientSize = std::numeric_limits<int>::max(); // Sentinel for unconstrained max client size.

    bool isModeAllowedForRole(WindowRole role, WindowMode mode)
    {
        switch (role)
        {
        case WindowRole::MainGame:
            return true;
        case WindowRole::SecondaryGameView:
            return mode == WindowMode::Windowed || mode == WindowMode::BorderlessFullscreen;
        case WindowRole::Tool:
        case WindowRole::Debug:
            return mode == WindowMode::Windowed;
        default:
            return false;
        }
    }

    bool isCursorConfinementAllowedForRole(WindowRole role)
    {
        return role == WindowRole::MainGame;
    }

    using AdjustWindowRectExForDpiFn = BOOL(WINAPI *)(LPRECT, DWORD, BOOL, DWORD, UINT);
    using GetDpiForWindowFn = UINT(WINAPI *)(HWND);
    using GetDpiForSystemFn = UINT(WINAPI *)();
    using SetProcessDpiAwarenessContextFn = BOOL(WINAPI *)(HANDLE);
    using SetProcessDPIAwareFn = BOOL(WINAPI *)();

    /// @brief Returns a function from user32.dll when the current OS provides it.
    /// @param name Export name to find.
    /// @return Function pointer as FARPROC, or nullptr if unavailable.
    FARPROC getUser32Function(const char *name)
    {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (user32 == nullptr)
        {
            return nullptr;
        }

        return GetProcAddress(user32, name);
    }

    /// @brief Cached function pointer for GetDpiForWindow, resolved on first call.
    GetDpiForWindowFn cachedGetDpiForWindow = nullptr;

    /// @brief Cached function pointer for GetDpiForSystem, resolved on first call.
    GetDpiForSystemFn cachedGetDpiForSystem = nullptr;

    /// @brief Cached function pointer for AdjustWindowRectExForDpi, resolved on first call.
    AdjustWindowRectExForDpiFn cachedAdjustWindowRectExForDpi = nullptr;

    /// @brief Cached function pointer for SetProcessDpiAwarenessContext, resolved on first call.
    SetProcessDpiAwarenessContextFn cachedSetProcessDpiAwarenessContext = nullptr;

    /// @brief Cached function pointer for SetProcessDPIAware, resolved on first call.
    SetProcessDPIAwareFn cachedSetProcessDPIAware = nullptr;

    /// @brief Returns the system DPI, falling back to standard 96 DPI.
    /// @return System DPI value.
    unsigned int getSystemDpi()
    {
        if (cachedGetDpiForSystem == nullptr)
        {
            cachedGetDpiForSystem = reinterpret_cast<GetDpiForSystemFn>(getUser32Function("GetDpiForSystem"));
        }
        if (cachedGetDpiForSystem != nullptr)
        {
            return cachedGetDpiForSystem();
        }

        return defaultDpi;
    }

    /// @brief Returns the DPI for a window, falling back to system DPI.
    /// @param handle Window handle to query.
    /// @return Window DPI value.
    unsigned int getWindowDpi(HWND handle)
    {
        if (cachedGetDpiForWindow == nullptr)
        {
            cachedGetDpiForWindow = reinterpret_cast<GetDpiForWindowFn>(getUser32Function("GetDpiForWindow"));
        }
        if (cachedGetDpiForWindow != nullptr && handle != nullptr)
        {
            return cachedGetDpiForWindow(handle);
        }

        return getSystemDpi();
    }

    /// @brief Adjusts a window rect around a desired client size.
    /// @param windowRect Rect holding desired client size on input and adjusted outer size on output.
    /// @param style Window style used for frame sizing.
    /// @param extendedStyle Extended window style used for frame sizing.
    /// @param dpi DPI used for frame sizing when the OS supports it.
    /// @return True when the rect was adjusted.
    bool adjustWindowRectForDpi(RECT &windowRect, DWORD style, DWORD extendedStyle, unsigned int dpi)
    {
        if (cachedAdjustWindowRectExForDpi == nullptr)
        {
            cachedAdjustWindowRectExForDpi = reinterpret_cast<AdjustWindowRectExForDpiFn>(getUser32Function("AdjustWindowRectExForDpi"));
        }
        if (cachedAdjustWindowRectExForDpi != nullptr)
        {
            return cachedAdjustWindowRectExForDpi(&windowRect, style, FALSE, extendedStyle, dpi) != FALSE;
        }

        return AdjustWindowRectEx(&windowRect, style, FALSE, extendedStyle) != FALSE;
    }

    /// @brief Enables DPI awareness for the process when the OS allows it.
    void enableDpiAwareness()
    {
        static bool attempted = false;
        if (attempted)
        {
            return;
        }
        attempted = true;

        if (cachedSetProcessDpiAwarenessContext == nullptr)
        {
            cachedSetProcessDpiAwarenessContext = reinterpret_cast<SetProcessDpiAwarenessContextFn>(getUser32Function("SetProcessDpiAwarenessContext"));
        }
        if (cachedSetProcessDpiAwarenessContext != nullptr)
        {
            HANDLE perMonitorAwareV2 = reinterpret_cast<HANDLE>(static_cast<intptr_t>(-4));
            HANDLE perMonitorAware = reinterpret_cast<HANDLE>(static_cast<intptr_t>(-3));
            if (cachedSetProcessDpiAwarenessContext(perMonitorAwareV2) || cachedSetProcessDpiAwarenessContext(perMonitorAware))
            {
                return;
            }
        }

        if (cachedSetProcessDPIAware == nullptr)
        {
            cachedSetProcessDPIAware = reinterpret_cast<SetProcessDPIAwareFn>(getUser32Function("SetProcessDPIAware"));
        }
        if (cachedSetProcessDPIAware != nullptr)
        {
            cachedSetProcessDPIAware();
        }
    }

    // Text helpers

    /// @brief Converts UTF-8 text into UTF-16 for Win32 W APIs.
    /// @param text UTF-8 text to convert.
    /// @param outText Receives UTF-16 text.
    /// @return True when conversion succeeded.
    bool utf8ToWide(std::string_view text, std::wstring &outText)
    {
        outText.clear();
        if (text.empty())
        {
            return true;
        }

        if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return false;
        }

        int sourceLength = static_cast<int>(text.size());
        int wideLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), sourceLength, nullptr, 0);
        if (wideLength <= 0)
        {
            return false;
        }

        outText.resize(static_cast<std::size_t>(wideLength));
        return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), sourceLength, outText.data(), wideLength) == wideLength;
    }

    /// @brief Converts null-terminated UTF-16 text from Win32 into UTF-8.
    /// @param text UTF-16 text to convert.
    /// @param outText Receives UTF-8 text.
    /// @return True when conversion succeeded.
    bool wideToUtf8(const wchar_t *text, std::string &outText)
    {
        outText.clear();
        if (text == nullptr || text[0] == L'\0')
        {
            return true;
        }

        int utf8Length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
        if (utf8Length <= 0)
        {
            return false;
        }

        std::string converted(static_cast<std::size_t>(utf8Length), '\0');
        if (WideCharToMultiByte(CP_UTF8, 0, text, -1, converted.data(), utf8Length, nullptr, nullptr) == 0)
        {
            return false;
        }

        if (!converted.empty() && converted.back() == '\0')
        {
            converted.pop_back();
        }

        outText = std::move(converted);
        return true;
    }

    /// @brief Adds two non-negative dimensions without overflowing int.
    /// @param value Base dimension.
    /// @param extra Extra frame dimension.
    /// @return Clamped sum.
    int addClamped(int value, int extra)
    {
        if (value > unlimitedClientSize - extra)
        {
            return unlimitedClientSize;
        }

        return value + extra;
    }

    /// @brief Clamps a positive client dimension to the active window constraints.
    /// @param value Requested client dimension.
    /// @param minValue Minimum allowed client dimension.
    /// @param maxValue Maximum allowed client dimension.
    /// @return Clamped client dimension.
    int clampClientDimension(int value, int minValue, int maxValue)
    {
        return std::clamp(value, minValue, maxValue);
    }

    /// @brief Converts Win32 monitor data into the public MonitorInfo shape.
    /// @param monitor Native monitor handle.
    /// @param outInfo Receives monitor info.
    /// @param outError Receives GetLastError when conversion fails.
    /// @return True when monitor info was read.
    bool buildMonitorInfo(HMONITOR monitor, MonitorInfo &outInfo, unsigned long &outError)
    {
        outInfo = {};
        outError = 0;

        if (monitor == nullptr)
        {
            outError = ERROR_INVALID_HANDLE;
            return false;
        }

        MONITORINFOEXW monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);
        if (GetMonitorInfoW(monitor, &monitorInfo) == FALSE)
        {
            outError = GetLastError();
            return false;
        }

        outInfo.handle = monitor;
        outInfo.workArea = Rect{
            monitorInfo.rcWork.left,
            monitorInfo.rcWork.top,
            monitorInfo.rcWork.right,
            monitorInfo.rcWork.bottom};
        outInfo.monitorArea = Rect{
            monitorInfo.rcMonitor.left,
            monitorInfo.rcMonitor.top,
            monitorInfo.rcMonitor.right,
            monitorInfo.rcMonitor.bottom};
        outInfo.isPrimary = (monitorInfo.dwFlags & MONITORINFOF_PRIMARY) != 0;

        if (!wideToUtf8(monitorInfo.szDevice, outInfo.deviceName))
        {
            outError = GetLastError();
            return false;
        }

        return true;
    }

    /// @brief Returns whether a display mode already exists in a list.
    /// @param modes Existing mode list.
    /// @param mode Mode to search for.
    /// @return True if an identical mode exists.
    bool containsDisplayMode(const std::vector<DisplayMode> &modes, const DisplayMode &mode)
    {
        for (const DisplayMode &existing : modes)
        {
            if (existing.width == mode.width &&
                existing.height == mode.height &&
                existing.refreshRate == mode.refreshRate &&
                existing.bitsPerPixel == mode.bitsPerPixel)
            {
                return true;
            }
        }

        return false;
    }

    /// @brief Returns whether a display mode contains enough data for an exact fullscreen mode change.
    /// @param mode Display mode to validate.
    /// @return True when every mode field is positive.
    bool isCompleteDisplayMode(const DisplayMode &mode)
    {
        return mode.width > 0 &&
               mode.height > 0 &&
               mode.refreshRate > 0 &&
               mode.bitsPerPixel > 0;
    }

    /// @brief Selects the highest-refresh display mode for a requested resolution.
    /// @param modes Supported monitor display modes.
    /// @param width Requested display width.
    /// @param height Requested display height.
    /// @param outMode Receives the best mode when one exists.
    /// @return True when a supported mode matched the requested resolution.
    bool chooseHighestRefreshDisplayMode(const std::vector<DisplayMode> &modes, int width, int height, DisplayMode &outMode)
    {
        outMode = {};

        for (const DisplayMode &mode : modes)
        {
            if (!isCompleteDisplayMode(mode) || mode.width != width || mode.height != height)
            {
                continue;
            }

            if (!isCompleteDisplayMode(outMode) ||
                mode.refreshRate > outMode.refreshRate ||
                (mode.refreshRate == outMode.refreshRate && mode.bitsPerPixel > outMode.bitsPerPixel))
            {
                outMode = mode;
            }
        }

        return isCompleteDisplayMode(outMode);
    }

    /// @brief Writes a public DisplayMode into a Win32 DEVMODE.
    /// @param devMode DEVMODE to update.
    /// @param mode Display mode values to write.
    /// @param exactMode True to include refresh rate and bits per pixel.
    void applyDisplayModeToDevMode(DEVMODEW &devMode, const DisplayMode &mode, bool exactMode)
    {
        devMode.dmPelsWidth = static_cast<DWORD>(mode.width);
        devMode.dmPelsHeight = static_cast<DWORD>(mode.height);
        devMode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;

        if (exactMode)
        {
            devMode.dmDisplayFrequency = static_cast<DWORD>(mode.refreshRate);
            devMode.dmBitsPerPel = static_cast<DWORD>(mode.bitsPerPixel);
            devMode.dmFields |= DM_DISPLAYFREQUENCY | DM_BITSPERPEL;
        }
    }

    /// @brief Converts a Win32 DEVMODE into the public DisplayMode shape.
    /// @param devMode Win32 display mode.
    /// @return Public display mode values.
    DisplayMode displayModeFromDevMode(const DEVMODEW &devMode)
    {
        return DisplayMode{
            static_cast<int>(devMode.dmPelsWidth),
            static_cast<int>(devMode.dmPelsHeight),
            static_cast<int>(devMode.dmDisplayFrequency),
            static_cast<int>(devMode.dmBitsPerPel)};
    }

    /// @brief Returns whether newer events of this type can replace older queued data.
    /// @param type Event type to check.
    /// @return True when keeping only the latest tail event is enough.
    bool isWindowEventCoalesable(WindowEventType type)
    {
        return type == WindowEventType::Resized ||
               type == WindowEventType::Moved ||
               type == WindowEventType::MonitorChanged ||
               type == WindowEventType::DisplayChanged ||
               type == WindowEventType::DpiChanged;
    }

    bool isStartupStateEvent(WindowEventType type)
    {
        return type == WindowEventType::Resized ||
               type == WindowEventType::ModeChanged;
    }

    /// @brief Checks whether the cursor is over a window's client area.
    /// @param handle The handle to the window.
    /// @return True if the cursor is over the client area, false otherwise.
    bool isCursorOverClientArea(HWND handle)
    {
        POINT cursorPosition{};
        if (!GetCursorPos(&cursorPosition))
        {
            return false;
        }

        POINT clientPosition = cursorPosition;
        if (!ScreenToClient(handle, &clientPosition))
        {
            return false;
        }

        RECT clientRect{};
        if (!GetClientRect(handle, &clientRect))
        {
            return false;
        }

        return PtInRect(&clientRect, clientPosition) != 0;
    }

    // Native callback

    struct QueuedWindowEvent // Compact event representation used by the internal ring buffer.
    {
        WindowEventType type = WindowEventType::Resized;
        int width = 0;
        int height = 0;
        int x = 0;
        int y = 0;
        unsigned int dpi = 0;
        WindowMode mode = WindowMode::Windowed;
        std::size_t filePayloadIndex = static_cast<std::size_t>(-1);
    };

    Window *windowFromHandle(HWND hwnd)
    {
        return reinterpret_cast<Window *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    void setWindowForHandle(HWND hwnd, Window *window)
    {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
    }

    DWORD getWindowStyle(HWND handle)
    {
        return static_cast<DWORD>(GetWindowLongPtrW(handle, GWL_STYLE));
    }

    DWORD getWindowExtendedStyle(HWND handle)
    {
        return static_cast<DWORD>(GetWindowLongPtrW(handle, GWL_EXSTYLE));
    }

    bool setWindowStyle(HWND handle, DWORD style, unsigned long &outError)
    {
        SetLastError(0);
        LONG_PTR previousStyle = SetWindowLongPtrW(handle, GWL_STYLE, static_cast<LONG_PTR>(style));
        if (previousStyle == 0)
        {
            outError = GetLastError();
            return outError == 0;
        }

        outError = 0;
        return true;
    }

    bool setWindowExtendedStyle(HWND handle, DWORD extendedStyle, unsigned long &outError)
    {
        SetLastError(0);
        LONG_PTR previousExtendedStyle = SetWindowLongPtrW(handle, GWL_EXSTYLE, static_cast<LONG_PTR>(extendedStyle));
        if (previousExtendedStyle == 0)
        {
            outError = GetLastError();
            return outError == 0;
        }

        outError = 0;
        return true;
    }

    void restoreDisplayModeForDevice(const wchar_t *deviceName, DEVMODEW savedDisplayMode)
    {
        ChangeDisplaySettingsExW(deviceName, &savedDisplayMode, nullptr, 0, nullptr);
    }

    /// @brief platform window procedure to handle messages for the game window.
    /// @param hwnd Handle to the window receiving the message.
    /// @param message Win32 message identifier.
    /// @param wParam Message-specific parameter.
    /// @param lParam Message-specific parameter.
    /// @return Message-specific result value.
    LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        // Creation and close
        case WM_NCCREATE:
        {
            auto createStruct = reinterpret_cast<CREATESTRUCTW *>(lParam);
            auto window = reinterpret_cast<Window *>(createStruct->lpCreateParams);

            // Store the Window pointer so later messages can reach the owning object.
            setWindowForHandle(hwnd, window);

            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
        case WM_CLOSE:
        {
            Window *window = windowFromHandle(hwnd);
            if (window != nullptr)
            {
                window->requestClose();
            }
            else
            {
                DestroyWindow(hwnd);
            }
            return 0;
        }
        case WM_DESTROY:
        {
            Window *window = windowFromHandle(hwnd);
            if (window != nullptr)
            {
                Win32Internal::MessageAccess::handleVisibilityChange(*window, false);
                Win32Internal::MessageAccess::handleCursorLeave(*window);
                Win32Internal::MessageAccess::handleDestroyed(*window);
            }
            return 0;
        }
        // Size and position
        case WM_SIZE:
        {
            Window *window = windowFromHandle(hwnd);
            int width = LOWORD(lParam);
            int height = HIWORD(lParam);
            if (window != nullptr)
            {
                if (wParam == SIZE_MINIMIZED)
                {
                    Win32Internal::MessageAccess::handleMinimizeChange(*window, true);
                    Win32Internal::MessageAccess::handleVisibilityChange(*window, false);
                }
                else
                {
                    Win32Internal::MessageAccess::handleMinimizeChange(*window, false);
                    Win32Internal::MessageAccess::handleMaximizeChange(*window, wParam == SIZE_MAXIMIZED);
                    Win32Internal::MessageAccess::handleVisibilityChange(*window, true);
                    Win32Internal::MessageAccess::handleResize(*window, width, height);
                }
            }
            return 0;
        }
        case WM_MOVE:
        {
            Window *window = windowFromHandle(hwnd);
            if (window != nullptr)
            {
                int x = GET_X_LPARAM(lParam);
                int y = GET_Y_LPARAM(lParam);
                if (!IsMinimized(hwnd))
                {
                    Win32Internal::MessageAccess::handleMove(*window, x, y);
                }
                Win32Internal::MessageAccess::updateCurrentMonitor(*window);
            }
            return 0;
        }
        case WM_SHOWWINDOW:
        {
            Window *window = windowFromHandle(hwnd);
            if (window != nullptr)
            {
                Win32Internal::MessageAccess::handleVisibilityChange(*window, wParam != FALSE);
            }
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
        case WM_DPICHANGED:
        {
            Window *window = windowFromHandle(hwnd);
            auto suggestedRect = reinterpret_cast<RECT *>(lParam);
            if (window != nullptr && suggestedRect != nullptr)
            {
                unsigned int dpi = static_cast<unsigned int>(HIWORD(wParam));
                Win32Internal::MessageAccess::handleDpiChange(
                    *window,
                    dpi,
                    suggestedRect->left,
                    suggestedRect->top,
                    suggestedRect->right,
                    suggestedRect->bottom);
            }
            return 0;
        }
        case WM_DISPLAYCHANGE:
        {
            Window *window = windowFromHandle(hwnd);
            if (window != nullptr)
            {
                Win32Internal::MessageAccess::handleDisplayChange(*window);
            }
            return 0;
        }
        case WM_GETMINMAXINFO:
        {
            Window *window = windowFromHandle(hwnd);
            auto minMaxInfo = reinterpret_cast<MINMAXINFO *>(lParam);
            if (window != nullptr && minMaxInfo != nullptr)
            {
                Win32Internal::MessageAccess::handleGetMinMaxInfo(*window, minMaxInfo);
            }
            return 0;
        }
        // Focus
        case WM_ACTIVATEAPP:
        {
            Window *window = windowFromHandle(hwnd);
            if (window != nullptr)
            {
                Win32Internal::MessageAccess::handleActivationChange(*window, wParam != FALSE);
            }
            return 0;
        }
        case WM_SETFOCUS:
        {
            Window *window = windowFromHandle(hwnd);
            if (window != nullptr)
            {
                Win32Internal::MessageAccess::handleFocusChange(*window, true);
            }
            return 0;
        }
        case WM_KILLFOCUS:
        {
            Window *window = windowFromHandle(hwnd);
            if (window != nullptr)
            {
                Win32Internal::MessageAccess::handleFocusChange(*window, false);
            }
            return 0;
        }
        // Cursor
        case WM_MOUSEMOVE:
        {
            Window *window = windowFromHandle(hwnd);
            if (window != nullptr)
            {
                if (Win32Internal::MessageAccess::shouldHandleCursorEnter(*window))
                {
                    TRACKMOUSEEVENT trackMouseEvent{};
                    trackMouseEvent.cbSize = sizeof(trackMouseEvent);
                    trackMouseEvent.dwFlags = TME_LEAVE;
                    trackMouseEvent.hwndTrack = hwnd;
                    if (!TrackMouseEvent(&trackMouseEvent))
                    {
                        Win32Internal::MessageAccess::handleCursorTrackingFailure(*window, GetLastError());
                    }
                    else
                    {
                        Win32Internal::MessageAccess::handleCursorEnter(*window);
                    }
                }
            }

            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
        case WM_MOUSELEAVE:
        {
            Window *window = windowFromHandle(hwnd);
            if (window != nullptr)
            {
                Win32Internal::MessageAccess::handleCursorLeave(*window);
            }
            return 0;
        }
        case WM_SETCURSOR:
        {
            Window *window = windowFromHandle(hwnd);
            if (window != nullptr && LOWORD(lParam) == HTCLIENT)
            {
                SetCursor(window->isCursorVisible() ? Win32Internal::MessageAccess::getCursorHandle(*window) : nullptr);
                return TRUE;
            }

            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
        case WM_DROPFILES:
        {
            Window *window = windowFromHandle(hwnd);
            HDROP dropHandle = reinterpret_cast<HDROP>(wParam);
            if (window != nullptr && dropHandle != nullptr)
            {
                UINT fileCount = DragQueryFileW(dropHandle, 0xFFFFFFFF, nullptr, 0);
                for (UINT fileIndex = 0; fileIndex < fileCount; ++fileIndex)
                {
                    UINT filePathLength = DragQueryFileW(dropHandle, fileIndex, nullptr, 0);
                    std::wstring filePath(static_cast<std::size_t>(filePathLength) + 1, L'\0');
                    if (DragQueryFileW(dropHandle, fileIndex, filePath.data(), filePathLength + 1) == filePathLength)
                    {
                        std::string utf8Path;
                        if (wideToUtf8(filePath.c_str(), utf8Path))
                        {
                            Win32Internal::MessageAccess::handleFileDrop(*window, utf8Path);
                        }
                    }
                }
            }

            if (dropHandle != nullptr)
            {
                DragFinish(dropHandle);
            }
            return 0;
        }
        case WM_UNICHAR:
            if (wParam == UNICODE_NOCHAR)
            {
                return TRUE;
            }
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
    }

    struct MonitorEnumerationContext
    {
        std::vector<MonitorInfo> *monitors = nullptr;
        unsigned long error = 0;
    };

    /// @brief Callback function for enumerating monitors.
    /// @param hMonitor Handle to the monitor being enumerated.
    /// @param hdcMonitor Handle to a device context for the monitor being enumerated.
    /// @param lprcMonitor Pointer to the monitor rectangle supplied by Win32.
    /// @param dwData Pointer to user-defined data.
    /// @return TRUE to continue enumeration, FALSE to stop.
    BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData)
    {
        (void)hdcMonitor;
        (void)lprcMonitor;

        auto context = reinterpret_cast<MonitorEnumerationContext *>(dwData);
        if (context == nullptr || context->monitors == nullptr)
        {
            return FALSE;
        }

        MonitorInfo info{};
        unsigned long error = 0;
        if (!buildMonitorInfo(hMonitor, info, error))
        {
            context->error = error;
            return FALSE;
        }

        context->monitors->push_back(std::move(info));
        return TRUE;
    }
}

namespace GameWIP
{
    struct WindowEventQueue
    {
        static constexpr std::size_t invalidFilePayloadIndex = static_cast<std::size_t>(-1);

        std::vector<QueuedWindowEvent> events;
        std::vector<std::string> fileDropPayloads;
        std::vector<std::size_t> freeFileDropPayloadIndices;
        std::size_t head = 0;
        std::size_t count = 0;
        std::size_t queuedFileDropPayloadCount = 0;

        void init(std::size_t capacity)
        {
            events.assign(capacity, QueuedWindowEvent{});
            fileDropPayloads.clear();
            freeFileDropPayloadIndices.clear();
            queuedFileDropPayloadCount = 0;
            head = 0;
            count = 0;
        }

        bool empty() const
        {
            return count == 0;
        }

        std::size_t size() const
        {
            return count;
        }

        std::size_t capacity() const
        {
            return events.size();
        }

        QueuedWindowEvent &eventAt(std::size_t index)
        {
            return events[(head + index) % capacity()];
        }

        const QueuedWindowEvent &eventAt(std::size_t index) const
        {
            return events[(head + index) % capacity()];
        }

        void clear()
        {
            head = 0;
            count = 0;
            std::vector<std::string>().swap(fileDropPayloads);
            std::vector<std::size_t>().swap(freeFileDropPayloadIndices);
            queuedFileDropPayloadCount = 0;
        }

        void releaseFileDropPayload(const QueuedWindowEvent &event)
        {
            if (event.type != WindowEventType::FileDropped || event.filePayloadIndex == invalidFilePayloadIndex)
            {
                return;
            }

            if (event.filePayloadIndex < fileDropPayloads.size())
            {
                std::string().swap(fileDropPayloads[event.filePayloadIndex]);
            }

            if (queuedFileDropPayloadCount > 0)
            {
                --queuedFileDropPayloadCount;
            }

            if (queuedFileDropPayloadCount == 0)
            {
                std::vector<std::string>().swap(fileDropPayloads);
                std::vector<std::size_t>().swap(freeFileDropPayloadIndices);
            }
            else if (event.filePayloadIndex < fileDropPayloads.size())
            {
                freeFileDropPayloadIndices.push_back(event.filePayloadIndex);
            }
        }

        void moveToPublicEvent(const QueuedWindowEvent &queuedEvent, WindowEvent &outEvent)
        {
            outEvent = {};
            std::string().swap(outEvent.filePath);
            outEvent.type = queuedEvent.type;
            outEvent.width = queuedEvent.width;
            outEvent.height = queuedEvent.height;
            outEvent.x = queuedEvent.x;
            outEvent.y = queuedEvent.y;
            outEvent.dpi = queuedEvent.dpi;
            outEvent.mode = queuedEvent.mode;

            if (queuedEvent.type == WindowEventType::FileDropped &&
                queuedEvent.filePayloadIndex != invalidFilePayloadIndex &&
                queuedEvent.filePayloadIndex < fileDropPayloads.size())
            {
                outEvent.filePath = std::move(fileDropPayloads[queuedEvent.filePayloadIndex]);
            }
        }

        bool popFront(WindowEvent &outEvent)
        {
            if (empty())
            {
                return false;
            }

            QueuedWindowEvent queuedEvent = eventAt(0);
            moveToPublicEvent(queuedEvent, outEvent);
            releaseFileDropPayload(queuedEvent);
            head = (head + 1) % capacity();
            --count;
            return true;
        }

        void discardFront()
        {
            if (empty())
            {
                return;
            }

            releaseFileDropPayload(eventAt(0));
            head = (head + 1) % capacity();
            --count;
        }

        void pushBack(WindowEvent &&event)
        {
            QueuedWindowEvent queuedEvent{
                .type = event.type,
                .width = event.width,
                .height = event.height,
                .x = event.x,
                .y = event.y,
                .dpi = event.dpi,
                .mode = event.mode,
                .filePayloadIndex = invalidFilePayloadIndex};

            if (event.type == WindowEventType::FileDropped)
            {
                if (!freeFileDropPayloadIndices.empty())
                {
                    queuedEvent.filePayloadIndex = freeFileDropPayloadIndices.back();
                    freeFileDropPayloadIndices.pop_back();
                    fileDropPayloads[queuedEvent.filePayloadIndex] = std::move(event.filePath);
                }
                else
                {
                    queuedEvent.filePayloadIndex = fileDropPayloads.size();
                    fileDropPayloads.push_back(std::move(event.filePath));
                }
                ++queuedFileDropPayloadCount;
            }

            events[(head + count) % capacity()] = queuedEvent;
            ++count;
        }

        void removeAt(std::size_t index)
        {
            releaseFileDropPayload(eventAt(index));
            for (std::size_t i = index; i + 1 < count; ++i)
            {
                eventAt(i) = eventAt(i + 1);
            }
            --count;
        }

        std::size_t findOldestCoalescableEventIndex() const
        {
            for (std::size_t i = 0; i < count; ++i)
            {
                if (isWindowEventCoalesable(eventAt(i).type))
                {
                    return i;
                }
            }
            return static_cast<std::size_t>(-1);
        }

        bool tryCoalesce(const WindowEvent &event)
        {
            for (std::size_t i = count; i-- > 0;)
            {
                const QueuedWindowEvent &queuedEvent = eventAt(i);
                if (!isWindowEventCoalesable(queuedEvent.type))
                {
                    break;
                }
                if (queuedEvent.type == event.type)
                {
                    QueuedWindowEvent &target = eventAt(i);
                    target.width = event.width;
                    target.height = event.height;
                    target.x = event.x;
                    target.y = event.y;
                    target.dpi = event.dpi;
                    target.mode = event.mode;
                    return true;
                }
            }
            return false;
        }
    };

    struct Window::NativeWindow
    {
        // === Native handles ===
        HINSTANCE instance = nullptr;
        HWND handle = nullptr;

        // === Window state ===
        WindowMode mode = WindowMode::Windowed;
        WindowRole role = WindowRole::MainGame;
        bool closeRequested = false;
        bool isFocused = false;
        bool isMinimized = false;
        bool isMaximized = false;
        bool isVisible = false;
        bool isSuspended = false;
        bool clientSizeChanged = false;
        bool suppressStartupEvents = false;

        // === Client size and DPI ===
        int clientWidth = 0;
        int clientHeight = 0;

        // The requested size is the game's target resolution; the live client size may differ after user resizing.
        int requestedClientWidth = 0;
        int requestedClientHeight = 0;
        unsigned int dpi = defaultDpi;

        // === Client size constraints ===
        int minClientWidth = 0;
        int minClientHeight = 0;
        int maxClientWidth = unlimitedClientSize;
        int maxClientHeight = unlimitedClientSize;

        // === Windowed mode state ===
        RECT windowedRect{};
        DWORD windowedStyle = 0;
        DWORD windowedExtendedStyle = 0;
        bool hasSavedWindowedPlacement = false;

        // === Exclusive fullscreen mode state ===
        wchar_t fullscreenDeviceName[CCHDEVICENAME]{};
        DEVMODEW savedDisplayMode{};
        MonitorInfo requestedFullscreenMonitor{};
        DisplayMode requestedFullscreenDisplayMode{};
        MonitorInfo activeFullscreenMonitor{};
        DisplayMode activeFullscreenDisplayMode{};
        RECT fullscreenRect{};
        bool hasRequestedFullscreenDisplayMode = false;
        bool activeFullscreenModeIsExact = false;
        bool hasSavedDisplayMode = false;

        // Focus loss temporarily restores the desktop mode while preserving the fullscreen request.
        bool fullscreenSuspended = false;
        int fullscreenWidth = 0;
        int fullscreenHeight = 0;

        // === Cursor state ===
        HCURSOR arrowCursor = nullptr;
        bool cursorVisible = true;
        bool cursorConfined = false;
        bool cursorClipApplied = false;
        bool cursorInClient = false;

        // === Monitor and display caches ===
        std::vector<MonitorInfo> monitorCache;
        std::unordered_map<std::string, std::vector<DisplayMode>> displayModeCache;
        HMONITOR currentMonitor = nullptr;

        // === Events queue ===
        WindowEventQueue events{};

        // === Scratch buffers for repeated conversions ===
        std::wstring utf16Scratch;

        // === Window title caching ===
        std::string title;
    };

    struct Window::FullscreenModeSnapshot
    {
        wchar_t deviceName[CCHDEVICENAME]{};
        DEVMODEW savedDisplayMode{};
        MonitorInfo activeMonitor{};
        DisplayMode activeDisplayMode{};
        RECT rect{};
        bool hasSavedDisplayMode = false;
        bool activeModeIsExact = false;
        bool suspended = false;
        int width = 0;
        int height = 0;
    };

    struct Window::FullscreenModeTarget
    {
        HMONITOR monitor = nullptr;
        MONITORINFOEXW monitorInfo{};
        wchar_t deviceName[CCHDEVICENAME]{};
        MonitorInfo publicMonitorInfo{};
        DEVMODEW savedDisplayMode{};
        DisplayMode displayMode{};
        bool savedModeIsFromDifferentDevice = false;
    };

    namespace WindowInternal
    {
        // Native message bridge

        void MessageAccess::handleResize(Window &window, int width, int height)
        {
            window.handleResize(width, height);
        }

        void MessageAccess::handleMove(Window &window, int x, int y)
        {
            window.handleMove(x, y);
        }

        void MessageAccess::handleFocusChange(Window &window, bool focused)
        {
            window.handleFocusChange(focused);
        }

        void MessageAccess::handleActivationChange(Window &window, bool active)
        {
            window.handleActivationChange(active);
        }

        void MessageAccess::handleMinimizeChange(Window &window, bool minimized)
        {
            window.handleMinimizeChange(minimized);
        }

        void MessageAccess::handleMaximizeChange(Window &window, bool maximized)
        {
            window.handleMaximizeChange(maximized);
        }

        void MessageAccess::handleVisibilityChange(Window &window, bool visible)
        {
            window.handleVisibilityChange(visible);
        }

        bool MessageAccess::shouldHandleCursorEnter(const Window &window)
        {
            return window.shouldHandleCursorEnter();
        }

        bool MessageAccess::handleCursorEnter(Window &window)
        {
            return window.handleCursorEnter();
        }

        void MessageAccess::handleCursorTrackingFailure(Window &window, unsigned long win32Error)
        {
            window.handleCursorTrackingFailure(win32Error);
        }

        void MessageAccess::handleCursorLeave(Window &window)
        {
            window.handleCursorLeave();
        }

        void MessageAccess::handleFileDrop(Window &window, std::string_view filePath)
        {
            window.handleFileDrop(filePath);
        }

        void MessageAccess::handleDestroyed(Window &window)
        {
            window.handleDestroyed();
        }

        void MessageAccess::updateCurrentMonitor(Window &window)
        {
            window.updateCurrentMonitor();
        }

        void MessageAccess::handleDpiChange(Window &window, unsigned int dpi, int suggestedLeft, int suggestedTop, int suggestedRight, int suggestedBottom)
        {
            window.handleDpiChange(dpi, suggestedLeft, suggestedTop, suggestedRight, suggestedBottom);
        }

        void MessageAccess::handleDisplayChange(Window &window)
        {
            window.handleDisplayChange();
        }

        void MessageAccess::handleGetMinMaxInfo(Window &window, MINMAXINFO *minMaxInfo)
        {
            window.handleGetMinMaxInfo(minMaxInfo);
        }

        HCURSOR MessageAccess::getCursorHandle(const Window &window)
        {
            if (window.nativeWindow == nullptr)
            {
                return nullptr;
            }

            return window.nativeWindow->arrowCursor;
        }
    }

    void Window::handleGetMinMaxInfo(void *minMaxInfo)
    {
        if (nativeWindow == nullptr || minMaxInfo == nullptr)
        {
            return;
        }

        if (nativeWindow->mode != WindowMode::Windowed)
        {
            return;
        }

        bool hasMinWidth = nativeWindow->minClientWidth > 0;
        bool hasMinHeight = nativeWindow->minClientHeight > 0;
        bool hasMaxWidth = nativeWindow->maxClientWidth != unlimitedClientSize;
        bool hasMaxHeight = nativeWindow->maxClientHeight != unlimitedClientSize;
        if (!hasMinWidth && !hasMinHeight && !hasMaxWidth && !hasMaxHeight)
        {
            return;
        }

        MINMAXINFO *info = reinterpret_cast<MINMAXINFO *>(minMaxInfo);

        DWORD windowStyle = nativeWindow->windowedStyle;
        DWORD extendedWindowStyle = nativeWindow->windowedExtendedStyle;
        if (nativeWindow->handle != nullptr)
        {
            windowStyle = getWindowStyle(nativeWindow->handle);
            extendedWindowStyle = getWindowExtendedStyle(nativeWindow->handle);
        }

        RECT frameRect{0, 0, 0, 0};
        if (!adjustWindowRectForDpi(frameRect, windowStyle, extendedWindowStyle, nativeWindow->dpi))
        {
            recordAsyncError(WindowResult::PlatformCallFailed, GetLastError());
            return;
        }

        int frameWidth = frameRect.right - frameRect.left;
        int frameHeight = frameRect.bottom - frameRect.top;

        if (hasMinWidth)
        {
            info->ptMinTrackSize.x = addClamped(nativeWindow->minClientWidth, frameWidth);
        }
        if (hasMinHeight)
        {
            info->ptMinTrackSize.y = addClamped(nativeWindow->minClientHeight, frameHeight);
        }

        if (hasMaxWidth)
        {
            info->ptMaxTrackSize.x = addClamped(nativeWindow->maxClientWidth, frameWidth);
        }
        if (hasMaxHeight)
        {
            info->ptMaxTrackSize.y = addClamped(nativeWindow->maxClientHeight, frameHeight);
        }
    }

    // Result helpers

    WindowResult Window::recordResult(WindowResult result, unsigned long win32Error)
    {
        lastResult = result;
        lastPlatformError = win32Error;
        return result;
    }

    void Window::recordAsyncError(WindowResult result, unsigned long win32Error)
    {
        if (result == WindowResult::Success)
        {
            return;
        }

        lastAsyncResult = result;
        lastAsyncPlatformError = win32Error;
        asyncErrorRecorded = true;
    }

    // Event helpers

    void Window::pushEvent(WindowEvent event)
    {
        if (nativeWindow == nullptr || nativeWindow->events.capacity() == 0 || shouldSuppressEvent(event.type))
        {
            return;
        }

        WindowEventQueue &events = nativeWindow->events;

        if (isWindowEventCoalesable(event.type) && events.tryCoalesce(event))
        {
            return;
        }

        while (events.size() >= events.capacity())
        {
            std::size_t dropIndex = events.findOldestCoalescableEventIndex();
            if (dropIndex != static_cast<std::size_t>(-1))
            {
                events.removeAt(dropIndex);
            }
            else
            {
                events.discardFront();
            }
        }

        events.pushBack(std::move(event));
    }

    bool Window::shouldSuppressEvent(WindowEventType type) const
    {
        return nativeWindow != nullptr &&
               nativeWindow->suppressStartupEvents &&
               isStartupStateEvent(type);
    }

    // Lifecycle

    Window::~Window()
    {
        destroy();
    }

    WindowResult Window::create(const WindowDescription &description)
    {
        if (description.width <= 0 || description.height <= 0)
        {
            return recordResult(WindowResult::InvalidSize);
        }

        if (description.eventQueueCapacity == 0)
        {
            return recordResult(WindowResult::InvalidDescription);
        }

        if (!isModeAllowedForRole(description.role, description.mode))
        {
            return recordResult(WindowResult::OperationNotAllowed);
        }

        enableDpiAwareness();

        if (nativeWindow != nullptr)
        {
            WindowResult destroyResult = destroy();
            if (destroyResult != WindowResult::Success)
            {
                return destroyResult;
            }
        }

        nativeWindow = new NativeWindow;

        nativeWindow->instance = GetModuleHandleW(nullptr);
        nativeWindow->mode = WindowMode::Windowed;
        nativeWindow->role = description.role;
        nativeWindow->requestedClientWidth = description.width;
        nativeWindow->requestedClientHeight = description.height;
        nativeWindow->events.init(description.eventQueueCapacity);
        nativeWindow->dpi = getSystemDpi();
        nativeWindow->suppressStartupEvents = true;

        if (nativeWindow->instance == nullptr)
        {
            unsigned long error = GetLastError();
            delete nativeWindow;
            nativeWindow = nullptr;
            return recordResult(WindowResult::PlatformCallFailed, error);
        }

        nativeWindow->arrowCursor = LoadCursor(nullptr, IDC_ARROW);
        if (nativeWindow->arrowCursor == nullptr)
        {
            unsigned long error = GetLastError();
            destroy();
            return recordResult(WindowResult::PlatformCallFailed, error);
        }

        nativeWindow->utf16Scratch.clear();
        if (!utf8ToWide(description.title, nativeWindow->utf16Scratch))
        {
            unsigned long error = GetLastError();
            destroy();
            return recordResult(WindowResult::PlatformCallFailed, error);
        }

        const wchar_t *className = L"GameWIPWindowClass";

        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = windowProc;
        windowClass.cbClsExtra = 0;
        windowClass.cbWndExtra = 0;
        windowClass.hInstance = nativeWindow->instance;
        windowClass.hIcon = nullptr;
        windowClass.hCursor = nativeWindow->arrowCursor;
        windowClass.hbrBackground = nullptr;
        windowClass.lpszMenuName = nullptr;
        windowClass.lpszClassName = className;
        windowClass.hIconSm = nullptr;

        if (RegisterClassExW(&windowClass) == 0)
        {
            DWORD error = GetLastError();
            if (error != ERROR_CLASS_ALREADY_EXISTS)
            {
                destroy();
                return recordResult(WindowResult::PlatformCallFailed, error);
            }
        }

        DWORD windowStyle = WS_OVERLAPPEDWINDOW;
        if (!description.resizable)
        {
            windowStyle &= ~WS_THICKFRAME;
            windowStyle &= ~WS_MAXIMIZEBOX;
        }
        DWORD extendedWindowStyle = 0;
        RECT windowRect{0, 0, description.width, description.height};

        if (!adjustWindowRectForDpi(windowRect, windowStyle, extendedWindowStyle, nativeWindow->dpi))
        {
            unsigned long error = GetLastError();
            destroy();
            return recordResult(WindowResult::PlatformCallFailed, error);
        }

        int outerWidth = windowRect.right - windowRect.left;
        int outerHeight = windowRect.bottom - windowRect.top;

        nativeWindow->handle = CreateWindowExW(extendedWindowStyle, className, nativeWindow->utf16Scratch.c_str(), windowStyle, CW_USEDEFAULT, CW_USEDEFAULT, outerWidth, outerHeight, nullptr, nullptr, nativeWindow->instance, this);

        if (nativeWindow->handle == nullptr)
        {
            unsigned long error = GetLastError();
            destroy();
            return recordResult(WindowResult::PlatformCallFailed, error);
        }

        nativeWindow->title = description.title;
        nativeWindow->dpi = getWindowDpi(nativeWindow->handle);

        DWORD currentStyle = getWindowStyle(nativeWindow->handle);
        DWORD currentExtendedStyle = getWindowExtendedStyle(nativeWindow->handle);
        RECT correctedWindowRect{0, 0, description.width, description.height};
        if (!adjustWindowRectForDpi(correctedWindowRect, currentStyle, currentExtendedStyle, nativeWindow->dpi))
        {
            unsigned long error = GetLastError();
            destroy();
            return recordResult(WindowResult::PlatformCallFailed, error);
        }

        int correctedOuterWidth = correctedWindowRect.right - correctedWindowRect.left;
        int correctedOuterHeight = correctedWindowRect.bottom - correctedWindowRect.top;
        if (correctedOuterWidth != outerWidth || correctedOuterHeight != outerHeight)
        {
            if (!SetWindowPos(nativeWindow->handle, nullptr, 0, 0, correctedOuterWidth, correctedOuterHeight, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED))
            {
                unsigned long error = GetLastError();
                destroy();
                return recordResult(WindowResult::PlatformCallFailed, error);
            }
        }

        if (nativeWindow->handle != nullptr)
        {
            nativeWindow->currentMonitor = MonitorFromWindow(nativeWindow->handle, MONITOR_DEFAULTTONEAREST);
        }
        DragAcceptFiles(nativeWindow->handle, TRUE);

        RECT clientRect{};
        if (GetClientRect(nativeWindow->handle, &clientRect))
        {
            handleResize(clientRect.right - clientRect.left, clientRect.bottom - clientRect.top);
        }

        ShowWindow(nativeWindow->handle, SW_SHOW);
        UpdateWindow(nativeWindow->handle);

        WindowResult modeResult = setMode(description.mode);
        if (modeResult != WindowResult::Success)
        {
            unsigned long error = lastPlatformError;
            destroy();
            return recordResult(modeResult, error);
        }

        nativeWindow->suppressStartupEvents = false;
        nativeWindow->clientSizeChanged = false;
        return recordResult(WindowResult::Success);
    }

    WindowResult Window::destroy()
    {
        if (nativeWindow == nullptr)
        {
            return recordResult(WindowResult::Success);
        }

        WindowResult finalResult = WindowResult::Success;
        unsigned long finalError = 0;

        nativeWindow->cursorConfined = false;
        unsigned long cursorError = 0;
        WindowResult cursorResult = releaseCursorConfinement(&cursorError);
        if (cursorResult != WindowResult::Success && finalResult == WindowResult::Success)
        {
            finalResult = cursorResult;
            finalError = cursorError;
        }

        if (nativeWindow->mode != WindowMode::Windowed)
        {
            WindowResult modeResult = setMode(WindowMode::Windowed);
            if (modeResult != WindowResult::Success)
            {
                finalResult = modeResult;
                finalError = lastPlatformError;
            }
        }

        if (nativeWindow->handle != nullptr)
        {
            DragAcceptFiles(nativeWindow->handle, FALSE);
            if (!DestroyWindow(nativeWindow->handle) && finalResult == WindowResult::Success)
            {
                finalResult = WindowResult::PlatformCallFailed;
                finalError = GetLastError();
            }
            nativeWindow->handle = nullptr;
        }

        delete nativeWindow;
        nativeWindow = nullptr;

        return recordResult(finalResult, finalError);
    }

    // Events

    void Window::pollEvents(Input::InputState &inputState)
    {
        MSG message{};

        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT)
            {
                requestClose();
                continue;
            }

            bool clearInputAfterDispatch =
                nativeWindow != nullptr &&
                message.hwnd == nativeWindow->handle &&
                message.message == WM_KILLFOCUS;

            Win32Input::handleMessage(message.message, message.wParam, message.lParam, inputState);
            TranslateMessage(&message);
            DispatchMessageW(&message);

            if (clearInputAfterDispatch)
            {
                inputState.clear();
            }
        }

        if (nativeWindow != nullptr && nativeWindow->isFocused)
        {
            Win32Input::updateGamepads(inputState);
        }
    }

    bool Window::popEvent(WindowEvent &outEvent)
    {
        if (nativeWindow == nullptr || nativeWindow->events.empty())
        {
            return false;
        }

        return nativeWindow->events.popFront(outEvent);
    }

    void Window::clearEvents()
    {
        if (nativeWindow != nullptr)
        {
            nativeWindow->events.clear();
        }
    }

    bool Window::shouldClose() const
    {
        if (nativeWindow == nullptr)
        {
            return true;
        }

        return nativeWindow->closeRequested;
    }

    void Window::requestClose()
    {
        if (nativeWindow != nullptr)
        {
            if (!nativeWindow->closeRequested)
            {
                nativeWindow->closeRequested = true;
                pushEvent(WindowEvent{.type = WindowEventType::CloseRequested});
            }
        }
    }

    // Size and state

    int Window::getClientWidth() const
    {
        if (nativeWindow == nullptr)
        {
            return 0;
        }

        return nativeWindow->clientWidth;
    }

    int Window::getClientHeight() const
    {
        if (nativeWindow == nullptr)
        {
            return 0;
        }

        return nativeWindow->clientHeight;
    }

    unsigned int Window::getDpi() const
    {
        if (nativeWindow == nullptr)
        {
            return defaultDpi;
        }

        return nativeWindow->dpi;
    }

    bool Window::wasClientSizeChanged() const
    {
        return nativeWindow != nullptr && nativeWindow->clientSizeChanged;
    }

    void Window::clearClientSizeChanged()
    {
        if (nativeWindow != nullptr)
        {
            nativeWindow->clientSizeChanged = false;
        }
    }

    void Window::setMinClientSize(int width, int height)
    {
        if (nativeWindow != nullptr)
        {
            nativeWindow->minClientWidth = std::max(0, width);
            nativeWindow->minClientHeight = std::max(0, height);

            nativeWindow->maxClientWidth = std::max(nativeWindow->maxClientWidth, nativeWindow->minClientWidth);
            nativeWindow->maxClientHeight = std::max(nativeWindow->maxClientHeight, nativeWindow->minClientHeight);
            nativeWindow->requestedClientWidth = clampClientDimension(nativeWindow->requestedClientWidth, nativeWindow->minClientWidth, nativeWindow->maxClientWidth);
            nativeWindow->requestedClientHeight = clampClientDimension(nativeWindow->requestedClientHeight, nativeWindow->minClientHeight, nativeWindow->maxClientHeight);
        }
    }

    void Window::setMaxClientSize(int width, int height)
    {
        if (nativeWindow != nullptr)
        {
            nativeWindow->maxClientWidth = width <= 0 ? unlimitedClientSize : std::max(width, nativeWindow->minClientWidth);
            nativeWindow->maxClientHeight = height <= 0 ? unlimitedClientSize : std::max(height, nativeWindow->minClientHeight);
            nativeWindow->requestedClientWidth = clampClientDimension(nativeWindow->requestedClientWidth, nativeWindow->minClientWidth, nativeWindow->maxClientWidth);
            nativeWindow->requestedClientHeight = clampClientDimension(nativeWindow->requestedClientHeight, nativeWindow->minClientHeight, nativeWindow->maxClientHeight);
        }
    }

    void Window::getClientSizeConstraints(int &outMinWidth, int &outMinHeight, int &outMaxWidth, int &outMaxHeight) const
    {
        if (nativeWindow != nullptr)
        {
            outMinWidth = nativeWindow->minClientWidth;
            outMinHeight = nativeWindow->minClientHeight;
            outMaxWidth = nativeWindow->maxClientWidth;
            outMaxHeight = nativeWindow->maxClientHeight;
            return;
        }

        outMinWidth = 0;
        outMinHeight = 0;
        outMaxWidth = unlimitedClientSize;
        outMaxHeight = unlimitedClientSize;
    }

    WindowInfo Window::getInfo() const
    {
        if (nativeWindow == nullptr)
        {
            return {};
        }

        return WindowInfo{
            .instance = nativeWindow->instance,
            .handle = nativeWindow->handle};
    }

    bool Window::isFocused() const
    {
        return nativeWindow != nullptr && nativeWindow->isFocused;
    }

    bool Window::isMinimized() const
    {
        return nativeWindow != nullptr && nativeWindow->isMinimized;
    }

    // Size and title

    WindowResult Window::setClientSize(int width, int height)
    {
        if (nativeWindow == nullptr || nativeWindow->handle == nullptr)
        {
            return recordResult(WindowResult::NotCreated);
        }

        if (width <= 0 || height <= 0)
        {
            return recordResult(WindowResult::InvalidSize);
        }

        int targetWidth = clampClientDimension(width, nativeWindow->minClientWidth, nativeWindow->maxClientWidth);
        int targetHeight = clampClientDimension(height, nativeWindow->minClientHeight, nativeWindow->maxClientHeight);

        switch (nativeWindow->mode)
        {
        case WindowMode::Windowed:
        {
            if (IsZoomed(nativeWindow->handle))
            {
                ShowWindow(nativeWindow->handle, SW_RESTORE);
            }

            DWORD windowStyle = getWindowStyle(nativeWindow->handle);
            DWORD extendedWindowStyle = getWindowExtendedStyle(nativeWindow->handle);
            nativeWindow->dpi = getWindowDpi(nativeWindow->handle);

            RECT windowRect{0, 0, targetWidth, targetHeight};

            if (!adjustWindowRectForDpi(windowRect, windowStyle, extendedWindowStyle, nativeWindow->dpi))
            {
                return recordResult(WindowResult::PlatformCallFailed, GetLastError());
            }

            int outerWidth = windowRect.right - windowRect.left;
            int outerHeight = windowRect.bottom - windowRect.top;

            if (!SetWindowPos(nativeWindow->handle, nullptr, 0, 0, outerWidth, outerHeight, SWP_NOMOVE | SWP_NOZORDER | SWP_FRAMECHANGED))
            {
                return recordResult(WindowResult::PlatformCallFailed, GetLastError());
            }

            nativeWindow->requestedClientWidth = targetWidth;
            nativeWindow->requestedClientHeight = targetHeight;
            updateCursorConfinement();
            return recordResult(WindowResult::Success);
        }
        case WindowMode::BorderlessFullscreen:
            nativeWindow->requestedClientWidth = targetWidth;
            nativeWindow->requestedClientHeight = targetHeight;
            updateCursorConfinement();
            return recordResult(WindowResult::Success);
        case WindowMode::Fullscreen:
        {
            if (!nativeWindow->hasSavedDisplayMode)
            {
                return recordResult(WindowResult::MissingDisplayMode);
            }

            // Explicit fullscreen display-mode requests win over client-size requests.
            // Call setFullscreenDisplayMode to change an exclusive fullscreen resolution.
            DisplayMode fullscreenDisplayMode{};
            if (nativeWindow->hasRequestedFullscreenDisplayMode)
            {
                fullscreenDisplayMode = nativeWindow->requestedFullscreenDisplayMode;
            }
            else
            {
                MonitorInfo monitor = nativeWindow->activeFullscreenMonitor;
                if (monitor.handle == nullptr || monitor.deviceName.empty())
                {
                    WindowResult monitorResult = getCurrentMonitor(monitor);
                    if (monitorResult != WindowResult::Success)
                    {
                        return monitorResult;
                    }
                }

                std::vector<DisplayMode> displayModes;
                WindowResult displayModesResult = getDisplayModes(monitor, displayModes);
                if (displayModesResult != WindowResult::Success)
                {
                    return displayModesResult;
                }

                if (!chooseHighestRefreshDisplayMode(displayModes, targetWidth, targetHeight, fullscreenDisplayMode))
                {
                    return recordResult(WindowResult::InvalidDisplayMode);
                }
            }

            DisplayMode previousFullscreenDisplayMode = nativeWindow->activeFullscreenDisplayMode;
            bool previousFullscreenModeIsExact = nativeWindow->activeFullscreenModeIsExact;
            int previousFullscreenWidth = nativeWindow->fullscreenWidth;
            int previousFullscreenHeight = nativeWindow->fullscreenHeight;

            DEVMODEW fullscreenMode = nativeWindow->savedDisplayMode;
            applyDisplayModeToDevMode(fullscreenMode, fullscreenDisplayMode, true);

            LONG displayResult = ChangeDisplaySettingsExW(
                nativeWindow->fullscreenDeviceName,
                &fullscreenMode,
                nullptr,
                CDS_FULLSCREEN,
                nullptr);

            if (displayResult != DISP_CHANGE_SUCCESSFUL)
            {
                return recordResult(WindowResult::ModeChangeFailed);
            }

            if (!SetWindowPos(
                    nativeWindow->handle,
                    HWND_TOP,
                    nativeWindow->fullscreenRect.left,
                    nativeWindow->fullscreenRect.top,
                    fullscreenDisplayMode.width,
                    fullscreenDisplayMode.height,
                    SWP_FRAMECHANGED | SWP_SHOWWINDOW))
            {
                unsigned long error = GetLastError();

                DisplayMode rollbackDisplayMode = previousFullscreenDisplayMode;
                if (!isCompleteDisplayMode(rollbackDisplayMode))
                {
                    rollbackDisplayMode = displayModeFromDevMode(nativeWindow->savedDisplayMode);
                    rollbackDisplayMode.width = previousFullscreenWidth;
                    rollbackDisplayMode.height = previousFullscreenHeight;
                }

                DEVMODEW rollbackMode = nativeWindow->savedDisplayMode;
                bool exactMode = previousFullscreenModeIsExact && isCompleteDisplayMode(rollbackDisplayMode);
                applyDisplayModeToDevMode(rollbackMode, rollbackDisplayMode, exactMode);
                ChangeDisplaySettingsExW(nativeWindow->fullscreenDeviceName, &rollbackMode, nullptr, CDS_FULLSCREEN, nullptr);
                return recordResult(WindowResult::PlatformCallFailed, error);
            }

            nativeWindow->requestedClientWidth = targetWidth;
            nativeWindow->requestedClientHeight = targetHeight;
            nativeWindow->fullscreenWidth = fullscreenDisplayMode.width;
            nativeWindow->fullscreenHeight = fullscreenDisplayMode.height;
            nativeWindow->activeFullscreenDisplayMode = fullscreenDisplayMode;
            nativeWindow->activeFullscreenModeIsExact = true;

            updateCursorConfinement();
            return recordResult(WindowResult::Success);
        }
        default:
            return recordResult(WindowResult::ModeChangeFailed);
        }
    }

    WindowResult Window::setTitle(std::string_view title)
    {
        if (nativeWindow == nullptr || nativeWindow->handle == nullptr)
        {
            return recordResult(WindowResult::NotCreated);
        }

        if (nativeWindow->title == title)
        {
            return recordResult(WindowResult::Success);
        }

        nativeWindow->utf16Scratch.clear();
        if (!utf8ToWide(title, nativeWindow->utf16Scratch))
        {
            return recordResult(WindowResult::PlatformCallFailed, GetLastError());
        }

        if (SetWindowTextW(nativeWindow->handle, nativeWindow->utf16Scratch.c_str()) == FALSE)
        {
            return recordResult(WindowResult::PlatformCallFailed, GetLastError());
        }

        nativeWindow->title.assign(title);
        return recordResult(WindowResult::Success);
    }

    WindowMode Window::getMode() const
    {
        if (nativeWindow == nullptr)
        {
            return WindowMode::Windowed;
        }

        return nativeWindow->mode;
    }

    WindowRole Window::getRole() const
    {
        if (nativeWindow == nullptr)
        {
            return WindowRole::MainGame;
        }

        return nativeWindow->role;
    }

    // Errors

    WindowResult Window::getLastResult() const
    {
        return lastResult;
    }

    unsigned long Window::getLastPlatformError() const
    {
        return lastPlatformError;
    }

    bool Window::hasAsyncError() const
    {
        return asyncErrorRecorded;
    }

    WindowResult Window::getLastAsyncResult() const
    {
        return asyncErrorRecorded ? lastAsyncResult : WindowResult::Success;
    }

    unsigned long Window::getLastAsyncPlatformError() const
    {
        return asyncErrorRecorded ? lastAsyncPlatformError : 0;
    }

    void Window::clearAsyncError()
    {
        asyncErrorRecorded = false;
        lastAsyncResult = WindowResult::Success;
        lastAsyncPlatformError = 0;
    }

    // Window mode

    WindowResult Window::setMode(WindowMode mode)
    {
        if (nativeWindow == nullptr || nativeWindow->handle == nullptr)
        {
            return recordResult(WindowResult::NotCreated);
        }

        if (!isModeAllowedForRole(nativeWindow->role, mode))
        {
            return recordResult(WindowResult::OperationNotAllowed);
        }

        if (nativeWindow->mode == mode)
        {
            return recordResult(WindowResult::Success);
        }

        WindowResult modeResult = WindowResult::ModeChangeFailed;
        switch (mode)
        {
        case WindowMode::Windowed:
            modeResult = applyWindowedMode();
            break;
        case WindowMode::BorderlessFullscreen:
            modeResult = applyBorderlessFullscreenMode();
            break;
        case WindowMode::Fullscreen:
            modeResult = applyFullscreenMode();
            break;
        default:
            return recordResult(WindowResult::ModeChangeFailed);
        }

        if (modeResult == WindowResult::Success)
        {
            updateCurrentMonitor();
            pushEvent(WindowEvent{
                .type = WindowEventType::ModeChanged,
                .width = nativeWindow->clientWidth,
                .height = nativeWindow->clientHeight,
                .mode = nativeWindow->mode});
            updateCursorConfinement();
        }

        return modeResult;
    }

    // Cursor

    void Window::setCursorVisible(bool visible)
    {
        if (nativeWindow == nullptr)
        {
            return;
        }

        nativeWindow->cursorVisible = visible;

        if (nativeWindow->handle != nullptr && isCursorOverClientArea(nativeWindow->handle))
        {
            SetCursor(visible ? nativeWindow->arrowCursor : nullptr);
        }
    }

    bool Window::isCursorVisible() const
    {
        return nativeWindow != nullptr && nativeWindow->cursorVisible;
    }

    WindowResult Window::setCursorConfined(bool confined)
    {
        if (nativeWindow == nullptr)
        {
            return recordResult(WindowResult::NotCreated);
        }

        if (confined && !isCursorConfinementAllowedForRole(nativeWindow->role))
        {
            return recordResult(WindowResult::OperationNotAllowed);
        }

        nativeWindow->cursorConfined = confined;
        updateCursorConfinement();
        return recordResult(WindowResult::Success);
    }

    bool Window::isCursorConfined() const
    {
        return nativeWindow != nullptr && nativeWindow->cursorConfined;
    }

    WindowResult Window::setCursorMode(CursorMode mode)
    {
        if (nativeWindow == nullptr)
        {
            return recordResult(WindowResult::NotCreated);
        }

        bool newVisible = true;
        bool newConfined = false;

        switch (mode)
        {
        case CursorMode::FreeVisible:
            newVisible = true;
            newConfined = false;
            break;
        case CursorMode::FreeHidden:
            newVisible = false;
            newConfined = false;
            break;
        case CursorMode::ConfinedVisible:
            newVisible = true;
            newConfined = true;
            break;
        case CursorMode::ConfinedHidden:
            newVisible = false;
            newConfined = true;
            break;
        default:
            return recordResult(WindowResult::OperationNotAllowed);
        }

        if (newConfined && !isCursorConfinementAllowedForRole(nativeWindow->role))
        {
            return recordResult(WindowResult::OperationNotAllowed);
        }

        nativeWindow->cursorVisible = newVisible;
        nativeWindow->cursorConfined = newConfined;
        updateCursorConfinement();
        if (nativeWindow->handle != nullptr && isCursorOverClientArea(nativeWindow->handle))
        {
            SetCursor(newVisible ? nativeWindow->arrowCursor : nullptr);
        }

        return recordResult(WindowResult::Success);
    }

    CursorMode Window::getCursorMode() const
    {
        bool visible = isCursorVisible();
        bool confined = isCursorConfined();

        if (confined)
        {
            return visible ? CursorMode::ConfinedVisible : CursorMode::ConfinedHidden;
        }

        return visible ? CursorMode::FreeVisible : CursorMode::FreeHidden;
    }

    // Message handlers

    void Window::handleResize(int width, int height)
    {
        if (nativeWindow == nullptr)
        {
            return;
        }

        if (width < 0)
        {
            width = 0;
        }
        if (height < 0)
        {
            height = 0;
        }
        if (nativeWindow->clientWidth == width && nativeWindow->clientHeight == height)
        {
            return;
        }

        nativeWindow->clientWidth = width;
        nativeWindow->clientHeight = height;
        nativeWindow->clientSizeChanged = true;
        pushEvent(WindowEvent{
            .type = WindowEventType::Resized,
            .width = width,
            .height = height});
        updateCursorConfinement();
    }

    void Window::handleMove(int x, int y)
    {
        pushEvent(WindowEvent{
            .type = WindowEventType::Moved,
            .x = x,
            .y = y});
        updateCursorConfinement();
    }

    void Window::updateCurrentMonitor()
    {
        if (nativeWindow == nullptr || nativeWindow->handle == nullptr)
        {
            return;
        }

        HMONITOR monitor = MonitorFromWindow(nativeWindow->handle, MONITOR_DEFAULTTONEAREST);
        if (monitor == nullptr || monitor == nativeWindow->currentMonitor)
        {
            return;
        }

        nativeWindow->currentMonitor = monitor;
        pushEvent(WindowEvent{
            .type = WindowEventType::MonitorChanged,
            .width = nativeWindow->clientWidth,
            .height = nativeWindow->clientHeight});
    }

    void Window::handleFocusChange(bool focused)
    {
        if (nativeWindow == nullptr)
        {
            return;
        }

        bool focusChanged = nativeWindow->isFocused != focused;
        nativeWindow->isFocused = focused;

        if (focusChanged)
        {
            pushEvent(WindowEvent{
                .type = focused ? WindowEventType::Focused : WindowEventType::LostFocus});
        }

        updateCursorConfinement();

        if (!focused && nativeWindow->mode == WindowMode::Fullscreen && nativeWindow->hasSavedDisplayMode && !nativeWindow->fullscreenSuspended)
        {
            LONG displayResult = ChangeDisplaySettingsExW(
                nativeWindow->fullscreenDeviceName,
                &nativeWindow->savedDisplayMode,
                nullptr,
                0,
                nullptr);

            if (displayResult != DISP_CHANGE_SUCCESSFUL)
            {
                recordAsyncError(WindowResult::ModeChangeFailed);
                return;
            }

            ShowWindow(nativeWindow->handle, SW_MINIMIZE);
            nativeWindow->fullscreenSuspended = true;
        }
        else if (focused && nativeWindow->mode == WindowMode::Fullscreen && nativeWindow->fullscreenSuspended && nativeWindow->hasSavedDisplayMode)
        {
            ShowWindow(nativeWindow->handle, SW_RESTORE);

            DisplayMode fullscreenDisplayMode = nativeWindow->activeFullscreenDisplayMode;
            if (!isCompleteDisplayMode(fullscreenDisplayMode))
            {
                fullscreenDisplayMode = displayModeFromDevMode(nativeWindow->savedDisplayMode);
                fullscreenDisplayMode.width = nativeWindow->fullscreenWidth;
                fullscreenDisplayMode.height = nativeWindow->fullscreenHeight;
            }

            bool exactMode = nativeWindow->activeFullscreenModeIsExact && isCompleteDisplayMode(fullscreenDisplayMode);
            DEVMODEW fullscreenMode = nativeWindow->savedDisplayMode;
            applyDisplayModeToDevMode(fullscreenMode, fullscreenDisplayMode, exactMode);

            LONG displayResult = ChangeDisplaySettingsExW(
                nativeWindow->fullscreenDeviceName,
                &fullscreenMode,
                nullptr,
                CDS_FULLSCREEN,
                nullptr);

            if (displayResult != DISP_CHANGE_SUCCESSFUL)
            {
                recordAsyncError(WindowResult::ModeChangeFailed);
                return;
            }

            if (!SetWindowPos(
                    nativeWindow->handle,
                    HWND_TOP,
                    nativeWindow->fullscreenRect.left,
                    nativeWindow->fullscreenRect.top,
                    nativeWindow->fullscreenWidth,
                    nativeWindow->fullscreenHeight,
                    SWP_FRAMECHANGED | SWP_SHOWWINDOW))
            {
                recordAsyncError(WindowResult::PlatformCallFailed, GetLastError());
                return;
            }

            nativeWindow->fullscreenSuspended = false;
            updateCursorConfinement();
        }
    }

    void Window::handleActivationChange(bool active)
    {
        if (nativeWindow == nullptr)
        {
            return;
        }

        bool suspended = !active;
        if (nativeWindow->isSuspended == suspended)
        {
            return;
        }

        nativeWindow->isSuspended = suspended;
        pushEvent(WindowEvent{
            .type = suspended ? WindowEventType::Suspended : WindowEventType::Resumed});
    }

    void Window::handleMinimizeChange(bool minimized)
    {
        if (nativeWindow == nullptr)
        {
            return;
        }

        bool minimizedChanged = nativeWindow->isMinimized != minimized;
        nativeWindow->isMinimized = minimized;
        if (minimizedChanged)
        {
            pushEvent(WindowEvent{
                .type = minimized ? WindowEventType::Minimized : WindowEventType::Restored});
        }
        updateCursorConfinement();
    }

    void Window::handleMaximizeChange(bool maximized)
    {
        if (nativeWindow == nullptr)
        {
            return;
        }

        bool maximizedChanged = nativeWindow->isMaximized != maximized;
        nativeWindow->isMaximized = maximized;
        if (maximizedChanged)
        {
            pushEvent(WindowEvent{
                .type = maximized ? WindowEventType::Maximized : WindowEventType::Restored});
        }
    }

    void Window::handleVisibilityChange(bool visible)
    {
        if (nativeWindow == nullptr)
        {
            return;
        }

        bool visibleChanged = nativeWindow->isVisible != visible;
        nativeWindow->isVisible = visible;
        if (visibleChanged)
        {
            pushEvent(WindowEvent{
                .type = visible ? WindowEventType::Visible : WindowEventType::Occluded});
        }
    }

    bool Window::shouldHandleCursorEnter() const
    {
        return nativeWindow != nullptr && !nativeWindow->cursorInClient;
    }

    bool Window::handleCursorEnter()
    {
        if (nativeWindow == nullptr || nativeWindow->cursorInClient)
        {
            return false;
        }

        nativeWindow->cursorInClient = true;
        pushEvent(WindowEvent{.type = WindowEventType::CursorEntered});
        return true;
    }

    void Window::handleCursorTrackingFailure(unsigned long win32Error)
    {
        if (nativeWindow == nullptr)
        {
            return;
        }

        nativeWindow->cursorInClient = false;
        recordAsyncError(WindowResult::PlatformCallFailed, win32Error);
    }

    void Window::handleCursorLeave()
    {
        if (nativeWindow == nullptr || !nativeWindow->cursorInClient)
        {
            return;
        }

        nativeWindow->cursorInClient = false;
        pushEvent(WindowEvent{.type = WindowEventType::CursorLeft});
    }

    void Window::handleFileDrop(std::string_view filePath)
    {
        if (nativeWindow == nullptr)
        {
            return;
        }

        WindowEvent event{.type = WindowEventType::FileDropped};
        event.filePath.assign(filePath);
        pushEvent(std::move(event));
    }

    void Window::handleDestroyed()
    {
        if (nativeWindow == nullptr)
        {
            return;
        }

        requestClose();
        pushEvent(WindowEvent{.type = WindowEventType::Destroyed});
    }

    void Window::handleDpiChange(unsigned int dpi, int suggestedLeft, int suggestedTop, int suggestedRight, int suggestedBottom)
    {
        if (nativeWindow == nullptr || nativeWindow->handle == nullptr)
        {
            return;
        }

        nativeWindow->dpi = dpi == 0 ? defaultDpi : dpi;

        switch (nativeWindow->mode)
        {
        case WindowMode::Windowed:
            if (!SetWindowPos(
                    nativeWindow->handle,
                    nullptr,
                    suggestedLeft,
                    suggestedTop,
                    suggestedRight - suggestedLeft,
                    suggestedBottom - suggestedTop,
                    SWP_NOZORDER | SWP_NOACTIVATE))
            {
                recordAsyncError(WindowResult::PlatformCallFailed, GetLastError());
            }
            break;
        case WindowMode::BorderlessFullscreen:
        {
            HMONITOR monitor = MonitorFromWindow(nativeWindow->handle, MONITOR_DEFAULTTONEAREST);
            MONITORINFO monitorInfo{};
            monitorInfo.cbSize = sizeof(monitorInfo);
            if (monitor == nullptr)
            {
                recordAsyncError(WindowResult::InvalidMonitor, ERROR_INVALID_HANDLE);
                break;
            }
            if (!GetMonitorInfoW(monitor, &monitorInfo))
            {
                recordAsyncError(WindowResult::PlatformCallFailed, GetLastError());
                break;
            }

            RECT monitorRect = monitorInfo.rcMonitor;
            if (!SetWindowPos(
                    nativeWindow->handle,
                    HWND_TOP,
                    monitorRect.left,
                    monitorRect.top,
                    monitorRect.right - monitorRect.left,
                    monitorRect.bottom - monitorRect.top,
                    SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOACTIVATE))
            {
                recordAsyncError(WindowResult::PlatformCallFailed, GetLastError());
            }
            break;
        }
        case WindowMode::Fullscreen:
            if (!nativeWindow->fullscreenSuspended &&
                nativeWindow->fullscreenWidth > 0 &&
                nativeWindow->fullscreenHeight > 0)
            {
                if (!SetWindowPos(
                        nativeWindow->handle,
                        HWND_TOP,
                        nativeWindow->fullscreenRect.left,
                        nativeWindow->fullscreenRect.top,
                        nativeWindow->fullscreenWidth,
                        nativeWindow->fullscreenHeight,
                        SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOACTIVATE))
                {
                    recordAsyncError(WindowResult::PlatformCallFailed, GetLastError());
                }
            }
            break;
        default:
            break;
        }

        RECT clientRect{};
        if (GetClientRect(nativeWindow->handle, &clientRect))
        {
            handleResize(clientRect.right - clientRect.left, clientRect.bottom - clientRect.top);
        }

        invalidateMonitorCache();
        updateCurrentMonitor();

        pushEvent(WindowEvent{
            .type = WindowEventType::DpiChanged,
            .width = nativeWindow->clientWidth,
            .height = nativeWindow->clientHeight,
            .dpi = nativeWindow->dpi});
        updateCursorConfinement();
    }

    void Window::handleDisplayChange()
    {
        if (nativeWindow == nullptr)
        {
            return;
        }

        invalidateMonitorCache();
        nativeWindow->currentMonitor = MonitorFromWindow(nativeWindow->handle, MONITOR_DEFAULTTONEAREST);
        pushEvent(WindowEvent{
            .type = WindowEventType::DisplayChanged});
    }

    void Window::invalidateMonitorCache()
    {
        if (nativeWindow == nullptr)
        {
            return;
        }

        nativeWindow->monitorCache.clear();
        nativeWindow->displayModeCache.clear();
    }

    // Mode helpers

    WindowResult Window::saveWindowedPlacement()
    {
        if (nativeWindow == nullptr || nativeWindow->handle == nullptr)
        {
            return recordResult(WindowResult::NotCreated);
        }

        if (nativeWindow->hasSavedWindowedPlacement)
        {
            return recordResult(WindowResult::Success);
        }

        nativeWindow->windowedStyle = getWindowStyle(nativeWindow->handle);
        nativeWindow->windowedStyle &= ~(WS_MAXIMIZE | WS_MINIMIZE);
        nativeWindow->windowedExtendedStyle = getWindowExtendedStyle(nativeWindow->handle);

        WINDOWPLACEMENT placement{};
        placement.length = sizeof(placement);
        if (GetWindowPlacement(nativeWindow->handle, &placement))
        {
            nativeWindow->windowedRect = placement.rcNormalPosition;
        }
        else if (!GetWindowRect(nativeWindow->handle, &nativeWindow->windowedRect))
        {
            return recordResult(WindowResult::PlatformCallFailed, GetLastError());
        }

        nativeWindow->hasSavedWindowedPlacement = true;
        return recordResult(WindowResult::Success);
    }

    WindowResult Window::restoreDisplayMode()
    {
        if (nativeWindow == nullptr)
        {
            return recordResult(WindowResult::NotCreated);
        }

        if (!nativeWindow->hasSavedDisplayMode)
        {
            nativeWindow->fullscreenSuspended = false;
            nativeWindow->activeFullscreenMonitor = {};
            nativeWindow->activeFullscreenDisplayMode = {};
            nativeWindow->activeFullscreenModeIsExact = false;
            nativeWindow->fullscreenRect = {};
            nativeWindow->fullscreenWidth = 0;
            nativeWindow->fullscreenHeight = 0;
            return recordResult(WindowResult::Success);
        }

        // If fullscreen was suspended on focus loss, the desktop display mode has already been restored.
        if (!nativeWindow->fullscreenSuspended)
        {
            LONG displayResult = ChangeDisplaySettingsExW(
                nativeWindow->fullscreenDeviceName,
                &nativeWindow->savedDisplayMode,
                nullptr,
                0,
                nullptr);

            if (displayResult != DISP_CHANGE_SUCCESSFUL)
            {
                return recordResult(WindowResult::ModeChangeFailed);
            }
        }

        nativeWindow->hasSavedDisplayMode = false;
        nativeWindow->fullscreenSuspended = false;
        nativeWindow->activeFullscreenMonitor = {};
        nativeWindow->activeFullscreenDisplayMode = {};
        nativeWindow->activeFullscreenModeIsExact = false;
        nativeWindow->fullscreenRect = {};
        nativeWindow->fullscreenWidth = 0;
        nativeWindow->fullscreenHeight = 0;
        return recordResult(WindowResult::Success);
    }

    WindowResult Window::restoreWindowedPlacement()
    {
        if (nativeWindow == nullptr || nativeWindow->handle == nullptr)
        {
            return recordResult(WindowResult::NotCreated);
        }

        if (!nativeWindow->hasSavedWindowedPlacement)
        {
            return recordResult(WindowResult::MissingWindowedPlacement);
        }

        ShowWindow(nativeWindow->handle, SW_RESTORE);

        unsigned long styleError = 0;
        if (!setWindowStyle(nativeWindow->handle, nativeWindow->windowedStyle, styleError))
        {
            return recordResult(WindowResult::PlatformCallFailed, styleError);
        }

        unsigned long extendedStyleError = 0;
        if (!setWindowExtendedStyle(nativeWindow->handle, nativeWindow->windowedExtendedStyle, extendedStyleError))
        {
            return recordResult(WindowResult::PlatformCallFailed, extendedStyleError);
        }

        RECT requestedWindowRect{
            0,
            0,
            nativeWindow->requestedClientWidth,
            nativeWindow->requestedClientHeight};

        nativeWindow->dpi = getWindowDpi(nativeWindow->handle);
        if (!adjustWindowRectForDpi(
                requestedWindowRect,
                nativeWindow->windowedStyle,
                nativeWindow->windowedExtendedStyle,
                nativeWindow->dpi))
        {
            return recordResult(WindowResult::PlatformCallFailed, GetLastError());
        }

        int restoredWidth = requestedWindowRect.right - requestedWindowRect.left;
        int restoredHeight = requestedWindowRect.bottom - requestedWindowRect.top;

        if (!SetWindowPos(
                nativeWindow->handle,
                nullptr,
                nativeWindow->windowedRect.left,
                nativeWindow->windowedRect.top,
                restoredWidth,
                restoredHeight,
                SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW))
        {
            return recordResult(WindowResult::PlatformCallFailed, GetLastError());
        }

        return recordResult(WindowResult::Success);
    }

    WindowResult Window::applyWindowedMode()
    {
        WindowMode previousMode = nativeWindow->mode;

        auto rollbackToFullscreen = [&]()
        {
            if (previousMode == WindowMode::Fullscreen && applyFullscreenMode() != WindowResult::Success)
            {
                recordResult(WindowResult::ModeChangeFailed);
            }
        };

        if (!nativeWindow->hasSavedWindowedPlacement)
        {
            return recordResult(WindowResult::MissingWindowedPlacement);
        }

        WindowResult displayResult = restoreDisplayMode();
        if (displayResult != WindowResult::Success)
        {
            return displayResult;
        }

        WindowResult placementResult = restoreWindowedPlacement();
        if (placementResult != WindowResult::Success)
        {
            unsigned long placementError = lastPlatformError;
            rollbackToFullscreen();
            return recordResult(placementResult, placementError);
        }

        nativeWindow->hasSavedWindowedPlacement = false;
        nativeWindow->mode = WindowMode::Windowed;
        return recordResult(WindowResult::Success);
    }

    WindowResult Window::applyBorderlessFullscreenMode()
    {
        WindowMode previousMode = nativeWindow->mode;

        auto rollbackToFullscreen = [&]()
        {
            if (previousMode == WindowMode::Fullscreen && applyFullscreenMode() != WindowResult::Success)
            {
                recordResult(WindowResult::ModeChangeFailed);
            }
        };

        WindowResult saveResult = saveWindowedPlacement();
        if (saveResult != WindowResult::Success)
        {
            return saveResult;
        }

        HMONITOR monitor = MonitorFromWindow(nativeWindow->handle, MONITOR_DEFAULTTONEAREST);

        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);

        if (!GetMonitorInfoW(monitor, &monitorInfo))
        {
            return recordResult(WindowResult::PlatformCallFailed, GetLastError());
        }

        WindowResult displayResult = restoreDisplayMode();
        if (displayResult != WindowResult::Success)
        {
            return displayResult;
        }

        DWORD borderlessStyle = nativeWindow->windowedStyle;
        borderlessStyle &= ~WS_OVERLAPPEDWINDOW;
        borderlessStyle |= WS_POPUP;

        unsigned long styleError = 0;
        if (!setWindowStyle(nativeWindow->handle, borderlessStyle, styleError))
        {
            rollbackToFullscreen();
            return recordResult(WindowResult::PlatformCallFailed, styleError);
        }

        RECT monitorRect = monitorInfo.rcMonitor;

        if (!SetWindowPos(
                nativeWindow->handle,
                HWND_TOP,
                monitorRect.left,
                monitorRect.top,
                monitorRect.right - monitorRect.left,
                monitorRect.bottom - monitorRect.top,
                SWP_FRAMECHANGED | SWP_SHOWWINDOW))
        {
            unsigned long error = GetLastError();
            rollbackToFullscreen();
            return recordResult(WindowResult::PlatformCallFailed, error);
        }

        nativeWindow->mode = WindowMode::BorderlessFullscreen;
        return recordResult(WindowResult::Success);
    }

    Window::FullscreenModeSnapshot Window::captureFullscreenModeSnapshot() const
    {
        FullscreenModeSnapshot snapshot{};
        if (nativeWindow == nullptr)
        {
            return snapshot;
        }

        std::copy(
            std::begin(nativeWindow->fullscreenDeviceName),
            std::end(nativeWindow->fullscreenDeviceName),
            std::begin(snapshot.deviceName));
        snapshot.savedDisplayMode = nativeWindow->savedDisplayMode;
        snapshot.activeMonitor = nativeWindow->activeFullscreenMonitor;
        snapshot.activeDisplayMode = nativeWindow->activeFullscreenDisplayMode;
        snapshot.rect = nativeWindow->fullscreenRect;
        snapshot.hasSavedDisplayMode = nativeWindow->hasSavedDisplayMode;
        snapshot.activeModeIsExact = nativeWindow->activeFullscreenModeIsExact;
        snapshot.suspended = nativeWindow->fullscreenSuspended;
        snapshot.width = nativeWindow->fullscreenWidth;
        snapshot.height = nativeWindow->fullscreenHeight;
        return snapshot;
    }

    WindowResult Window::restoreFullscreenModeSnapshot(const FullscreenModeSnapshot &snapshot)
    {
        if (nativeWindow == nullptr || nativeWindow->handle == nullptr)
        {
            return WindowResult::NotCreated;
        }

        if (!snapshot.hasSavedDisplayMode)
        {
            return WindowResult::ModeChangeFailed;
        }

        if (!snapshot.suspended)
        {
            DisplayMode displayMode = snapshot.activeDisplayMode;
            if (!isCompleteDisplayMode(displayMode))
            {
                displayMode = displayModeFromDevMode(snapshot.savedDisplayMode);
                displayMode.width = snapshot.width;
                displayMode.height = snapshot.height;
            }

            DEVMODEW fullscreenMode = snapshot.savedDisplayMode;
            bool exactMode = snapshot.activeModeIsExact && isCompleteDisplayMode(displayMode);
            applyDisplayModeToDevMode(fullscreenMode, displayMode, exactMode);

            LONG displayResult = ChangeDisplaySettingsExW(
                snapshot.deviceName,
                &fullscreenMode,
                nullptr,
                CDS_FULLSCREEN,
                nullptr);
            if (displayResult != DISP_CHANGE_SUCCESSFUL)
            {
                return WindowResult::ModeChangeFailed;
            }

            DWORD fullscreenStyle = nativeWindow->windowedStyle;
            fullscreenStyle &= ~WS_OVERLAPPEDWINDOW;
            fullscreenStyle |= WS_POPUP;

            unsigned long styleError = 0;
            if (!setWindowStyle(nativeWindow->handle, fullscreenStyle, styleError))
            {
                return WindowResult::PlatformCallFailed;
            }

            if (!SetWindowPos(
                    nativeWindow->handle,
                    HWND_TOP,
                    snapshot.rect.left,
                    snapshot.rect.top,
                    snapshot.width,
                    snapshot.height,
                    SWP_FRAMECHANGED | SWP_SHOWWINDOW))
            {
                return WindowResult::PlatformCallFailed;
            }
        }

        std::copy(
            std::begin(snapshot.deviceName),
            std::end(snapshot.deviceName),
            std::begin(nativeWindow->fullscreenDeviceName));
        nativeWindow->savedDisplayMode = snapshot.savedDisplayMode;
        nativeWindow->activeFullscreenMonitor = snapshot.activeMonitor;
        nativeWindow->activeFullscreenDisplayMode = snapshot.activeDisplayMode;
        nativeWindow->fullscreenRect = snapshot.rect;
        nativeWindow->hasSavedDisplayMode = snapshot.hasSavedDisplayMode;
        nativeWindow->activeFullscreenModeIsExact = snapshot.activeModeIsExact;
        nativeWindow->fullscreenSuspended = snapshot.suspended;
        nativeWindow->fullscreenWidth = snapshot.width;
        nativeWindow->fullscreenHeight = snapshot.height;
        nativeWindow->mode = WindowMode::Fullscreen;
        return WindowResult::Success;
    }

    void Window::rollbackFullscreenMode(WindowMode previousMode, const FullscreenModeSnapshot &snapshot)
    {
        WindowResult rollbackResult = WindowResult::ModeChangeFailed;
        switch (previousMode)
        {
        case WindowMode::Windowed:
            rollbackResult = applyWindowedMode();
            break;
        case WindowMode::BorderlessFullscreen:
            rollbackResult = applyBorderlessFullscreenMode();
            break;
        case WindowMode::Fullscreen:
            rollbackResult = restoreFullscreenModeSnapshot(snapshot);
            break;
        default:
            break;
        }

        if (rollbackResult != WindowResult::Success)
        {
            recordResult(WindowResult::ModeChangeFailed);
        }
    }

    WindowResult Window::resolveFullscreenModeTarget(const FullscreenModeSnapshot &previousState, FullscreenModeTarget &outTarget)
    {
        outTarget = {};
        outTarget.monitor = nativeWindow->hasRequestedFullscreenDisplayMode
                                ? static_cast<HMONITOR>(nativeWindow->requestedFullscreenMonitor.handle)
                                : MonitorFromWindow(nativeWindow->handle, MONITOR_DEFAULTTONEAREST);

        if (outTarget.monitor == nullptr)
        {
            return recordResult(WindowResult::InvalidMonitor);
        }

        outTarget.monitorInfo.cbSize = sizeof(outTarget.monitorInfo);
        if (!GetMonitorInfoW(outTarget.monitor, &outTarget.monitorInfo))
        {
            return recordResult(WindowResult::PlatformCallFailed, GetLastError());
        }

        std::copy(
            std::begin(outTarget.monitorInfo.szDevice),
            std::end(outTarget.monitorInfo.szDevice),
            std::begin(outTarget.deviceName));

        unsigned long monitorError = 0;
        if (!buildMonitorInfo(outTarget.monitor, outTarget.publicMonitorInfo, monitorError))
        {
            return recordResult(WindowResult::InvalidMonitor, monitorError);
        }

        std::wstring targetDeviceNameText = outTarget.deviceName;
        std::wstring previousDeviceNameText = previousState.deviceName;
        outTarget.savedModeIsFromDifferentDevice = previousState.hasSavedDisplayMode && targetDeviceNameText != previousDeviceNameText;

        if (previousState.hasSavedDisplayMode && !outTarget.savedModeIsFromDifferentDevice)
        {
            outTarget.savedDisplayMode = previousState.savedDisplayMode;
        }
        else
        {
            outTarget.savedDisplayMode.dmSize = sizeof(outTarget.savedDisplayMode);
            if (!EnumDisplaySettingsW(outTarget.deviceName, ENUM_CURRENT_SETTINGS, &outTarget.savedDisplayMode))
            {
                return recordResult(WindowResult::PlatformCallFailed, GetLastError());
            }
        }

        if (nativeWindow->hasRequestedFullscreenDisplayMode)
        {
            outTarget.displayMode = nativeWindow->requestedFullscreenDisplayMode;
            return recordResult(WindowResult::Success);
        }

        std::vector<DisplayMode> displayModes;
        WindowResult displayModesResult = getDisplayModes(outTarget.publicMonitorInfo, displayModes);
        if (displayModesResult != WindowResult::Success)
        {
            return displayModesResult;
        }

        if (!chooseHighestRefreshDisplayMode(
                displayModes,
                nativeWindow->requestedClientWidth,
                nativeWindow->requestedClientHeight,
                outTarget.displayMode))
        {
            return recordResult(WindowResult::InvalidDisplayMode);
        }

        return recordResult(WindowResult::Success);
    }

    WindowResult Window::applyFullscreenModeTarget(
        const FullscreenModeTarget &target,
        WindowMode previousMode,
        const FullscreenModeSnapshot &previousState)
    {
        if (target.savedModeIsFromDifferentDevice && !previousState.suspended)
        {
            DEVMODEW previousSavedDisplayMode = previousState.savedDisplayMode;
            LONG restorePreviousDisplayResult = ChangeDisplaySettingsExW(
                previousState.deviceName,
                &previousSavedDisplayMode,
                nullptr,
                0,
                nullptr);
            if (restorePreviousDisplayResult != DISP_CHANGE_SUCCESSFUL)
            {
                return recordResult(WindowResult::ModeChangeFailed);
            }
        }

        DEVMODEW fullscreenMode = target.savedDisplayMode;
        applyDisplayModeToDevMode(fullscreenMode, target.displayMode, true);

        LONG displayResult = ChangeDisplaySettingsExW(target.deviceName, &fullscreenMode, nullptr, CDS_FULLSCREEN, nullptr);
        if (displayResult != DISP_CHANGE_SUCCESSFUL)
        {
            rollbackFullscreenMode(previousMode, previousState);
            return recordResult(WindowResult::ModeChangeFailed);
        }

        DWORD fullscreenStyle = nativeWindow->windowedStyle;
        fullscreenStyle &= ~WS_OVERLAPPEDWINDOW;
        fullscreenStyle |= WS_POPUP;

        unsigned long styleError = 0;
        if (!setWindowStyle(nativeWindow->handle, fullscreenStyle, styleError))
        {
            restoreDisplayModeForDevice(target.deviceName, target.savedDisplayMode);
            rollbackFullscreenMode(previousMode, previousState);
            return recordResult(WindowResult::PlatformCallFailed, styleError);
        }

        if (!SetWindowPos(
                nativeWindow->handle,
                HWND_TOP,
                target.monitorInfo.rcMonitor.left,
                target.monitorInfo.rcMonitor.top,
                target.displayMode.width,
                target.displayMode.height,
                SWP_FRAMECHANGED | SWP_SHOWWINDOW))
        {
            unsigned long error = GetLastError();
            restoreDisplayModeForDevice(target.deviceName, target.savedDisplayMode);
            rollbackFullscreenMode(previousMode, previousState);
            return recordResult(WindowResult::PlatformCallFailed, error);
        }

        return WindowResult::Success;
    }

    void Window::storeFullscreenModeTarget(const FullscreenModeTarget &target)
    {
        std::copy(
            std::begin(target.deviceName),
            std::end(target.deviceName),
            std::begin(nativeWindow->fullscreenDeviceName));
        nativeWindow->savedDisplayMode = target.savedDisplayMode;
        nativeWindow->activeFullscreenMonitor = target.publicMonitorInfo;
        nativeWindow->activeFullscreenDisplayMode = target.displayMode;
        nativeWindow->activeFullscreenModeIsExact = true;
        nativeWindow->hasSavedDisplayMode = true;
        nativeWindow->fullscreenRect = target.monitorInfo.rcMonitor;
        nativeWindow->fullscreenWidth = target.displayMode.width;
        nativeWindow->fullscreenHeight = target.displayMode.height;
        nativeWindow->mode = WindowMode::Fullscreen;
        nativeWindow->fullscreenSuspended = false;
    }

    WindowResult Window::applyFullscreenMode()
    {
        WindowMode previousMode = nativeWindow->mode;
        FullscreenModeSnapshot previousState = captureFullscreenModeSnapshot();
        FullscreenModeTarget target{};

        WindowResult targetResult = resolveFullscreenModeTarget(previousState, target);
        if (targetResult != WindowResult::Success)
        {
            return targetResult;
        }

        WindowResult saveResult = saveWindowedPlacement();
        if (saveResult != WindowResult::Success)
        {
            return saveResult;
        }

        WindowResult applyResult = applyFullscreenModeTarget(target, previousMode, previousState);
        if (applyResult != WindowResult::Success)
        {
            return applyResult;
        }

        storeFullscreenModeTarget(target);
        return recordResult(WindowResult::Success);
    }

    // Cursor helpers

    WindowResult Window::releaseCursorConfinement(unsigned long *outWin32Error)
    {
        if (outWin32Error != nullptr)
        {
            *outWin32Error = 0;
        }

        if (nativeWindow == nullptr || !nativeWindow->cursorClipApplied)
        {
            return WindowResult::Success;
        }

        if (!ClipCursor(nullptr))
        {
            if (outWin32Error != nullptr)
            {
                *outWin32Error = GetLastError();
            }
            return WindowResult::PlatformCallFailed;
        }

        nativeWindow->cursorClipApplied = false;
        return WindowResult::Success;
    }

    void Window::updateCursorConfinement()
    {
        if (nativeWindow == nullptr)
        {
            return;
        }

        if (!isCursorConfinementAllowedForRole(nativeWindow->role) || !nativeWindow->cursorConfined || nativeWindow->handle == nullptr || !nativeWindow->isFocused || nativeWindow->isMinimized)
        {
            unsigned long releaseError = 0;
            if (releaseCursorConfinement(&releaseError) != WindowResult::Success)
            {
                recordAsyncError(WindowResult::PlatformCallFailed, releaseError);
            }
            return;
        }

        RECT clientRect{};
        if (!GetClientRect(nativeWindow->handle, &clientRect))
        {
            recordAsyncError(WindowResult::PlatformCallFailed, GetLastError());
            unsigned long releaseError = 0;
            if (releaseCursorConfinement(&releaseError) != WindowResult::Success)
            {
                recordAsyncError(WindowResult::PlatformCallFailed, releaseError);
            }
            return;
        }

        POINT topLeft{clientRect.left, clientRect.top};
        POINT bottomRight{clientRect.right, clientRect.bottom};

        if (!ClientToScreen(nativeWindow->handle, &topLeft) || !ClientToScreen(nativeWindow->handle, &bottomRight))
        {
            recordAsyncError(WindowResult::PlatformCallFailed, GetLastError());
            unsigned long releaseError = 0;
            if (releaseCursorConfinement(&releaseError) != WindowResult::Success)
            {
                recordAsyncError(WindowResult::PlatformCallFailed, releaseError);
            }
            return;
        }

        RECT screenRect{topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};

        if (!ClipCursor(&screenRect))
        {
            recordAsyncError(WindowResult::PlatformCallFailed, GetLastError());
            return;
        }

        nativeWindow->cursorClipApplied = true;
    }

    // Monitor info

    WindowResult Window::getMonitors(std::vector<MonitorInfo> &outMonitors)
    {
        outMonitors.clear();

        if (nativeWindow == nullptr)
        {
            return recordResult(WindowResult::NotCreated);
        }

        if (!nativeWindow->monitorCache.empty())
        {
            outMonitors = nativeWindow->monitorCache;
            return recordResult(WindowResult::Success);
        }

        std::vector<MonitorInfo> monitors;
        MonitorEnumerationContext context{.monitors = &monitors};
        if (EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, reinterpret_cast<LPARAM>(&context)) == FALSE)
        {
            return recordResult(WindowResult::PlatformCallFailed, context.error);
        }

        nativeWindow->monitorCache = monitors;
        outMonitors = nativeWindow->monitorCache;
        return recordResult(WindowResult::Success);
    }

    WindowResult Window::getCurrentMonitor(MonitorInfo &outMonitor)
    {
        outMonitor = {};

        if (nativeWindow == nullptr || nativeWindow->handle == nullptr)
        {
            return recordResult(WindowResult::NotCreated);
        }

        HMONITOR hMonitor = MonitorFromWindow(nativeWindow->handle, MONITOR_DEFAULTTONEAREST);
        if (hMonitor == nullptr)
        {
            return recordResult(WindowResult::PlatformCallFailed, GetLastError());
        }

        unsigned long error = 0;
        if (!buildMonitorInfo(hMonitor, outMonitor, error))
        {
            return recordResult(WindowResult::PlatformCallFailed, error);
        }

        return recordResult(WindowResult::Success);
    }

    WindowResult Window::getDisplayModes(const MonitorInfo &monitor, std::vector<DisplayMode> &outModes)
    {
        outModes.clear();

        if (nativeWindow == nullptr || monitor.handle == nullptr || monitor.deviceName.empty())
        {
            return recordResult(nativeWindow == nullptr ? WindowResult::NotCreated : WindowResult::InvalidMonitor);
        }

        auto cacheIt = nativeWindow->displayModeCache.find(monitor.deviceName);
        if (cacheIt != nativeWindow->displayModeCache.end())
        {
            outModes = cacheIt->second;
            return recordResult(WindowResult::Success);
        }

        std::vector<DisplayMode> modes;

        nativeWindow->utf16Scratch.clear();
        if (!utf8ToWide(monitor.deviceName, nativeWindow->utf16Scratch))
        {
            return recordResult(WindowResult::InvalidMonitor, GetLastError());
        }

        DWORD modeNum = 0;
        for (;;)
        {
            DEVMODEW devMode{};
            devMode.dmSize = sizeof(devMode);
            SetLastError(0);
            if (EnumDisplaySettingsW(nativeWindow->utf16Scratch.c_str(), modeNum, &devMode) == FALSE)
            {
                if (modeNum == 0)
                {
                    return recordResult(WindowResult::InvalidMonitor, GetLastError());
                }

                break;
            }

            DisplayMode mode{
                static_cast<int>(devMode.dmPelsWidth),
                static_cast<int>(devMode.dmPelsHeight),
                static_cast<int>(devMode.dmDisplayFrequency),
                static_cast<int>(devMode.dmBitsPerPel)};

            if (!containsDisplayMode(modes, mode))
            {
                modes.push_back(mode);
            }

            modeNum++;
        }

        nativeWindow->displayModeCache.emplace(monitor.deviceName, std::move(modes));
        outModes = nativeWindow->displayModeCache.find(monitor.deviceName)->second;
        return recordResult(WindowResult::Success);
    }

    WindowResult Window::getCurrentDisplayMode(DisplayMode &outMode)
    {
        outMode = {};

        if (nativeWindow == nullptr || nativeWindow->handle == nullptr)
        {
            return recordResult(WindowResult::NotCreated);
        }

        MonitorInfo monitor{};
        WindowResult monitorResult = getCurrentMonitor(monitor);
        if (monitorResult != WindowResult::Success)
        {
            return monitorResult;
        }

        if (monitor.deviceName.empty())
        {
            return recordResult(WindowResult::InvalidMonitor);
        }

        nativeWindow->utf16Scratch.clear();
        if (!utf8ToWide(monitor.deviceName, nativeWindow->utf16Scratch))
        {
            return recordResult(WindowResult::InvalidMonitor, GetLastError());
        }

        DEVMODEW devMode = {};
        devMode.dmSize = sizeof(DEVMODEW);
        if (EnumDisplaySettingsW(nativeWindow->utf16Scratch.c_str(), ENUM_CURRENT_SETTINGS, &devMode) == FALSE)
        {
            return recordResult(WindowResult::PlatformCallFailed, GetLastError());
        }

        outMode = DisplayMode{
            static_cast<int>(devMode.dmPelsWidth),
            static_cast<int>(devMode.dmPelsHeight),
            static_cast<int>(devMode.dmDisplayFrequency),
            static_cast<int>(devMode.dmBitsPerPel)};
        return recordResult(WindowResult::Success);
    }

    WindowResult Window::setFullscreenDisplayMode(const MonitorInfo &monitor, const DisplayMode &mode)
    {
        if (nativeWindow == nullptr || nativeWindow->handle == nullptr)
        {
            return recordResult(WindowResult::NotCreated);
        }

        if (nativeWindow->role != WindowRole::MainGame)
        {
            return recordResult(WindowResult::OperationNotAllowed);
        }

        if (monitor.handle == nullptr || monitor.deviceName.empty())
        {
            return recordResult(WindowResult::InvalidMonitor);
        }

        if (!isCompleteDisplayMode(mode))
        {
            return recordResult(WindowResult::InvalidDisplayMode);
        }

        std::vector<DisplayMode> modes;
        WindowResult modesResult = getDisplayModes(monitor, modes);
        if (modesResult != WindowResult::Success)
        {
            return modesResult;
        }

        if (!containsDisplayMode(modes, mode))
        {
            return recordResult(WindowResult::InvalidDisplayMode);
        }

        MonitorInfo previousMonitor = nativeWindow->requestedFullscreenMonitor;
        DisplayMode previousMode = nativeWindow->requestedFullscreenDisplayMode;
        bool hadPreviousMode = nativeWindow->hasRequestedFullscreenDisplayMode;

        nativeWindow->requestedFullscreenMonitor = monitor;
        nativeWindow->requestedFullscreenDisplayMode = mode;
        nativeWindow->hasRequestedFullscreenDisplayMode = true;

        if (nativeWindow->mode == WindowMode::Fullscreen)
        {
            WindowResult applyResult = applyFullscreenMode();
            if (applyResult != WindowResult::Success)
            {
                nativeWindow->requestedFullscreenMonitor = previousMonitor;
                nativeWindow->requestedFullscreenDisplayMode = previousMode;
                nativeWindow->hasRequestedFullscreenDisplayMode = hadPreviousMode;
                return applyResult;
            }
        }

        return recordResult(WindowResult::Success);
    }

    WindowResult Window::getFullscreenDisplayMode(MonitorInfo &outMonitor, DisplayMode &outMode)
    {
        outMonitor = {};
        outMode = {};

        if (nativeWindow == nullptr || nativeWindow->handle == nullptr)
        {
            return recordResult(WindowResult::NotCreated);
        }

        if (nativeWindow->hasRequestedFullscreenDisplayMode)
        {
            outMonitor = nativeWindow->requestedFullscreenMonitor;
            outMode = nativeWindow->requestedFullscreenDisplayMode;
            return recordResult(WindowResult::Success);
        }

        if (nativeWindow->mode == WindowMode::Fullscreen &&
            nativeWindow->activeFullscreenMonitor.handle != nullptr &&
            isCompleteDisplayMode(nativeWindow->activeFullscreenDisplayMode))
        {
            outMonitor = nativeWindow->activeFullscreenMonitor;
            outMode = nativeWindow->activeFullscreenDisplayMode;
            return recordResult(WindowResult::Success);
        }

        WindowResult monitorResult = getCurrentMonitor(outMonitor);
        if (monitorResult != WindowResult::Success)
        {
            return monitorResult;
        }

        std::vector<DisplayMode> modes;
        WindowResult modesResult = getDisplayModes(outMonitor, modes);
        if (modesResult != WindowResult::Success)
        {
            return modesResult;
        }

        if (!chooseHighestRefreshDisplayMode(
                modes,
                nativeWindow->requestedClientWidth,
                nativeWindow->requestedClientHeight,
                outMode))
        {
            return recordResult(WindowResult::InvalidDisplayMode);
        }

        return recordResult(WindowResult::Success);
    }
}


