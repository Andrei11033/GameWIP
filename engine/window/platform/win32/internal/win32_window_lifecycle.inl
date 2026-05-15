#pragma once

namespace GameWIP
{
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

void Window::pollEvents(Input::InputState &inputState, Input::InputDeviceRegistry &inputDevices)
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

        Win32Input::handleMessage(message.message, message.wParam, message.lParam, inputState, inputDevices);
        TranslateMessage(&message);
        DispatchMessageW(&message);

        if (clearInputAfterDispatch)
        {
            inputState.clear();
        }
    }

    if (nativeWindow != nullptr && nativeWindow->isFocused)
    {
        Win32Input::updateGamepads(inputState, inputDevices);
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
}
