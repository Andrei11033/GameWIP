/// @file win32_controls.cpp
/// @brief Win32 presentation, chrome, interaction, and cursor operations for Window.

#include "desktop/internal/results.h"
#include "desktop/platform/win32/internal/win32_window_backend.h"
#include "desktop/platform/dialogs.h"
#include "desktop/platform/drag_drop.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace GameWIP::Desktop::Platform
{
    /// ------------------------------------------------------------
    /// Cached presentation synchronization
    /// ------------------------------------------------------------

    namespace
    {
        /// @brief Publishes a visibility transition observed from the native HWND.
        void synchronizeVisibility(WindowState &state) noexcept
        {
            const bool visible = IsWindowVisible(state.platform->handle) != FALSE;

            if(visible != state.visible)
            {
                state.visible = visible;

                if(state.presentationPublication != nullptr)
                {
                    state.presentationPublication->publishVisible(visible);
                }
                routeEvent(state, Types::Events::VisibilityChanged{visible});
            }
        }

        /// @brief Publishes minimized/maximized/normal state observed from the native HWND.
        void synchronizePresentation(WindowState &state) noexcept
        {
            const Types::PresentationState value = IsIconic(state.platform->handle)   ? Types::PresentationState::Minimized
                                                   : IsZoomed(state.platform->handle) ? Types::PresentationState::Maximized
                                                                                      : Types::PresentationState::Normal;

            if(value != state.presentation)
            {
                state.presentation = value;

                if(state.presentationPublication != nullptr)
                {
                    state.presentationPublication->publishPresentationState(value);
                }
                routeEvent(state, Types::Events::PresentationStateChanged{value});
            }
        }

        /// @brief Applies a cached style change transactionally and restores the old value on failure.
        template <typename Value, typename Apply>
        [[nodiscard]] GameWIP::Status::Types::Status updateStyleValue(
            WindowState &state,
            Value &destination,
            const Value &value,
            Apply &&apply) noexcept
        {
            if(destination == value)
            {
                return GameWIP::Status::Types::Status{};
            }
            const Value previous = destination;
            destination = value;
            GameWIP::Status::Types::Status status = applyStyle(state);

            if(status.ok())
            {
                status = apply();
            }

            if(!status.ok())
            {
                destination = previous;
                GameWIP::Status::Types::Status rollback = applyStyle(state);
                return rollback.ok() ? std::move(status) : std::move(rollback);
            }

            return status;
        }
    } // namespace

    /// ------------------------------------------------------------
    /// Visibility, focus, and presentation
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Status show(WindowState &state) noexcept
    {
        ShowWindow(state.platform->handle, SW_SHOWNOACTIVATE);
        synchronizeVisibility(state);
        synchronizePresentation(state);
        return applyCursorState(state);
    }

    GameWIP::Status::Types::Status hide(WindowState &state) noexcept
    {
        ShowWindow(state.platform->handle, SW_HIDE);
        synchronizeVisibility(state);
        return applyCursorState(state);
    }

    GameWIP::Status::Types::Status requestFocus(WindowState &state) noexcept
    {
        if(!state.focusable)
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::Unsupported);
        }

        if(IsWindowVisible(state.platform->handle) == FALSE)
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::InvalidArgument);
        }

        if(IsIconic(state.platform->handle) != FALSE)
        {
            ShowWindow(state.platform->handle, SW_RESTORE);
        }
        const HWND previous = SetFocus(state.platform->handle);

        if(GetFocus() != state.platform->handle && SetForegroundWindow(state.platform->handle) == FALSE)
        {
            if(previous != nullptr)
            {
                SetFocus(previous);
            }

            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::PermissionDenied);
        }

        return GameWIP::Status::Types::Status{};
    }

    GameWIP::Status::Types::Status requestAttention(WindowState &state) noexcept
    {
        FLASHWINFO info{};
        info.cbSize = sizeof(info);
        info.hwnd = state.platform->handle;
        info.dwFlags = FLASHW_TRAY | FLASHW_TIMERNOFG;
        info.uCount = 3;
        info.dwTimeout = 0;
        // FlashWindowEx returns the Window's prior active state, not operation success.
        static_cast<void>(FlashWindowEx(&info));
        return GameWIP::Status::Types::Status{};
    }

    GameWIP::Status::Types::Status minimize(WindowState &state) noexcept
    {
        if(!state.controls.minimizable)
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::Unsupported);
        }
        ShowWindow(state.platform->handle, SW_MINIMIZE);
        synchronizePresentation(state);
        return applyCursorState(state);
    }

    GameWIP::Status::Types::Status maximize(WindowState &state) noexcept
    {
        if(!state.controls.maximizable || !state.resizable)
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::Unsupported);
        }
        ShowWindow(state.platform->handle, SW_MAXIMIZE);
        synchronizePresentation(state);
        return refreshCachedGeometry(state);
    }

    GameWIP::Status::Types::Status restore(WindowState &state) noexcept
    {
        ShowWindow(state.platform->handle, SW_RESTORE);
        synchronizePresentation(state);
        return refreshCachedGeometry(state);
    }

    /// ------------------------------------------------------------
    /// Chrome and interaction policy
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Status setResizable(WindowState &state, bool resizable) noexcept
    {
        return updateStyleValue(
            state,
            state.resizable,
            resizable,
            []
            {
                return GameWIP::Status::Types::Status{};
            });
    }

    GameWIP::Status::Types::Status setDecorationMode(WindowState &state, Types::DecorationMode mode) noexcept
    {
        return updateStyleValue(
            state,
            state.decoration,
            mode,
            []
            {
                return GameWIP::Status::Types::Status{};
            });
    }

    GameWIP::Status::Types::Status setControls(WindowState &state, const Types::Controls &controls) noexcept
    {
        return updateStyleValue(
            state,
            state.controls,
            controls,
            []
            {
                return GameWIP::Status::Types::Status{};
            });
    }

    GameWIP::Status::Types::Status setFocusable(WindowState &state, bool focusable) noexcept
    {
        return updateStyleValue(
            state,
            state.focusable,
            focusable,
            [&state, focusable]
            {
                if(!focusable && GetFocus() == state.platform->handle)
                {
                    SetFocus(nullptr);
                }

                return GameWIP::Status::Types::Status{};
            });
    }

    GameWIP::Status::Types::Status setUserInteractionEnabled(WindowState &state, bool enabled) noexcept
    {
        if(state.interactionEnabled == enabled)
        {
            return GameWIP::Status::Types::Status{};
        }
        const bool effectiveEnabled = enabled && !windowHasBlockingProgressDialog(state);
        EnableWindow(state.platform->handle, effectiveEnabled ? TRUE : FALSE);

        if((IsWindowEnabled(state.platform->handle) != FALSE) != effectiveEnabled)
        {
            return statusFromWin32(GameWIP::Status::Types::Code::NativeFailure, ERROR_FUNCTION_FAILED, "EnableWindow");
        }
        state.interactionEnabled = enabled;
        return GameWIP::Status::Types::Status{};
    }

    GameWIP::Status::Types::Status setAlwaysOnTop(WindowState &state, bool alwaysOnTop) noexcept
    {
        return updateStyleValue(
            state,
            state.alwaysOnTop,
            alwaysOnTop,
            []
            {
                return GameWIP::Status::Types::Status{};
            });
    }

    GameWIP::Status::Types::Status setOpacity(WindowState &state, float opacity) noexcept
    {
        const float previous = state.opacity;
        state.opacity = opacity;
        GameWIP::Status::Types::Status status = applyStyle(state);

        if(status.ok() && (opacity < 1.0F || state.pointerInputMode == Types::PointerInputMode::ClickThrough))
        {
            const BYTE alpha = static_cast<BYTE>(std::lround(std::clamp(opacity, 0.0F, 1.0F) * 255.0F));

            if(SetLayeredWindowAttributes(state.platform->handle, 0, alpha, LWA_ALPHA) == FALSE)
            {
                status = statusFromWin32(GameWIP::Status::Types::Code::NativeFailure, GetLastError(), "SetLayeredWindowAttributes");
            }
        }

        if(!status.ok())
        {
            state.opacity = previous;
            GameWIP::Status::Types::Status rollback = applyStyle(state);
            return rollback.ok() ? std::move(status) : std::move(rollback);
        }

        return status;
    }

    GameWIP::Status::Types::Status setBackdropEffect(WindowState &state, Types::BackdropEffect effect) noexcept
    {
        if(effect == Types::BackdropEffect::None && !supportsSystemBackdrop())
        {
            state.backdrop = effect;
            return GameWIP::Status::Types::Status{};
        }

        if(!supportsSystemBackdrop())
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::Unsupported);
        }

        DWM_SYSTEMBACKDROP_TYPE nativeEffect = DWMSBT_NONE;

        switch(effect)
        {
        case Types::BackdropEffect::None:
            nativeEffect = DWMSBT_NONE;
            break;
        case Types::BackdropEffect::Automatic:
            nativeEffect = DWMSBT_AUTO;
            break;
        case Types::BackdropEffect::MainWindow:
            nativeEffect = DWMSBT_MAINWINDOW;
            break;
        case Types::BackdropEffect::TransientWindow:
            nativeEffect = DWMSBT_TRANSIENTWINDOW;
            break;
        case Types::BackdropEffect::TabbedWindow:
            nativeEffect = DWMSBT_TABBEDWINDOW;
            break;
        }
        const HRESULT result = DwmSetWindowAttribute(state.platform->handle, DWMWA_SYSTEMBACKDROP_TYPE, &nativeEffect, sizeof(nativeEffect));

        if(FAILED(result))
        {
            return GameWIP::Desktop::Internal::statusFor(
                effect != Types::BackdropEffect::None ? GameWIP::Status::Types::Code::Unsupported : GameWIP::Status::Types::Code::NativeFailure,
                result);
        }
        state.backdrop = effect;
        return GameWIP::Status::Types::Status{};
    }

    GameWIP::Status::Types::Status setFileDropEnabled(WindowState &state, bool enabled) noexcept
    {
        if(enabled && hasDragDropTarget(state))
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::ResourceBusy);
        }
        DragAcceptFiles(state.platform->handle, enabled ? TRUE : FALSE);
        state.fileDropEnabled = enabled;
        return GameWIP::Status::Types::Status{};
    }

    GameWIP::Status::Types::Status setCustomChromeLayout(WindowState &state) noexcept
    {
        if(SetWindowPos(state.platform->handle, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED) ==
           FALSE)
        {
            return statusFromWin32(GameWIP::Status::Types::Code::NativeFailure, GetLastError(), "refresh custom chrome");
        }
        RedrawWindow(state.platform->handle, nullptr, nullptr, RDW_FRAME | RDW_INVALIDATE);
        return GameWIP::Status::Types::Status{};
    }

    GameWIP::Status::Types::Status setPointerInputLayout(WindowState &state) noexcept
    {
        GameWIP::Status::Types::Status status = applyStyle(state);

        if(!status.ok())
        {
            return status;
        }

        if(state.pointerInputMode == Types::PointerInputMode::ClickThrough || state.opacity < 1.0F)
        {
            const BYTE alpha = static_cast<BYTE>(std::lround(std::clamp(state.opacity, 0.0F, 1.0F) * 255.0F));

            if(SetLayeredWindowAttributes(state.platform->handle, 0, alpha, LWA_ALPHA) == FALSE)
            {
                return statusFromWin32(GameWIP::Status::Types::Code::NativeFailure, GetLastError(), "SetLayeredWindowAttributes pointer policy");
            }
        }

        return GameWIP::Status::Types::Status{};
    }

    /// ------------------------------------------------------------
    /// Cursor controls
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Status setCursorMode(WindowState &state, Types::CursorMode mode) noexcept
    {
        if(state.cursorMode == mode)
        {
            return GameWIP::Status::Types::Status{};
        }
        const Types::CursorMode previous = state.cursorMode;
        state.cursorMode = mode;
        GameWIP::Status::Types::Status status = applyCursorState(state);

        if(!status.ok())
        {
            state.cursorMode = previous;
            static_cast<void>(applyCursorState(state));
            return status;
        }

        if(state.cursorInside)
        {
            SetCursor(
                mode == Types::CursorMode::Hidden || mode == Types::CursorMode::HiddenConfined || mode == Types::CursorMode::Relative
                    ? nullptr
                    : state.platform->cursor);
        }

        return GameWIP::Status::Types::Status{};
    }

    GameWIP::Status::Types::Status setCursorShape(WindowState &state, Types::CursorShape shape) noexcept
    {
        HCURSOR cursor = loadCursor(shape);

        if(cursor == nullptr)
        {
            return statusFromWin32(GameWIP::Status::Types::Code::NativeFailure, GetLastError(), "LoadCursorW");
        }

        GameWIP::Status::Types::Status status = replaceCustomCursorWithSystem(state, cursor);

        if(!status.ok())
        {
            return status;
        }

        state.cursorShape = shape;
        return GameWIP::Status::Types::Status{};
    }

    GameWIP::Status::Types::Status setCursorPosition(WindowState &state, Types::LogicalPosition position) noexcept
    {
        const UINT dpi = dpiForWindow(state.platform->handle);
        POINT point{};

        if(!logicalToPhysicalChecked(position.x, dpi, point.x) || !logicalToPhysicalChecked(position.y, dpi, point.y))
        {
            return GameWIP::Desktop::Internal::statusFor(
                GameWIP::Status::Types::Code::InvalidArgument,
                ERROR_ARITHMETIC_OVERFLOW,
                "logical cursor position exceeds Win32 range at the effective DPI");
        }

        if(ClientToScreen(state.platform->handle, &point) == FALSE)
        {
            return statusFromWin32(GameWIP::Status::Types::Code::NativeFailure, GetLastError(), "ClientToScreen cursor");
        }

        if(SetCursorPos(point.x, point.y) == FALSE)
        {
            return statusFromWin32(GameWIP::Status::Types::Code::NativeFailure, GetLastError(), "SetCursorPos");
        }

        return GameWIP::Status::Types::Status{};
    }

    GameWIP::Status::Types::Result<Types::LogicalPosition> cursorPosition(const WindowState &state) noexcept
    {
        POINT point{};

        if(GetCursorPos(&point) == FALSE)
        {
            return GameWIP::Status::Types::Result<Types::LogicalPosition>::failure(
                statusFromWin32(GameWIP::Status::Types::Code::NativeFailure, GetLastError(), "GetCursorPos"));
        }

        if(ScreenToClient(state.platform->handle, &point) == FALSE)
        {
            return GameWIP::Status::Types::Result<Types::LogicalPosition>::failure(
                statusFromWin32(GameWIP::Status::Types::Code::NativeFailure, GetLastError(), "ScreenToClient cursor"));
        }
        const UINT dpi = dpiForWindow(state.platform->handle);
        return GameWIP::Status::Types::Result<Types::LogicalPosition>::success(
            Types::LogicalPosition{physicalToLogical(point.x, dpi), physicalToLogical(point.y, dpi)});
    }
} // namespace GameWIP::Desktop::Platform
