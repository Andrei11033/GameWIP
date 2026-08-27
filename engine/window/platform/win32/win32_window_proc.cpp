/// @file win32_window_proc.cpp
/// @brief Win32 native message callback and event translation.

#include "window/platform/win32/internal/win32_window_backend.h"
#include "window/platform/win32/internal/win32_compat.h"

#include "window/native/win32.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <new>
#include <utility>

namespace GameWIP::Window::Detail::Platform
{
    // ------------------------------------------------------------
    // Native message translation
    // ------------------------------------------------------------
    namespace
    {
        void emitGeometryChanges(
            WindowState &state,
            Types::ScreenPosition previousPosition,
            Types::LogicalSize previousClient,
            Types::PixelSize previousFramebuffer) noexcept
        {
            if (state.clientPosition != previousPosition)
            {
                routeEvent(state, Types::Events::ClientPositionChanged{state.clientPosition});
            }
            if (state.clientSize != previousClient)
            {
                routeEvent(state, Types::Events::ClientSizeChanged{state.clientSize});
            }
            if (state.framebufferSize != previousFramebuffer)
            {
                routeEvent(state, Types::Events::FramebufferSizeChanged{state.framebufferSize});
            }
        }

        [[nodiscard]] LRESULT resizeHitTest(WindowState &state, POINT screenPoint) noexcept
        {
            if (!state.resizable || state.presentation != Types::PresentationState::Normal || state.mode != Types::Mode::Windowed)
            {
                return HTNOWHERE;
            }
            RECT frame{};
            if (GetWindowRect(state.platform->handle, &frame) == FALSE)
            {
                return HTNOWHERE;
            }
            const LONG border = std::max<LONG>(4, MulDiv(8, static_cast<int>(dpiForWindow(state.platform->handle)), kBaselineDpi));
            const bool left = screenPoint.x < frame.left + border;
            const bool right = screenPoint.x >= frame.right - border;
            const bool top = screenPoint.y < frame.top + border;
            const bool bottom = screenPoint.y >= frame.bottom - border;
            if (top && left)
                return HTTOPLEFT;
            if (top && right)
                return HTTOPRIGHT;
            if (bottom && left)
                return HTBOTTOMLEFT;
            if (bottom && right)
                return HTBOTTOMRIGHT;
            if (left)
                return HTLEFT;
            if (right)
                return HTRIGHT;
            if (top)
                return HTTOP;
            if (bottom)
                return HTBOTTOM;
            return HTNOWHERE;
        }

        [[nodiscard]] Types::LogicalPosition logicalClientPoint(WindowState &state, POINT screenPoint) noexcept
        {
            ScreenToClient(state.platform->handle, &screenPoint);
            const UINT dpi = dpiForWindow(state.platform->handle);
            return {physicalToLogical(screenPoint.x, dpi), physicalToLogical(screenPoint.y, dpi)};
        }

        [[nodiscard]] LRESULT customHitTest(WindowState &state, POINT screenPoint) noexcept
        {
            const LRESULT resize = resizeHitTest(state, screenPoint);
            if (resize != HTNOWHERE)
                return resize;

            const Types::LogicalPosition point = logicalClientPoint(state, screenPoint);
            if (state.closeButtonRegion && pointInRect(point, *state.closeButtonRegion))
                return HTCLOSE;
            if (state.maximizeButtonRegion && pointInRect(point, *state.maximizeButtonRegion))
                return HTMAXBUTTON;
            if (state.minimizeButtonRegion && pointInRect(point, *state.minimizeButtonRegion))
                return HTMINBUTTON;
            if (state.systemMenuRegion && pointInRect(point, *state.systemMenuRegion))
                return HTSYSMENU;
            for (const Types::LogicalRect &rect : state.draggableRegions)
            {
                if (pointInRect(point, rect))
                    return HTCAPTION;
            }

            return HTCLIENT;
        }
    } // namespace
    // ------------------------------------------------------------
    // Window procedure
    // ------------------------------------------------------------
    LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        WindowState *state = reinterpret_cast<WindowState *>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            const auto *create = reinterpret_cast<const CREATESTRUCTW *>(lParam);
            state = static_cast<WindowState *>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        }
        if (state == nullptr)
            return DefWindowProcW(window, message, wParam, lParam);

        switch (message)
        {
        case WM_NCCALCSIZE:
            if (state->decoration != Types::DecorationMode::System && wParam != FALSE)
                return 0;
            break;
        case WM_NCHITTEST:
        {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (state->decoration == Types::DecorationMode::Custom)
            {
                LRESULT dwmResult = 0;
                if (DwmDefWindowProc(window, message, wParam, lParam, &dwmResult) != FALSE && dwmResult != HTCLIENT)
                    return dwmResult;
            }
            if (state->decoration != Types::DecorationMode::System)
                return customHitTest(*state, point);
            break;
        }
        case WM_MOUSEACTIVATE:
            if (!state->focusable)
                return MA_NOACTIVATE;
            break;
        case WM_CLOSE:
            if (state->controls.closable)
                static_cast<void>(Detail::requestClose(*state, Types::Events::CloseRequestSource::User));
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xFFF0) == SC_CLOSE && !state->controls.closable)
                return 0;
            if ((wParam & 0xFFF0) == SC_MINIMIZE && !state->controls.minimizable)
                return 0;
            if ((wParam & 0xFFF0) == SC_MAXIMIZE && !state->controls.maximizable)
                return 0;
            break;
        case WM_SHOWWINDOW:
        {
            const bool visible = wParam != FALSE;
            if (state->visible != visible)
            {
                state->visible = visible;
                routeEvent(*state, Types::Events::VisibilityChanged{visible});
                const IO::Types::Status cursorStatus = applyCursorState(*state);
                if (!cursorStatus.ok())
                    recordPumpFailure(cursorStatus);
            }
            break;
        }
        case WM_ACTIVATEAPP:
        {
            // Focus can move among this process's windows without surrendering the exclusive
            // display mode. Only application activation crosses that ownership boundary.
            const IO::Types::Status fullscreenStatus = wParam != FALSE ? resumeExclusive(*state) : suspendExclusive(*state);
            if (!fullscreenStatus.ok())
                recordPumpFailure(fullscreenStatus);
            return 0;
        }
        case WM_SETFOCUS:
            if (!state->focused)
            {
                state->focused = true;
                routeEvent(*state, Types::Events::FocusChanged{true});
                const IO::Types::Status cursorStatus = applyCursorState(*state);
                if (!cursorStatus.ok())
                    recordPumpFailure(cursorStatus);
            }
            return 0;
        case WM_KILLFOCUS:
            if (state->focused)
            {
                state->focused = false;
                routeEvent(*state, Types::Events::FocusChanged{false});
                const IO::Types::Status cursorStatus = applyCursorState(*state);
                if (!cursorStatus.ok())
                    recordPumpFailure(cursorStatus);
            }
            return 0;
        case WM_SIZE:
        {
            const Types::PresentationState previousPresentation = state->presentation;
            state->presentation = wParam == SIZE_MINIMIZED   ? Types::PresentationState::Minimized
                                  : wParam == SIZE_MAXIMIZED ? Types::PresentationState::Maximized
                                                             : Types::PresentationState::Normal;
            const Types::ScreenPosition previousPosition = state->clientPosition;
            const Types::LogicalSize previousClient = state->clientSize;
            const Types::PixelSize previousFramebuffer = state->framebufferSize;
            const IO::Types::Status geometry = refreshCachedGeometry(*state);
            if (!geometry.ok())
                recordPumpFailure(geometry);
            else
                emitGeometryChanges(*state, previousPosition, previousClient, previousFramebuffer);
            if (state->presentation != previousPresentation)
                routeEvent(*state, Types::Events::PresentationStateChanged{state->presentation});
            const IO::Types::Status cursorStatus = applyCursorState(*state);
            if (!cursorStatus.ok())
                recordPumpFailure(cursorStatus);
            return 0;
        }
        case WM_MOVE:
        {
            const Types::ScreenPosition previousPosition = state->clientPosition;
            const Types::LogicalSize previousClient = state->clientSize;
            const Types::PixelSize previousFramebuffer = state->framebufferSize;
            const IO::Types::Status geometry = refreshCachedGeometry(*state);
            if (!geometry.ok())
                recordPumpFailure(geometry);
            else
                emitGeometryChanges(*state, previousPosition, previousClient, previousFramebuffer);
            updateCurrentMonitor(*state);
            const IO::Types::Status cursorStatus = applyCursorState(*state);
            if (!cursorStatus.ok())
                recordPumpFailure(cursorStatus);
            return 0;
        }
        case WM_DPICHANGED:
        {
            const Types::ContentScale previousScale = state->contentScale;
            const Types::Dpi previousDpi = state->dpi;
            const Types::ScreenPosition previousPosition = state->clientPosition;
            const Types::LogicalSize previousClient = state->clientSize;
            const Types::PixelSize previousFramebuffer = state->framebufferSize;
            const RECT *suggested = reinterpret_cast<const RECT *>(lParam);
            const UINT newDpi = LOWORD(wParam);
            const bool transitionActive = state->platform->modeTransitionDepth != 0;
            // A synchronous WM_DPICHANGED can arrive inside SetWindowPos. While a mode
            // transition is active, its outer placement remains the sole geometry owner.
            if (!transitionActive && state->mode == Types::Mode::Windowed)
            {
                Types::PixelSize desiredClient = previousFramebuffer;
                if (state->dpiResizePolicy == Types::DpiResizePolicy::PreserveLogicalClientSize)
                    desiredClient = logicalToPhysicalSize(previousClient, newDpi);
                if (desiredClient.width == 0 || desiredClient.height == 0 ||
                    desiredClient.width > static_cast<std::uint32_t>(std::numeric_limits<LONG>::max()) ||
                    desiredClient.height > static_cast<std::uint32_t>(std::numeric_limits<LONG>::max()))
                {
                    recordPumpFailure(
                        IO::makeStatus(
                            IO::Types::ErrorCode::InvalidArgument,
                            ERROR_ARITHMETIC_OVERFLOW,
                            "WM_DPICHANGED client size exceeds Win32 range"));
                    return 0;
                }
                RECT desiredOuter{0, 0, static_cast<LONG>(desiredClient.width), static_cast<LONG>(desiredClient.height)};
                const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE));
                const DWORD extendedStyle = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE));
                const BOOL adjusted = AdjustWindowRectExForDpi(&desiredOuter, style, FALSE, extendedStyle, newDpi);
                if (suggested == nullptr || adjusted == FALSE)
                {
                    recordPumpFailure(statusFromWin32(
                        IO::Types::ErrorCode::NativeFailure,
                        adjusted == FALSE ? GetLastError() : ERROR_INVALID_PARAMETER,
                        "calculate WM_DPICHANGED bounds"));
                    return 0;
                }
                if (SetWindowPos(
                        window,
                        nullptr,
                        suggested->left,
                        suggested->top,
                        desiredOuter.right - desiredOuter.left,
                        desiredOuter.bottom - desiredOuter.top,
                        SWP_NOZORDER | SWP_NOACTIVATE) == FALSE)
                {
                    recordPumpFailure(statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "apply WM_DPICHANGED bounds"));
                }
            }
            else if (!transitionActive && !state->fullscreen.suspended)
            {
                HMONITOR monitor = nativeMonitor(state->fullscreen.monitor);
                if (monitor == nullptr)
                    monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
                if (IO::Types::Status placement = placeFullscreenOnMonitor(*state, monitor, true); !placement.ok())
                    recordPumpFailure(std::move(placement));
            }
            const IO::Types::Status geometry = refreshCachedGeometry(*state);
            if (!geometry.ok())
                recordPumpFailure(geometry);
            else
            {
                emitGeometryChanges(*state, previousPosition, previousClient, previousFramebuffer);
                if (state->contentScale != previousScale || state->dpi != previousDpi)
                {
                    routeEvent(
                        *state,
                        Types::Events::ContentScaleChanged{previousScale, state->contentScale, previousDpi, state->dpi, state->framebufferSize});
                }
            }
            refreshCustomCursorForDpi(*state, newDpi);
            updateCurrentMonitor(*state);
            return 0;
        }
        case WM_GETMINMAXINFO:
        {
            auto *info = reinterpret_cast<MINMAXINFO *>(lParam);
            if (info == nullptr || state->mode != Types::Mode::Windowed)
                return 0;
            const UINT dpi = dpiForWindow(window);
            RECT frame{0, 0, 0, 0};
            if (AdjustWindowRectExForDpi(&frame, styleFor(*state), FALSE, extendedStyleFor(*state), dpi) == FALSE)
            {
                recordPumpFailure(statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "AdjustWindowRectExForDpi limits"));
                return 0;
            }
            const LONG frameWidth = frame.right - frame.left;
            const LONG frameHeight = frame.bottom - frame.top;
            if (state->sizeLimits.minimum)
            {
                const Types::PixelSize physical = logicalToPhysicalSize(*state->sizeLimits.minimum, dpi);
                info->ptMinTrackSize = {static_cast<LONG>(physical.width) + frameWidth, static_cast<LONG>(physical.height) + frameHeight};
            }
            if (state->sizeLimits.maximum)
            {
                const Types::PixelSize physical = logicalToPhysicalSize(*state->sizeLimits.maximum, dpi);
                info->ptMaxTrackSize = {static_cast<LONG>(physical.width) + frameWidth, static_cast<LONG>(physical.height) + frameHeight};
            }
            return 0;
        }
        case WM_SIZING:
        {
            if (!state->aspectRatio || state->mode != Types::Mode::Windowed)
                break;
            auto *rect = reinterpret_cast<RECT *>(lParam);
            if (rect == nullptr)
                break;
            const UINT dpi = dpiForWindow(window);
            RECT adjustment{0, 0, 0, 0};
            if (AdjustWindowRectExForDpi(&adjustment, styleFor(*state), FALSE, extendedStyleFor(*state), dpi) == FALSE)
                break;
            const LONG frameWidth = adjustment.right - adjustment.left;
            const LONG frameHeight = adjustment.bottom - adjustment.top;
            LONG clientWidth = std::max<LONG>(1, rect->right - rect->left - frameWidth);
            LONG clientHeight = std::max<LONG>(1, rect->bottom - rect->top - frameHeight);
            const double ratio = static_cast<double>(state->aspectRatio->numerator) / state->aspectRatio->denominator;
            if (wParam == WMSZ_TOP || wParam == WMSZ_BOTTOM)
                clientWidth = static_cast<LONG>(std::lround(clientHeight * ratio));
            else
                clientHeight = static_cast<LONG>(std::lround(clientWidth / ratio));
            const LONG outerWidth = clientWidth + frameWidth;
            const LONG outerHeight = clientHeight + frameHeight;
            if (wParam == WMSZ_LEFT || wParam == WMSZ_TOPLEFT || wParam == WMSZ_BOTTOMLEFT)
                rect->left = rect->right - outerWidth;
            else
                rect->right = rect->left + outerWidth;
            if (wParam == WMSZ_TOP || wParam == WMSZ_TOPLEFT || wParam == WMSZ_TOPRIGHT)
                rect->top = rect->bottom - outerHeight;
            else
                rect->bottom = rect->top + outerHeight;
            return TRUE;
        }
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT)
            {
                const bool hidden = state->cursorMode == Types::CursorMode::Hidden || state->cursorMode == Types::CursorMode::HiddenConfined ||
                                    state->cursorMode == Types::CursorMode::Relative;
                SetCursor(hidden ? nullptr : state->platform->cursor);
                return TRUE;
            }
            break;
        case WM_MOUSEMOVE:
            if (!state->cursorInside)
            {
                TRACKMOUSEEVENT tracking{};
                tracking.cbSize = sizeof(tracking);
                tracking.dwFlags = TME_LEAVE;
                tracking.hwndTrack = window;
                if (TrackMouseEvent(&tracking) == FALSE)
                    recordPumpFailure(statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "TrackMouseEvent"));
                else
                {
                    state->cursorInside = true;
                    routeEvent(*state, Types::Events::CursorPresenceChanged{true});
                }
            }
            return DefWindowProcW(window, message, wParam, lParam);
        case WM_MOUSELEAVE:
            if (state->cursorInside)
            {
                state->cursorInside = false;
                routeEvent(*state, Types::Events::CursorPresenceChanged{false});
            }
            return 0;
        case WM_DROPFILES:
        {
            HDROP drop = reinterpret_cast<HDROP>(wParam);
            if (drop == nullptr)
                return 0;
            try
            {
                Types::Events::FilesDropped event;
                POINT position{};
                if (DragQueryPoint(drop, &position) != FALSE)
                {
                    const UINT dpi = dpiForWindow(window);
                    event.clientPosition = Types::LogicalPosition{physicalToLogical(position.x, dpi), physicalToLogical(position.y, dpi)};
                }
                const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
                event.paths.reserve(count);
                for (UINT index = 0; index < count; ++index)
                {
                    const UINT length = DragQueryFileW(drop, index, nullptr, 0);
                    std::wstring path(static_cast<std::size_t>(length) + 1, L'\0');
                    const UINT copied = DragQueryFileW(drop, index, path.data(), length + 1);
                    if (copied != length)
                    {
                        recordPumpFailure(statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "DragQueryFileW"));
                        DragFinish(drop);
                        return 0;
                    }
                    path.resize(length);
                    event.paths.emplace_back(std::move(path));
                }
                routeEvent(*state, std::move(event));
            }
            catch (const std::bad_alloc &)
            {
                recordPumpFailure(IO::makeStatus(IO::Types::ErrorCode::OutOfMemory));
            }
            catch (...)
            {
                recordPumpFailure(IO::makeStatus(IO::Types::ErrorCode::Unknown));
            }
            DragFinish(drop);
            return 0;
        }
        case WM_DISPLAYCHANGE:
            if (IO::Types::Status recovery = recoverAfterDisplayChange(*state); !recovery.ok())
                recordPumpFailure(std::move(recovery));
            return 0;
        case WM_PAINT:
        {
            PAINTSTRUCT paint{};
            BeginPaint(window, &paint);
            EndPaint(window, &paint);
            routeEvent(*state, Types::Events::RedrawRequested{});
            return 0;
        }
        case WM_ERASEBKGND:
            if (state->transparentFramebuffer)
                return TRUE;
            break;
        case WM_DESTROY:
            state->visible = false;
            state->focused = false;
            state->cursorInside = false;
            return 0;
        case WM_NCDESTROY:
            releaseCustomCursorBinding(window);
            if (state->platform)
            {
                const bool unexpected = !state->platform->destroying;
                if (unexpected)
                {
                    invalidatePointerHitMask(*state);
                    IO::Types::Status restoreStatus = leaveExclusive(*state);
                    if (!restoreStatus.ok())
                        recordPumpFailure(std::move(restoreStatus));
                    state->visible = false;
                    state->focused = false;
                    state->cursorInside = false;
                    state->nativeDestroyedPendingFinalize = true;
                    state->platform->handle = nullptr;
                    routeEvent(*state, Types::Events::NativeDestroyed{});
                }
                else
                {
                    state->platform->handle = nullptr;
                }
            }
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            break;
        default:
            break;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

} // namespace GameWIP::Window::Detail::Platform
