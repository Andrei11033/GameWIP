#pragma once

namespace GameWIP
{
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
}
