#pragma once

#include "window/internal/window_message_access.h"

namespace GameWIP
{
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

        void MessageAccess::handleDpiChange(
            Window &window,
            unsigned int dpi,
            int suggestedLeft,
            int suggestedTop,
            int suggestedRight,
            int suggestedBottom)
        {
            window.handleDpiChange(dpi, suggestedLeft, suggestedTop, suggestedRight, suggestedBottom);
        }

        void MessageAccess::handleDisplayChange(Window &window)
        {
            window.handleDisplayChange();
        }

        void MessageAccess::handleGetMinMaxInfo(Window &window, void *minMaxInfo)
        {
            window.handleGetMinMaxInfo(minMaxInfo);
        }

        void *MessageAccess::getCursorHandle(const Window &window)
        {
            return window.getNativeCursorHandle();
        }
    } // namespace WindowInternal
} // namespace GameWIP
