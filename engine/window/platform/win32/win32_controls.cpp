/// @file win32_controls.cpp
/// @brief Win32 presentation, chrome, interaction, and cursor operations for Window.

#include "window/platform/win32/internal/win32_window_backend.h"

#include <algorithm>
#include <cmath>

namespace GameWIP::Window::Detail::Platform
{
    namespace
    {
        void synchronizeVisibility(WindowState &state) noexcept
        {
            const bool visible = IsWindowVisible(state.platform->handle) != FALSE;
            if (visible != state.visible)
            {
                state.visible = visible;
                routeEvent(state, Types::VisibilityChangedEvent{visible});
            }
        }

        void synchronizePresentation(WindowState &state) noexcept
        {
            const Types::PresentationState value = IsIconic(state.platform->handle)   ? Types::PresentationState::Minimized
                                                   : IsZoomed(state.platform->handle) ? Types::PresentationState::Maximized
                                                                                      : Types::PresentationState::Normal;
            if (value != state.presentation)
            {
                state.presentation = value;
                routeEvent(state, Types::PresentationStateChangedEvent{value});
            }
        }

        template <typename Value, typename Apply>
        [[nodiscard]] IO::Types::Status updateStyleValue(WindowState &state, Value &destination, const Value &value, Apply &&apply) noexcept
        {
            if (destination == value)
                return IO::successStatus();
            const Value previous = destination;
            destination = value;
            IO::Types::Status status = applyStyle(state);
            if (status.ok())
                status = apply();
            if (!status.ok())
            {
                destination = previous;
                static_cast<void>(applyStyle(state));
            }
            return status;
        }
    } // namespace

    IO::Types::Status show(WindowState &state) noexcept
    {
        ShowWindow(state.platform->handle, SW_SHOWNOACTIVATE);
        synchronizeVisibility(state);
        synchronizePresentation(state);
        return applyCursorState(state);
    }

    IO::Types::Status hide(WindowState &state) noexcept
    {
        ShowWindow(state.platform->handle, SW_HIDE);
        synchronizeVisibility(state);
        return applyCursorState(state);
    }

    IO::Types::Status requestFocus(WindowState &state) noexcept
    {
        if (!state.focusable)
            return IO::makeStatus(IO::Types::ErrorCode::Unsupported);
        if (IsWindowVisible(state.platform->handle) == FALSE)
            return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
        if (IsIconic(state.platform->handle) != FALSE)
            ShowWindow(state.platform->handle, SW_RESTORE);
        const HWND previous = SetFocus(state.platform->handle);
        if (GetFocus() != state.platform->handle && SetForegroundWindow(state.platform->handle) == FALSE)
        {
            if (previous != nullptr)
                SetFocus(previous);
            return statusFromWin32(IO::Types::ErrorCode::PermissionDenied, GetLastError(), "request window focus");
        }
        return IO::successStatus();
    }

    IO::Types::Status requestAttention(WindowState &state) noexcept
    {
        FLASHWINFO info{};
        info.cbSize = sizeof(info);
        info.hwnd = state.platform->handle;
        info.dwFlags = FLASHW_TRAY | FLASHW_TIMERNOFG;
        info.uCount = 3;
        info.dwTimeout = 0;
        // FlashWindowEx returns the Window's prior active state, not operation success.
        static_cast<void>(FlashWindowEx(&info));
        return IO::successStatus();
    }

    IO::Types::Status minimize(WindowState &state) noexcept
    {
        if (!state.controls.minimizable)
            return IO::makeStatus(IO::Types::ErrorCode::Unsupported);
        ShowWindow(state.platform->handle, SW_MINIMIZE);
        synchronizePresentation(state);
        return applyCursorState(state);
    }

    IO::Types::Status maximize(WindowState &state) noexcept
    {
        if (!state.controls.maximizable || !state.resizable)
            return IO::makeStatus(IO::Types::ErrorCode::Unsupported);
        ShowWindow(state.platform->handle, SW_MAXIMIZE);
        synchronizePresentation(state);
        return refreshCachedGeometry(state);
    }

    IO::Types::Status restore(WindowState &state) noexcept
    {
        ShowWindow(state.platform->handle, SW_RESTORE);
        synchronizePresentation(state);
        return refreshCachedGeometry(state);
    }

    IO::Types::Status setResizable(WindowState &state, bool resizable) noexcept
    {
        return updateStyleValue(
            state,
            state.resizable,
            resizable,
            []
            {
                return IO::successStatus();
            });
    }

    IO::Types::Status setDecorationMode(WindowState &state, Types::DecorationMode mode) noexcept
    {
        return updateStyleValue(
            state,
            state.decoration,
            mode,
            []
            {
                return IO::successStatus();
            });
    }

    IO::Types::Status setWindowControls(WindowState &state, const Types::WindowControls &controls) noexcept
    {
        return updateStyleValue(
            state,
            state.controls,
            controls,
            []
            {
                return IO::successStatus();
            });
    }

    IO::Types::Status setFocusable(WindowState &state, bool focusable) noexcept
    {
        return updateStyleValue(
            state,
            state.focusable,
            focusable,
            [&state, focusable]
            {
                if (!focusable && GetFocus() == state.platform->handle)
                    SetFocus(nullptr);
                return IO::successStatus();
            });
    }

    IO::Types::Status setUserInteractionEnabled(WindowState &state, bool enabled) noexcept
    {
        if (state.interactionEnabled == enabled)
            return IO::successStatus();
        EnableWindow(state.platform->handle, enabled ? TRUE : FALSE);
        if ((IsWindowEnabled(state.platform->handle) != FALSE) != enabled)
            return statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "EnableWindow");
        state.interactionEnabled = enabled;
        return IO::successStatus();
    }

    IO::Types::Status setAlwaysOnTop(WindowState &state, bool alwaysOnTop) noexcept
    {
        return updateStyleValue(
            state,
            state.alwaysOnTop,
            alwaysOnTop,
            []
            {
                return IO::successStatus();
            });
    }

    IO::Types::Status setOpacity(WindowState &state, float opacity) noexcept
    {
        const float previous = state.opacity;
        state.opacity = opacity;
        IO::Types::Status status = applyStyle(state);
        if (status.ok() && (opacity < 1.0F || state.pointerInputMode == Types::PointerInputMode::ClickThrough))
        {
            const BYTE alpha = static_cast<BYTE>(std::lround(std::clamp(opacity, 0.0F, 1.0F) * 255.0F));
            if (SetLayeredWindowAttributes(state.platform->handle, 0, alpha, LWA_ALPHA) == FALSE)
                status = statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "SetLayeredWindowAttributes");
        }
        if (!status.ok())
        {
            state.opacity = previous;
            static_cast<void>(applyStyle(state));
        }
        return status;
    }

    IO::Types::Status setBackdropEffect(WindowState &state, Types::BackdropEffect effect) noexcept
    {
        if (effect == Types::BackdropEffect::None && !supportsSystemBackdrop())
        {
            state.backdrop = effect;
            return IO::successStatus();
        }
        if (!supportsSystemBackdrop())
            return IO::makeStatus(IO::Types::ErrorCode::Unsupported);

        DWM_SYSTEMBACKDROP_TYPE nativeEffect = DWMSBT_NONE;
        switch (effect)
        {
        case Types::BackdropEffect::None: nativeEffect = DWMSBT_NONE; break;
        case Types::BackdropEffect::Automatic: nativeEffect = DWMSBT_AUTO; break;
        case Types::BackdropEffect::MainWindow: nativeEffect = DWMSBT_MAINWINDOW; break;
        case Types::BackdropEffect::TransientWindow: nativeEffect = DWMSBT_TRANSIENTWINDOW; break;
        case Types::BackdropEffect::TabbedWindow: nativeEffect = DWMSBT_TABBEDWINDOW; break;
        }
        const HRESULT result =
            DwmSetWindowAttribute(state.platform->handle, DWMWA_SYSTEMBACKDROP_TYPE, &nativeEffect, sizeof(nativeEffect));
        if (FAILED(result))
            return IO::makeStatus(
                effect != Types::BackdropEffect::None ? IO::Types::ErrorCode::Unsupported : IO::Types::ErrorCode::NativeFailure,
                result);
        state.backdrop = effect;
        return IO::successStatus();
    }

    IO::Types::Status setFileDropEnabled(WindowState &state, bool enabled) noexcept
    {
        DragAcceptFiles(state.platform->handle, enabled ? TRUE : FALSE);
        state.fileDropEnabled = enabled;
        return IO::successStatus();
    }

    IO::Types::Status setCustomChromeLayout(WindowState &state) noexcept
    {
        if (SetWindowPos(state.platform->handle, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED) ==
            FALSE)
        {
            return statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "refresh custom chrome");
        }
        RedrawWindow(state.platform->handle, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE);
        return IO::successStatus();
    }

    IO::Types::Status setPointerInputLayout(WindowState &state) noexcept
    {
        IO::Types::Status status = applyStyle(state);
        if (!status.ok())
            return status;
        if (state.pointerInputMode == Types::PointerInputMode::ClickThrough || state.opacity < 1.0F)
        {
            const BYTE alpha = static_cast<BYTE>(std::lround(std::clamp(state.opacity, 0.0F, 1.0F) * 255.0F));
            if (SetLayeredWindowAttributes(state.platform->handle, 0, alpha, LWA_ALPHA) == FALSE)
                return statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "SetLayeredWindowAttributes pointer policy");
        }
        return IO::successStatus();
    }

    IO::Types::Status setCursorMode(WindowState &state, Types::CursorMode mode) noexcept
    {
        if (state.cursorMode == mode)
            return IO::successStatus();
        const Types::CursorMode previous = state.cursorMode;
        state.cursorMode = mode;
        IO::Types::Status status = applyCursorState(state);
        if (!status.ok())
        {
            state.cursorMode = previous;
            static_cast<void>(applyCursorState(state));
            return status;
        }
        if (state.cursorInside)
            SetCursor(
                mode == Types::CursorMode::Hidden || mode == Types::CursorMode::HiddenConfined || mode == Types::CursorMode::Relative
                    ? nullptr
                    : state.platform->cursor);
        return IO::successStatus();
    }

    IO::Types::Status setCursorShape(WindowState &state, Types::CursorShape shape) noexcept
    {
        HCURSOR cursor = loadCursor(shape);
        if (cursor == nullptr)
            return statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "LoadCursorW");
        state.platform->cursor = cursor;
        state.cursorShape = shape;
        if (state.cursorInside && state.cursorMode != Types::CursorMode::Hidden && state.cursorMode != Types::CursorMode::HiddenConfined &&
            state.cursorMode != Types::CursorMode::Relative)
            SetCursor(cursor);
        return IO::successStatus();
    }

    IO::Types::Status setCursorPosition(WindowState &state, Types::LogicalPosition position) noexcept
    {
        const UINT dpi = dpiForWindow(state.platform->handle);
        POINT point{};
        if (!logicalToPhysicalChecked(position.x, dpi, point.x) || !logicalToPhysicalChecked(position.y, dpi, point.y))
        {
            return IO::makeStatus(
                IO::Types::ErrorCode::InvalidArgument,
                ERROR_ARITHMETIC_OVERFLOW,
                "logical cursor position exceeds Win32 range at the effective DPI");
        }
        if (ClientToScreen(state.platform->handle, &point) == FALSE)
            return statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "ClientToScreen cursor");
        if (SetCursorPos(point.x, point.y) == FALSE)
            return statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "SetCursorPos");
        return IO::successStatus();
    }

    Types::LogicalPositionResult cursorPosition(const WindowState &state) noexcept
    {
        POINT point{};
        if (GetCursorPos(&point) == FALSE)
            return {.status = statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "GetCursorPos")};
        if (ScreenToClient(state.platform->handle, &point) == FALSE)
            return {.status = statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "ScreenToClient cursor")};
        const UINT dpi = dpiForWindow(state.platform->handle);
        return {.status = IO::successStatus(), .position = {physicalToLogical(point.x, dpi), physicalToLogical(point.y, dpi)}};
    }
} // namespace GameWIP::Window::Detail::Platform
