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
                SetCursor(window->isCursorVisible() ? static_cast<HCURSOR>(Win32Internal::MessageAccess::getCursorHandle(*window)) : nullptr);
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
