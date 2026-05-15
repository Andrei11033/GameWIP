#pragma once

#include "window/window.h"

#include <string_view>

namespace GameWIP::WindowInternal
{
    struct MessageAccess // Forwards Win32 messages from the callback into private Window handlers.
    {
        /// @brief Forwards a native resize message.
        static void handleResize(Window &window, int width, int height);

        /// @brief Forwards a native move message.
        static void handleMove(Window &window, int x, int y);

        /// @brief Forwards a native focus change message.
        static void handleFocusChange(Window &window, bool focused);

        /// @brief Forwards a native app activation change message.
        static void handleActivationChange(Window &window, bool active);

        /// @brief Forwards a native minimize change message.
        static void handleMinimizeChange(Window &window, bool minimized);

        /// @brief Forwards a native maximize change message.
        static void handleMaximizeChange(Window &window, bool maximized);

        /// @brief Forwards a native visibility change message.
        static void handleVisibilityChange(Window &window, bool visible);

        /// @brief Returns whether native cursor enter handling is needed.
        static bool shouldHandleCursorEnter(const Window &window);

        /// @brief Forwards a native cursor enter message.
        static bool handleCursorEnter(Window &window);

        /// @brief Forwards a cursor tracking failure.
        static void handleCursorTrackingFailure(Window &window, unsigned long win32Error);

        /// @brief Forwards a native cursor leave message.
        static void handleCursorLeave(Window &window);

        /// @brief Forwards a native file drop message.
        static void handleFileDrop(Window &window, std::string_view filePath);

        /// @brief Forwards native window destruction.
        static void handleDestroyed(Window &window);

        /// @brief Checks and forwards a monitor change if needed.
        static void updateCurrentMonitor(Window &window);

        /// @brief Forwards a native DPI change message.
        static void handleDpiChange(Window &window, unsigned int dpi, int suggestedLeft, int suggestedTop, int suggestedRight, int suggestedBottom);

        /// @brief Forwards a native display configuration change message.
        static void handleDisplayChange(Window &window);

        /// @brief Forwards a native minimum/maximum size message.
        static void handleGetMinMaxInfo(Window &window, void *minMaxInfo);

        /// @brief Returns the native cursor handle used by the window procedure.
        static void *getCursorHandle(const Window &window);
    };
}
