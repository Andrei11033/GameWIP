#pragma once

namespace GameWIP
{
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
        pushEvent(WindowEvent{.type = WindowEventType::Resized, .width = width, .height = height});
        updateCursorConfinement();
    }

    void Window::handleMove(int x, int y)
    {
        pushEvent(WindowEvent{.type = WindowEventType::Moved, .x = x, .y = y});
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
        pushEvent(WindowEvent{.type = WindowEventType::MonitorChanged, .width = nativeWindow->clientWidth, .height = nativeWindow->clientHeight});
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
            pushEvent(WindowEvent{.type = focused ? WindowEventType::Focused : WindowEventType::LostFocus});
        }

        updateCursorConfinement();

        if (!focused && nativeWindow->mode == WindowMode::Fullscreen && nativeWindow->hasSavedDisplayMode && !nativeWindow->fullscreenSuspended)
        {
            LONG displayResult = ChangeDisplaySettingsExW(nativeWindow->fullscreenDeviceName, &nativeWindow->savedDisplayMode, nullptr, 0, nullptr);

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

            LONG displayResult = ChangeDisplaySettingsExW(nativeWindow->fullscreenDeviceName, &fullscreenMode, nullptr, CDS_FULLSCREEN, nullptr);

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
        pushEvent(WindowEvent{.type = suspended ? WindowEventType::Suspended : WindowEventType::Resumed});
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
            pushEvent(WindowEvent{.type = minimized ? WindowEventType::Minimized : WindowEventType::Restored});
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
            pushEvent(WindowEvent{.type = maximized ? WindowEventType::Maximized : WindowEventType::Restored});
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
            pushEvent(WindowEvent{.type = visible ? WindowEventType::Visible : WindowEventType::Occluded});
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
            if (!nativeWindow->fullscreenSuspended && nativeWindow->fullscreenWidth > 0 && nativeWindow->fullscreenHeight > 0)
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

        pushEvent(
            WindowEvent{
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
        pushEvent(WindowEvent{.type = WindowEventType::DisplayChanged});
    }
} // namespace GameWIP
