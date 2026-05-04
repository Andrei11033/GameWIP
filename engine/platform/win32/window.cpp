#include "window.h"
#include "input/platform/win32/win32_input.h"

#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <deque>
#include <iterator>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace GameWIP::Platform::Win32::Internal
{
    struct WindowMessageAccess // Forwards Win32 messages from the callback into private Window handlers.
    {
        /// @brief Forwards a native resize message.
        static void handleResize(Window &window, int width, int height);

        /// @brief Forwards a native move message.
        static void handleMove(Window &window, int x, int y);

        /// @brief Forwards a native focus change message.
        static void handleFocusChange(Window &window, bool focused);

        /// @brief Forwards a native minimize change message.
        static void handleMinimizeChange(Window &window, bool minimized);

        /// @brief Forwards a native DPI change message.
        static void handleDpiChange(Window &window, unsigned int dpi, int suggestedLeft, int suggestedTop, int suggestedRight, int suggestedBottom);

        /// @brief Forwards a native display configuration change message.
        static void handleDisplayChange(Window &window);

        /// @brief Forwards a WM_GETMINMAXINFO message.
        static void handleGetMinMaxInfo(Window &window, MINMAXINFO *minMaxInfo);

        /// @brief Returns the cursor handle used by the window procedure.
        static HCURSOR getCursorHandle(const Window &window);
    };
}

namespace
{
    namespace Win32Input = GameWIP::Input::Platform::Win32;
    namespace Win32Internal = GameWIP::Platform::Win32::Internal;
    using Window = GameWIP::Platform::Win32::Window;
    using GameWIP::Platform::Win32::DisplayMode;
    using GameWIP::Platform::Win32::MonitorInfo;
    using GameWIP::Platform::Win32::Rect;

    // DPI helpers

    constexpr unsigned int defaultDpi = 96;                              // Win32's baseline DPI.
    constexpr int unlimitedClientSize = std::numeric_limits<int>::max(); // Sentinel for unconstrained max client size.

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
        HMODULE user32 = GetModuleHandleA("user32.dll");
        if (user32 == nullptr)
        {
            return nullptr;
        }

        return GetProcAddress(user32, name);
    }

    /// @brief Returns the system DPI, falling back to standard 96 DPI.
    /// @return System DPI value.
    unsigned int getSystemDpi()
    {
        auto getDpiForSystem = reinterpret_cast<GetDpiForSystemFn>(getUser32Function("GetDpiForSystem"));
        if (getDpiForSystem != nullptr)
        {
            return getDpiForSystem();
        }

        return defaultDpi;
    }

    /// @brief Returns the DPI for a window, falling back to system DPI.
    /// @param handle Window handle to query.
    /// @return Window DPI value.
    unsigned int getWindowDpi(HWND handle)
    {
        auto getDpiForWindow = reinterpret_cast<GetDpiForWindowFn>(getUser32Function("GetDpiForWindow"));
        if (getDpiForWindow != nullptr && handle != nullptr)
        {
            return getDpiForWindow(handle);
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
        auto adjustForDpi = reinterpret_cast<AdjustWindowRectExForDpiFn>(getUser32Function("AdjustWindowRectExForDpi"));
        if (adjustForDpi != nullptr)
        {
            return adjustForDpi(&windowRect, style, FALSE, extendedStyle, dpi) != FALSE;
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

        auto setDpiAwarenessContext = reinterpret_cast<SetProcessDpiAwarenessContextFn>(getUser32Function("SetProcessDpiAwarenessContext"));
        if (setDpiAwarenessContext != nullptr)
        {
            HANDLE perMonitorAwareV2 = reinterpret_cast<HANDLE>(static_cast<intptr_t>(-4));
            HANDLE perMonitorAware = reinterpret_cast<HANDLE>(static_cast<intptr_t>(-3));
            if (setDpiAwarenessContext(perMonitorAwareV2) || setDpiAwarenessContext(perMonitorAware))
            {
                return;
            }
        }

        auto setProcessDPIAware = reinterpret_cast<SetProcessDPIAwareFn>(getUser32Function("SetProcessDPIAware"));
        if (setProcessDPIAware != nullptr)
        {
            setProcessDPIAware();
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

    /// @brief Win32 window procedure to handle messages for the game window.
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
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));

            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
        case WM_CLOSE:
        {
            auto window = reinterpret_cast<Window *>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
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
            PostQuitMessage(0);
            return 0;
        }
        // Size and position
        case WM_SIZE:
        {
            auto window = reinterpret_cast<Window *>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
            int width = LOWORD(lParam);
            int height = HIWORD(lParam);
            if (window != nullptr)
            {
                bool minimized = wParam == SIZE_MINIMIZED;
                Win32Internal::WindowMessageAccess::handleMinimizeChange(*window, minimized);
                Win32Internal::WindowMessageAccess::handleResize(*window, width, height);
            }
            return 0;
        }
        case WM_MOVE:
        {
            auto window = reinterpret_cast<Window *>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
            if (window != nullptr)
            {
                int x = static_cast<int>(static_cast<short>(LOWORD(lParam)));
                int y = static_cast<int>(static_cast<short>(HIWORD(lParam)));
                Win32Internal::WindowMessageAccess::handleMove(*window, x, y);
            }
            return 0;
        }
        case WM_DPICHANGED:
        {
            auto window = reinterpret_cast<Window *>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
            auto suggestedRect = reinterpret_cast<RECT *>(lParam);
            if (window != nullptr && suggestedRect != nullptr)
            {
                unsigned int dpi = static_cast<unsigned int>(HIWORD(wParam));
                Win32Internal::WindowMessageAccess::handleDpiChange(
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
            auto window = reinterpret_cast<Window *>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
            if (window != nullptr)
            {
                Win32Internal::WindowMessageAccess::handleDisplayChange(*window);
            }
            return 0;
        }
        case WM_GETMINMAXINFO:
        {
            auto window = reinterpret_cast<Window *>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
            auto minMaxInfo = reinterpret_cast<MINMAXINFO *>(lParam);
            if (window != nullptr && minMaxInfo != nullptr)
            {
                Win32Internal::WindowMessageAccess::handleGetMinMaxInfo(*window, minMaxInfo);
            }
            return 0;
        }
        // Focus
        case WM_SETFOCUS:
        {
            auto window = reinterpret_cast<Window *>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
            if (window != nullptr)
            {
                Win32Internal::WindowMessageAccess::handleFocusChange(*window, true);
            }
            return 0;
        }
        case WM_KILLFOCUS:
        {
            auto window = reinterpret_cast<Window *>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
            if (window != nullptr)
            {
                Win32Internal::WindowMessageAccess::handleFocusChange(*window, false);
            }
            return 0;
        }
        // Cursor
        case WM_SETCURSOR:
        {
            auto window = reinterpret_cast<Window *>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
            if (window != nullptr && LOWORD(lParam) == HTCLIENT)
            {
                SetCursor(window->isCursorVisible() ? Win32Internal::WindowMessageAccess::getCursorHandle(*window) : nullptr);
                return TRUE;
            }

            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }
    }

    struct MonitorEnumerationContext // Temporary data shared with EnumDisplayMonitors.
    {
        std::vector<MonitorInfo> *monitors = nullptr; // Destination list.
        unsigned long error = 0;                      // First Win32/conversion error.
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

namespace GameWIP::Platform::Win32
{
    struct Window::NativeWindow // Holds Win32-specific window data hidden behind Window.
    {
        // Native handles
        HINSTANCE instance = nullptr; // Module instance used to register and create the window.
        HWND handle = nullptr;        // Native Win32 window handle.

        // Lifecycle state
        bool closeRequested = false;    // True once the game loop should exit.
        bool isFocused = false;         // True while this window has keyboard focus.
        bool isMinimized = false;       // True while the window is minimized.
        bool clientSizeChanged = false; // True until the game handles the latest client resize.

        // Events
        std::deque<WindowEvent> events{}; // Pending queued window events.

        // Client size
        int clientWidth = 0;           // Current client-area width.
        int clientHeight = 0;          // Current client-area height.
        int requestedClientWidth = 0;  // Desired game width for windowed sizing and fullscreen.
        int requestedClientHeight = 0; // Desired game height for windowed sizing and fullscreen.
        unsigned int dpi = defaultDpi; // DPI used for client-to-window size calculations.

        // Monitor/display caches
        std::vector<MonitorInfo> monitorCache;                                          // Cached monitor enumeration results.
        std::vector<std::pair<std::string, std::vector<DisplayMode>>> displayModeCache; // Cached supported display modes.

        // Window mode
        WindowMode mode = WindowMode::Windowed; // Current window mode.

        // Windowed placement
        RECT windowedRect{};                    // Saved windowed outer rectangle.
        DWORD windowedStyle = 0;                // Saved decorated window style.
        DWORD windowedExtendedStyle = 0;        // Saved decorated extended style.
        bool hasSavedWindowedPlacement = false; // True when windowedRect/style can be restored.

        // Exclusive fullscreen
        char fullscreenDeviceName[CCHDEVICENAME]{}; // Display device used for exclusive fullscreen.
        DEVMODEA savedDisplayMode{};                // Desktop display mode to restore after fullscreen.
        bool hasSavedDisplayMode = false;           // True when savedDisplayMode is valid.
        bool fullscreenSuspended = false;           // True when fullscreen was temporarily released on focus loss.
        int fullscreenWidth = 0;                    // Active exclusive fullscreen width.
        int fullscreenHeight = 0;                   // Active exclusive fullscreen height.

        // Client size constraints
        int minClientWidth = 0;                    // Minimum requested client width.
        int minClientHeight = 0;                   // Minimum requested client height.
        int maxClientWidth = unlimitedClientSize;  // Maximum requested client width.
        int maxClientHeight = unlimitedClientSize; // Maximum requested client height.

        // Cursor
        HCURSOR arrowCursor = nullptr;  // Default cursor shown over the client area.
        bool cursorVisible = true;      // Desired cursor visibility over the client area.
        bool cursorConfined = false;    // Desired cursor confinement to the client area.
        bool cursorClipApplied = false; // True when ClipCursor currently confines the cursor.

        // Window text caching
        std::string title; // Cached current window title.
    };

    namespace Internal
    {
        // Native message bridge

        void WindowMessageAccess::handleResize(Window &window, int width, int height)
        {
            window.handleResize(width, height);
        }

        void WindowMessageAccess::handleMove(Window &window, int x, int y)
        {
            window.handleMove(x, y);
        }

        void WindowMessageAccess::handleFocusChange(Window &window, bool focused)
        {
            window.handleFocusChange(focused);
        }

        void WindowMessageAccess::handleMinimizeChange(Window &window, bool minimized)
        {
            window.handleMinimizeChange(minimized);
        }

        void WindowMessageAccess::handleDpiChange(Window &window, unsigned int dpi, int suggestedLeft, int suggestedTop, int suggestedRight, int suggestedBottom)
        {
            window.handleDpiChange(dpi, suggestedLeft, suggestedTop, suggestedRight, suggestedBottom);
        }

        void WindowMessageAccess::handleDisplayChange(Window &window)
        {
            window.handleDisplayChange();
        }

        void WindowMessageAccess::handleGetMinMaxInfo(Window &window, MINMAXINFO *minMaxInfo)
        {
            window.handleGetMinMaxInfo(minMaxInfo);
        }

        HCURSOR WindowMessageAccess::getCursorHandle(const Window &window)
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
            windowStyle = static_cast<DWORD>(GetWindowLongPtrA(nativeWindow->handle, GWL_STYLE));
            extendedWindowStyle = static_cast<DWORD>(GetWindowLongPtrA(nativeWindow->handle, GWL_EXSTYLE));
        }

        RECT frameRect{0, 0, 0, 0};
        if (!adjustWindowRectForDpi(frameRect, windowStyle, extendedWindowStyle, nativeWindow->dpi))
        {
            recordResult(WindowResult::Win32CallFailed, GetLastError());
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
        lastWin32Error = win32Error;
        return result;
    }

    // Event helpers

    void Window::pushEvent(const WindowEvent &event)
    {
        if (nativeWindow != nullptr)
        {
            nativeWindow->events.push_back(event);
        }
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
        nativeWindow->requestedClientWidth = description.width;
        nativeWindow->requestedClientHeight = description.height;
        nativeWindow->dpi = getSystemDpi();

        if (nativeWindow->instance == nullptr)
        {
            unsigned long error = GetLastError();
            delete nativeWindow;
            nativeWindow = nullptr;
            return recordResult(WindowResult::Win32CallFailed, error);
        }

        nativeWindow->arrowCursor = LoadCursorA(nullptr, IDC_ARROW);

        std::wstring title;
        if (!utf8ToWide(description.title, title))
        {
            unsigned long error = GetLastError();
            destroy();
            return recordResult(WindowResult::Win32CallFailed, error);
        }

        const wchar_t *className = L"GameWIPWindowClass";

        WNDCLASSW windowClass{};

        windowClass.lpfnWndProc = windowProc;
        windowClass.hInstance = nativeWindow->instance;
        windowClass.lpszClassName = className;
        windowClass.hCursor = nativeWindow->arrowCursor;

        if (RegisterClassW(&windowClass) == 0)
        {
            DWORD error = GetLastError();
            if (error != ERROR_CLASS_ALREADY_EXISTS)
            {
                destroy();
                return recordResult(WindowResult::Win32CallFailed, error);
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
            return recordResult(WindowResult::Win32CallFailed, error);
        }

        int outerWidth = windowRect.right - windowRect.left;
        int outerHeight = windowRect.bottom - windowRect.top;

        nativeWindow->handle = CreateWindowExW(extendedWindowStyle, className, title.c_str(), windowStyle, CW_USEDEFAULT, CW_USEDEFAULT, outerWidth, outerHeight, nullptr, nullptr, nativeWindow->instance, this);

        if (nativeWindow->handle == nullptr)
        {
            unsigned long error = GetLastError();
            destroy();
            return recordResult(WindowResult::Win32CallFailed, error);
        }

        unsigned long rawInputError = 0;
        if (!Win32Input::registerInputDevices(nativeWindow->handle, rawInputError))
        {
            destroy();
            return recordResult(WindowResult::Win32CallFailed, rawInputError);
        }

        nativeWindow->title = description.title;
        nativeWindow->dpi = getWindowDpi(nativeWindow->handle);

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
            unsigned long error = lastWin32Error;
            destroy();
            return recordResult(modeResult, error);
        }

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
        updateCursorConfinement();

        if (nativeWindow->mode != WindowMode::Windowed)
        {
            WindowResult modeResult = setMode(WindowMode::Windowed);
            if (modeResult != WindowResult::Success)
            {
                finalResult = modeResult;
                finalError = lastWin32Error;
            }
        }

        if (nativeWindow->handle != nullptr)
        {
            if (!DestroyWindow(nativeWindow->handle) && finalResult == WindowResult::Success)
            {
                finalResult = WindowResult::Win32CallFailed;
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
            Win32Input::handleMessage(message.message, message.wParam, message.lParam, inputState);
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    bool Window::popEvent(WindowEvent &outEvent)
    {
        if (nativeWindow == nullptr || nativeWindow->events.empty())
        {
            return false;
        }

        outEvent = nativeWindow->events.front();
        nativeWindow->events.pop_front();
        return true;
    }

    void Window::clearEvents()
    {
        if (nativeWindow != nullptr)
        {
            nativeWindow->events = std::deque<WindowEvent>{};
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
        }
    }

    void Window::setMaxClientSize(int width, int height)
    {
        if (nativeWindow != nullptr)
        {
            nativeWindow->maxClientWidth = width <= 0 ? unlimitedClientSize : std::max(width, nativeWindow->minClientWidth);
            nativeWindow->maxClientHeight = height <= 0 ? unlimitedClientSize : std::max(height, nativeWindow->minClientHeight);
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

        nativeWindow->requestedClientWidth = width;
        nativeWindow->requestedClientHeight = height;

        switch (nativeWindow->mode)
        {
        case WindowMode::Windowed:
        {
            if (IsZoomed(nativeWindow->handle))
            {
                ShowWindow(nativeWindow->handle, SW_RESTORE);
            }

            DWORD windowStyle = static_cast<DWORD>(GetWindowLongPtrA(nativeWindow->handle, GWL_STYLE));
            DWORD extendedWindowStyle = static_cast<DWORD>(GetWindowLongPtrA(nativeWindow->handle, GWL_EXSTYLE));
            nativeWindow->dpi = getWindowDpi(nativeWindow->handle);

            RECT windowRect{0, 0, width, height};

            if (!adjustWindowRectForDpi(windowRect, windowStyle, extendedWindowStyle, nativeWindow->dpi))
            {
                return recordResult(WindowResult::Win32CallFailed, GetLastError());
            }

            int outerWidth = windowRect.right - windowRect.left;
            int outerHeight = windowRect.bottom - windowRect.top;

            if (!SetWindowPos(nativeWindow->handle, nullptr, 0, 0, outerWidth, outerHeight, SWP_NOMOVE | SWP_NOZORDER | SWP_FRAMECHANGED))
            {
                return recordResult(WindowResult::Win32CallFailed, GetLastError());
            }

            updateCursorConfinement();
            return recordResult(WindowResult::Success);
        }
        case WindowMode::BorderlessFullscreen:
            updateCursorConfinement();
            return recordResult(WindowResult::Success);
        case WindowMode::Fullscreen:
        {
            if (!nativeWindow->hasSavedDisplayMode)
            {
                return recordResult(WindowResult::MissingDisplayMode);
            }

            DEVMODEA fullscreenMode = nativeWindow->savedDisplayMode;
            fullscreenMode.dmPelsWidth = static_cast<DWORD>(width);
            fullscreenMode.dmPelsHeight = static_cast<DWORD>(height);
            fullscreenMode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;

            LONG displayResult = ChangeDisplaySettingsExA(
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
                    0,
                    0,
                    width,
                    height,
                    SWP_FRAMECHANGED | SWP_SHOWWINDOW))
            {
                return recordResult(WindowResult::Win32CallFailed, GetLastError());
            }

            nativeWindow->fullscreenWidth = width;
            nativeWindow->fullscreenHeight = height;

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

        std::wstring wideTitle;
        if (!utf8ToWide(title, wideTitle))
        {
            return recordResult(WindowResult::Win32CallFailed, GetLastError());
        }

        if (SetWindowTextW(nativeWindow->handle, wideTitle.c_str()) == FALSE)
        {
            return recordResult(WindowResult::Win32CallFailed, GetLastError());
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

    // Errors

    WindowResult Window::getLastResult() const
    {
        return lastResult;
    }

    unsigned long Window::getLastWin32Error() const
    {
        return lastWin32Error;
    }

    // Window mode

    WindowResult Window::setMode(WindowMode mode)
    {
        if (nativeWindow == nullptr || nativeWindow->handle == nullptr)
        {
            return recordResult(WindowResult::NotCreated);
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

    void Window::setCursorConfined(bool confined)
    {
        if (nativeWindow == nullptr)
        {
            return;
        }

        nativeWindow->cursorConfined = confined;
        updateCursorConfinement();
    }

    bool Window::isCursorConfined() const
    {
        return nativeWindow != nullptr && nativeWindow->cursorConfined;
    }

    void Window::setCursorMode(CursorMode mode)
    {
        switch (mode)
        {
        case CursorMode::FreeVisible:
            setCursorVisible(true);
            setCursorConfined(false);
            break;
        case CursorMode::FreeHidden:
            setCursorVisible(false);
            setCursorConfined(false);
            break;
        case CursorMode::ConfinedVisible:
            setCursorVisible(true);
            setCursorConfined(true);
            break;
        case CursorMode::ConfinedHidden:
            setCursorVisible(false);
            setCursorConfined(true);
            break;
        default:
            break;
        }
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
            LONG displayResult = ChangeDisplaySettingsExA(
                nativeWindow->fullscreenDeviceName,
                &nativeWindow->savedDisplayMode,
                nullptr,
                0,
                nullptr);

            if (displayResult != DISP_CHANGE_SUCCESSFUL)
            {
                recordResult(WindowResult::ModeChangeFailed);
                return;
            }

            ShowWindow(nativeWindow->handle, SW_MINIMIZE);
            nativeWindow->isMinimized = true;
            nativeWindow->fullscreenSuspended = true;
        }
        else if (focused && nativeWindow->mode == WindowMode::Fullscreen && nativeWindow->fullscreenSuspended && nativeWindow->hasSavedDisplayMode)
        {
            ShowWindow(nativeWindow->handle, SW_RESTORE);

            DEVMODEA fullscreenMode = nativeWindow->savedDisplayMode;
            fullscreenMode.dmPelsWidth = static_cast<DWORD>(nativeWindow->fullscreenWidth);
            fullscreenMode.dmPelsHeight = static_cast<DWORD>(nativeWindow->fullscreenHeight);
            fullscreenMode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;

            LONG displayResult = ChangeDisplaySettingsExA(
                nativeWindow->fullscreenDeviceName,
                &fullscreenMode,
                nullptr,
                CDS_FULLSCREEN,
                nullptr);

            if (displayResult != DISP_CHANGE_SUCCESSFUL)
            {
                recordResult(WindowResult::ModeChangeFailed);
                return;
            }

            if (!SetWindowPos(
                    nativeWindow->handle,
                    HWND_TOP,
                    0,
                    0,
                    nativeWindow->fullscreenWidth,
                    nativeWindow->fullscreenHeight,
                    SWP_FRAMECHANGED | SWP_SHOWWINDOW))
            {
                recordResult(WindowResult::Win32CallFailed, GetLastError());
                return;
            }

            nativeWindow->fullscreenSuspended = false;
            updateCursorConfinement();
        }
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

    void Window::handleDpiChange(unsigned int dpi, int suggestedLeft, int suggestedTop, int suggestedRight, int suggestedBottom)
    {
        if (nativeWindow == nullptr || nativeWindow->handle == nullptr)
        {
            return;
        }

        nativeWindow->dpi = dpi == 0 ? defaultDpi : dpi;

        if (!SetWindowPos(
                nativeWindow->handle,
                nullptr,
                suggestedLeft,
                suggestedTop,
                suggestedRight - suggestedLeft,
                suggestedBottom - suggestedTop,
                SWP_NOZORDER | SWP_NOACTIVATE))
        {
            recordResult(WindowResult::Win32CallFailed, GetLastError());
        }

        RECT clientRect{};
        if (GetClientRect(nativeWindow->handle, &clientRect))
        {
            handleResize(clientRect.right - clientRect.left, clientRect.bottom - clientRect.top);
        }

        invalidateMonitorCache();

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

        nativeWindow->windowedStyle = static_cast<DWORD>(GetWindowLongPtrA(nativeWindow->handle, GWL_STYLE));
        nativeWindow->windowedStyle &= ~(WS_MAXIMIZE | WS_MINIMIZE);
        nativeWindow->windowedExtendedStyle = static_cast<DWORD>(GetWindowLongPtrA(nativeWindow->handle, GWL_EXSTYLE));

        WINDOWPLACEMENT placement{};
        placement.length = sizeof(placement);
        if (GetWindowPlacement(nativeWindow->handle, &placement))
        {
            nativeWindow->windowedRect = placement.rcNormalPosition;
        }
        else if (!GetWindowRect(nativeWindow->handle, &nativeWindow->windowedRect))
        {
            return recordResult(WindowResult::Win32CallFailed, GetLastError());
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
            return recordResult(WindowResult::Success);
        }

        // If fullscreen was suspended on focus loss, the desktop display mode has already been restored.
        if (!nativeWindow->fullscreenSuspended)
        {
            LONG displayResult = ChangeDisplaySettingsExA(
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

        SetLastError(0);
        LONG_PTR previousStyle = SetWindowLongPtrA(nativeWindow->handle, GWL_STYLE, static_cast<LONG_PTR>(nativeWindow->windowedStyle));
        if (previousStyle == 0 && GetLastError() != 0)
        {
            return recordResult(WindowResult::Win32CallFailed, GetLastError());
        }

        SetLastError(0);
        LONG_PTR previousExtendedStyle = SetWindowLongPtrA(nativeWindow->handle, GWL_EXSTYLE, static_cast<LONG_PTR>(nativeWindow->windowedExtendedStyle));
        if (previousExtendedStyle == 0 && GetLastError() != 0)
        {
            return recordResult(WindowResult::Win32CallFailed, GetLastError());
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
            return recordResult(WindowResult::Win32CallFailed, GetLastError());
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
            return recordResult(WindowResult::Win32CallFailed, GetLastError());
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
            rollbackToFullscreen();
            return placementResult;
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

        if (!GetMonitorInfoA(monitor, &monitorInfo))
        {
            return recordResult(WindowResult::Win32CallFailed, GetLastError());
        }

        WindowResult displayResult = restoreDisplayMode();
        if (displayResult != WindowResult::Success)
        {
            return displayResult;
        }

        DWORD borderlessStyle = nativeWindow->windowedStyle;
        borderlessStyle &= ~WS_OVERLAPPEDWINDOW;
        borderlessStyle |= WS_POPUP;

        SetLastError(0);
        LONG_PTR previousStyle = SetWindowLongPtrA(nativeWindow->handle, GWL_STYLE, static_cast<LONG_PTR>(borderlessStyle));
        if (previousStyle == 0 && GetLastError() != 0)
        {
            unsigned long error = GetLastError();
            rollbackToFullscreen();
            return recordResult(WindowResult::Win32CallFailed, error);
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
            return recordResult(WindowResult::Win32CallFailed, error);
        }

        nativeWindow->mode = WindowMode::BorderlessFullscreen;
        return recordResult(WindowResult::Success);
    }

    WindowResult Window::applyFullscreenMode()
    {
        WindowMode previousMode = nativeWindow->mode;

        auto rollbackToPreviousMode = [&]()
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
                rollbackResult = restoreDisplayMode();
                break;
            default:
                break;
            }

            if (rollbackResult != WindowResult::Success)
            {
                recordResult(WindowResult::ModeChangeFailed);
            }
        };

        HMONITOR monitor = MonitorFromWindow(nativeWindow->handle, MONITOR_DEFAULTTONEAREST);

        MONITORINFOEXA monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);

        if (!GetMonitorInfoA(monitor, &monitorInfo))
        {
            return recordResult(WindowResult::Win32CallFailed, GetLastError());
        }

        std::copy(
            std::begin(monitorInfo.szDevice),
            std::end(monitorInfo.szDevice),
            std::begin(nativeWindow->fullscreenDeviceName));

        nativeWindow->savedDisplayMode = {};
        nativeWindow->savedDisplayMode.dmSize = sizeof(nativeWindow->savedDisplayMode);

        if (!EnumDisplaySettingsA(nativeWindow->fullscreenDeviceName, ENUM_CURRENT_SETTINGS, &nativeWindow->savedDisplayMode))
        {
            return recordResult(WindowResult::Win32CallFailed, GetLastError());
        }

        WindowResult saveResult = saveWindowedPlacement();
        if (saveResult != WindowResult::Success)
        {
            return saveResult;
        }

        nativeWindow->hasSavedDisplayMode = true;

        DEVMODEA fullscreenMode = nativeWindow->savedDisplayMode;
        fullscreenMode.dmPelsWidth = static_cast<DWORD>(nativeWindow->requestedClientWidth);
        fullscreenMode.dmPelsHeight = static_cast<DWORD>(nativeWindow->requestedClientHeight);
        fullscreenMode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
        nativeWindow->fullscreenWidth = static_cast<int>(fullscreenMode.dmPelsWidth);
        nativeWindow->fullscreenHeight = static_cast<int>(fullscreenMode.dmPelsHeight);

        LONG displayResult = ChangeDisplaySettingsExA(nativeWindow->fullscreenDeviceName, &fullscreenMode, nullptr, CDS_FULLSCREEN, nullptr);
        if (displayResult != DISP_CHANGE_SUCCESSFUL)
        {
            nativeWindow->hasSavedDisplayMode = false;
            return recordResult(WindowResult::ModeChangeFailed);
        }

        DWORD fullscreenStyle = nativeWindow->windowedStyle;
        fullscreenStyle &= ~WS_OVERLAPPEDWINDOW;
        fullscreenStyle |= WS_POPUP;

        SetLastError(0);
        LONG_PTR previousStyle = SetWindowLongPtrA(nativeWindow->handle, GWL_STYLE, static_cast<LONG_PTR>(fullscreenStyle));
        if (previousStyle == 0 && GetLastError() != 0)
        {
            unsigned long error = GetLastError();
            rollbackToPreviousMode();
            return recordResult(WindowResult::Win32CallFailed, error);
        }

        if (!SetWindowPos(
                nativeWindow->handle,
                HWND_TOP,
                0,
                0,
                static_cast<int>(fullscreenMode.dmPelsWidth),
                static_cast<int>(fullscreenMode.dmPelsHeight),
                SWP_FRAMECHANGED | SWP_SHOWWINDOW))
        {
            unsigned long error = GetLastError();
            rollbackToPreviousMode();
            return recordResult(WindowResult::Win32CallFailed, error);
        }

        nativeWindow->mode = WindowMode::Fullscreen;
        nativeWindow->fullscreenSuspended = false;
        return recordResult(WindowResult::Success);
    }

    // Cursor helpers

    WindowResult Window::releaseCursorConfinement()
    {
        if (nativeWindow == nullptr || !nativeWindow->cursorClipApplied)
        {
            return recordResult(WindowResult::Success);
        }

        if (!ClipCursor(nullptr))
        {
            return recordResult(WindowResult::Win32CallFailed, GetLastError());
        }

        nativeWindow->cursorClipApplied = false;
        return recordResult(WindowResult::Success);
    }

    void Window::updateCursorConfinement()
    {
        if (nativeWindow == nullptr)
        {
            return;
        }

        if (!nativeWindow->cursorConfined || nativeWindow->handle == nullptr || !nativeWindow->isFocused || nativeWindow->isMinimized)
        {
            releaseCursorConfinement();
            return;
        }

        RECT clientRect{};
        if (!GetClientRect(nativeWindow->handle, &clientRect))
        {
            recordResult(WindowResult::Win32CallFailed, GetLastError());
            releaseCursorConfinement();
            return;
        }

        POINT topLeft{clientRect.left, clientRect.top};
        POINT bottomRight{clientRect.right, clientRect.bottom};

        if (!ClientToScreen(nativeWindow->handle, &topLeft) || !ClientToScreen(nativeWindow->handle, &bottomRight))
        {
            recordResult(WindowResult::Win32CallFailed, GetLastError());
            releaseCursorConfinement();
            return;
        }

        RECT screenRect{topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};

        if (!ClipCursor(&screenRect))
        {
            recordResult(WindowResult::Win32CallFailed, GetLastError());
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
            return recordResult(WindowResult::Win32CallFailed, context.error);
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
            return recordResult(WindowResult::Win32CallFailed, GetLastError());
        }

        unsigned long error = 0;
        if (!buildMonitorInfo(hMonitor, outMonitor, error))
        {
            return recordResult(WindowResult::Win32CallFailed, error);
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

        for (const auto &entry : nativeWindow->displayModeCache)
        {
            if (entry.first == monitor.deviceName)
            {
                outModes = entry.second;
                return recordResult(WindowResult::Success);
            }
        }

        std::vector<DisplayMode> modes;

        std::wstring deviceName;
        if (!utf8ToWide(monitor.deviceName, deviceName))
        {
            return recordResult(WindowResult::InvalidMonitor, GetLastError());
        }

        DWORD modeNum = 0;
        for (;;)
        {
            DEVMODEW devMode{};
            devMode.dmSize = sizeof(devMode);
            if (EnumDisplaySettingsW(deviceName.c_str(), modeNum, &devMode) == FALSE)
            {
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

        nativeWindow->displayModeCache.emplace_back(monitor.deviceName, modes);
        outModes = nativeWindow->displayModeCache.back().second;
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

        std::wstring deviceName;
        if (!utf8ToWide(monitor.deviceName, deviceName))
        {
            return recordResult(WindowResult::InvalidMonitor, GetLastError());
        }

        DEVMODEW devMode = {};
        devMode.dmSize = sizeof(DEVMODEW);
        if (EnumDisplaySettingsW(deviceName.c_str(), ENUM_CURRENT_SETTINGS, &devMode) == FALSE)
        {
            return recordResult(WindowResult::Win32CallFailed, GetLastError());
        }

        outMode = DisplayMode{
            static_cast<int>(devMode.dmPelsWidth),
            static_cast<int>(devMode.dmPelsHeight),
            static_cast<int>(devMode.dmDisplayFrequency),
            static_cast<int>(devMode.dmBitsPerPel)};
        return recordResult(WindowResult::Success);
    }
}
