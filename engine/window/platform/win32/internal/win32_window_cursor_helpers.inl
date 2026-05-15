#pragma once

namespace GameWIP
{
// Cursor helpers

void *Window::getNativeCursorHandle() const
{
    if (nativeWindow == nullptr)
    {
        return nullptr;
    }

    return static_cast<void *>(nativeWindow->arrowCursor);
}

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
}
