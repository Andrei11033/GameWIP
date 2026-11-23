/// @file win32_window.cpp
/// @brief Win32 native class, style, status, and geometry operations.

#include "desktop/platform/win32/internal/win32_window_backend.h"
#include "desktop/platform/win32/internal/test_hooks.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <limits>
#include <mutex>
#include <utility>

namespace GameWIP::Desktop::Platform
{
    /// ------------------------------------------------------------
    /// Native class registry
    /// ------------------------------------------------------------

    namespace
    {
        std::mutex classMutex;
        std::size_t classUsers = 0;
        HINSTANCE classInstance = nullptr;
        bool classOwned = false;
    } // namespace

    /// ------------------------------------------------------------
    /// Native class lifetime
    /// ------------------------------------------------------------

    /// @brief Acquires the process-wide native Window class reference.
    IO::Types::Status acquireWindowClass(HINSTANCE instance) noexcept
    {
        std::scoped_lock lock(classMutex);

        if(classUsers != 0)
        {
            ++classUsers;
            return IO::successStatus();
        }

        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_DBLCLKS;
        windowClass.lpfnWndProc = windowProc;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        windowClass.lpszClassName = kWindowClassName;

        if(RegisterClassExW(&windowClass) == 0)
        {
            const DWORD nativeCode = GetLastError();

            if(nativeCode != ERROR_CLASS_ALREADY_EXISTS)
            {
                return statusFromWin32(IO::Types::ErrorCode::OpenFailed, nativeCode, "RegisterClassExW");
            }
            classOwned = false;
        }
        else
        {
            classOwned = true;
        }
        classInstance = instance;
        classUsers = 1;
        return IO::successStatus();
    }

    /// @brief Releases one process-wide native Window class reference.
    IO::Types::Status releaseWindowClass() noexcept
    {
        std::scoped_lock lock(classMutex);

        if(classUsers == 0)
        {
            return IO::successStatus();
        }
        --classUsers;

        if(classUsers != 0 || !classOwned)
        {
            return IO::successStatus();
        }

        if(UnregisterClassW(kWindowClassName, classInstance) == FALSE)
        {
            ++classUsers;
            return statusFromWin32(IO::Types::ErrorCode::CloseFailed, GetLastError(), "UnregisterClassW");
        }
        classInstance = nullptr;
        classOwned = false;
        return IO::successStatus();
    }

    /// ------------------------------------------------------------
    /// Window identity and native styles
    /// ------------------------------------------------------------

    /// @brief Computes the native frame style from cached portable Window policy.
    DWORD styleFor(const WindowState &state) noexcept
    {
        DWORD style = WS_CLIPCHILDREN | WS_CLIPSIBLINGS;

        if(state.mode != Types::WindowMode::Windowed)
        {
            return style | WS_POPUP;
        }

        if(state.decoration == Types::DecorationMode::System)
        {
            style |= WS_OVERLAPPED | WS_CAPTION;
        }
        else
        {
            style |= WS_POPUP;
        }

        if(state.controls.closable || state.controls.minimizable || state.controls.maximizable)
        {
            style |= WS_SYSMENU;
        }

        if(state.controls.minimizable)
        {
            style |= WS_MINIMIZEBOX;
        }

        if(state.controls.maximizable)
        {
            style |= WS_MAXIMIZEBOX;
        }

        if(state.resizable)
        {
            style |= WS_THICKFRAME;
        }

        return style;
    }

    /// @brief Computes extended native styles for ownership, focus, opacity, and input policy.
    DWORD extendedStyleFor(const WindowState &state) noexcept
    {
        DWORD style = state.owner.isValid() ? 0 : WS_EX_APPWINDOW;

        if(!state.focusable)
        {
            style |= WS_EX_NOACTIVATE;
        }

        if(state.alwaysOnTop)
        {
            style |= WS_EX_TOPMOST;
        }

        if(state.opacity < 1.0F || state.pointerInputMode == Types::PointerInputMode::ClickThrough)
        {
            style |= WS_EX_LAYERED;
        }

        if(state.pointerInputMode == Types::PointerInputMode::ClickThrough)
        {
            style |= WS_EX_TRANSPARENT;
        }

        return style;
    }

    namespace
    {
        /// @brief Sets a window long value while preserving Win32's zero-value ambiguity.
        bool setLong(HWND window, int index, LONG_PTR value, DWORD &nativeCode) noexcept
        {
            SetLastError(ERROR_SUCCESS);
            const LONG_PTR previous = SetWindowLongPtrW(window, index, value);
            nativeCode = GetLastError();
            return previous != 0 || nativeCode == ERROR_SUCCESS;
        }
    } // namespace

    /// ------------------------------------------------------------
    /// Native status conversion
    /// ------------------------------------------------------------

    /// @brief Maps a Win32 error and operation name into a portable status.
    IO::Types::Status statusFromWin32(IO::Types::ErrorCode fallback, DWORD nativeCode, std::string_view operation) noexcept
    {
        using IO::Types::ErrorCode;
        ErrorCode code = fallback;

        switch(nativeCode)
        {
        case ERROR_ACCESS_DENIED:
            code = ErrorCode::PermissionDenied;
            break;
        case ERROR_NOT_ENOUGH_MEMORY:
        case ERROR_OUTOFMEMORY:
            code = ErrorCode::OutOfMemory;
            break;
        case ERROR_INVALID_PARAMETER:
        case ERROR_INVALID_HANDLE:
            code = ErrorCode::InvalidArgument;
            break;
        case ERROR_BUSY:
        case ERROR_SHARING_VIOLATION:
            code = ErrorCode::ResourceBusy;
            break;
        case ERROR_FILE_NOT_FOUND:
        case ERROR_NOT_FOUND:
            code = ErrorCode::NotFound;
            break;
        default:
            break;
        }

        try
        {
            std::array<char, 512> buffer{};
            const DWORD length = FormatMessageA(
                FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr,
                nativeCode,
                0,
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                nullptr);
            std::string message(operation);

            if(length != 0)
            {
                message.append(": ");
                message.append(buffer.data(), length);
            }

            return IO::makeStatus(code, nativeCode, std::move(message));
        }
        catch(...)
        {
            return IO::makeStatus(code, nativeCode);
        }
    }

    /// @brief Maps a display-mode change result into a portable status.
    IO::Types::Status statusFromDisplayChange(LONG nativeCode, std::string_view operation) noexcept
    {
        IO::Types::ErrorCode code = IO::Types::ErrorCode::NativeFailure;

        if(nativeCode == DISP_CHANGE_BADMODE || nativeCode == DISP_CHANGE_BADPARAM)
        {
            code = IO::Types::ErrorCode::InvalidArgument;
        }
        else if(nativeCode == DISP_CHANGE_NOTUPDATED)
        {
            code = IO::Types::ErrorCode::PermissionDenied;
        }
        try
        {
            return IO::makeStatus(code, nativeCode, std::format("{} failed with display status {}", operation, nativeCode));
        }
        catch(...)
        {
            return IO::makeStatus(code, nativeCode);
        }
    }

    /// ------------------------------------------------------------
    /// Geometry and hit testing
    /// ------------------------------------------------------------

    /// @brief Returns the effective per-monitor DPI for a Window or the current thread context.
    UINT dpiForWindow(HWND window) noexcept
    {
        const UINT dpi = window != nullptr ? GetDpiForWindow(window) : GetDpiForSystem();
        return dpi == 0 ? kBaselineDpi : dpi;
    }

    /// @brief Converts one logical coordinate using rounded DPI scaling.
    LONG logicalToPhysical(std::int32_t value, UINT dpi) noexcept
    {
        LONG output = -1;
        static_cast<void>(logicalToPhysicalChecked(value, dpi, output));
        return output;
    }

    /// @brief Converts one logical coordinate while rejecting Win32 LONG overflow.
    bool logicalToPhysicalChecked(std::int32_t value, UINT dpi, LONG &output) noexcept
    {
        if(dpi == 0)
        {
            return false;
        }
        const std::int64_t product = static_cast<std::int64_t>(value) * dpi;
        const std::int64_t rounded = product >= 0 ? (product + kBaselineDpi / 2) / kBaselineDpi : (product - kBaselineDpi / 2) / kBaselineDpi;

        if(rounded < std::numeric_limits<LONG>::min() || rounded > std::numeric_limits<LONG>::max())
        {
            return false;
        }
        output = static_cast<LONG>(rounded);
        return true;
    }

    /// @brief Converts one physical coordinate to logical units at a given DPI.
    std::int32_t physicalToLogical(LONG value, UINT dpi) noexcept
    {
        return MulDiv(value, kBaselineDpi, static_cast<int>(dpi));
    }

    /// @brief Converts logical dimensions to physical pixels.
    Types::PixelSize logicalToPhysicalSize(Types::LogicalSize value, UINT dpi) noexcept
    {
        return {
            static_cast<std::uint32_t>(logicalToPhysical(static_cast<std::int32_t>(value.width), dpi)),
            static_cast<std::uint32_t>(logicalToPhysical(static_cast<std::int32_t>(value.height), dpi))};
    }

    /// @brief Converts physical dimensions to logical units.
    Types::LogicalSize physicalToLogicalSize(std::uint32_t width, std::uint32_t height, UINT dpi) noexcept
    {
        return {
            static_cast<std::uint32_t>(std::max(0, physicalToLogical(static_cast<LONG>(width), dpi))),
            static_cast<std::uint32_t>(std::max(0, physicalToLogical(static_cast<LONG>(height), dpi)))};
    }

    /// @brief Tests whether a logical point lies within a logical rectangle.
    bool pointInRect(Types::LogicalPosition point, const Types::LogicalRect &rect) noexcept
    {
        const std::int64_t right = static_cast<std::int64_t>(rect.position.x) + rect.size.width;
        const std::int64_t bottom = static_cast<std::int64_t>(rect.position.y) + rect.size.height;
        return point.x >= rect.position.x && point.y >= rect.position.y && point.x < right && point.y < bottom;
    }

    /// @brief Loads a system cursor resource for the portable cursor shape.
    HCURSOR loadCursor(Types::CursorShape shape) noexcept
    {
        if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::SystemCursorLoad))
        {
            SetLastError(ERROR_GEN_FAILURE);
            return nullptr;
        }

        WORD identifier = 32512;

        switch(shape)
        {
        case Types::CursorShape::Arrow:
            identifier = 32512;
            break;
        case Types::CursorShape::Text:
            identifier = 32513;
            break;
        case Types::CursorShape::Wait:
            identifier = 32514;
            break;
        case Types::CursorShape::Crosshair:
            identifier = 32515;
            break;
        case Types::CursorShape::ResizeDiagonalNorthWestSouthEast:
            identifier = 32642;
            break;
        case Types::CursorShape::ResizeDiagonalNorthEastSouthWest:
            identifier = 32643;
            break;
        case Types::CursorShape::ResizeHorizontal:
            identifier = 32644;
            break;
        case Types::CursorShape::ResizeVertical:
            identifier = 32645;
            break;
        case Types::CursorShape::Move:
        case Types::CursorShape::ResizeAll:
            identifier = 32646;
            break;
        case Types::CursorShape::NotAllowed:
            identifier = 32648;
            break;
        case Types::CursorShape::Hand:
            identifier = 32649;
            break;
        case Types::CursorShape::Progress:
            identifier = 32650;
            break;
        case Types::CursorShape::Help:
            identifier = 32651;
            break;
        }

        return LoadCursorW(nullptr, MAKEINTRESOURCEW(identifier));
    }

    /// ------------------------------------------------------------
    /// Cached native state
    /// ------------------------------------------------------------

    /// @brief Re-reads native client geometry and republishes cached Window values.
    IO::Types::Status refreshCachedGeometry(WindowState &state) noexcept
    {
        if(!state.platform || state.platform->handle == nullptr)
        {
            return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
        }

        RECT client{};
        RECT frame{};
        POINT clientOrigin{};

        if(GetClientRect(state.platform->handle, &client) == FALSE || ClientToScreen(state.platform->handle, &clientOrigin) == FALSE ||
           GetWindowRect(state.platform->handle, &frame) == FALSE)
        {
            return statusFromWin32(IO::Types::ErrorCode::StatFailed, GetLastError(), "query window geometry");
        }
        const UINT dpi = dpiForWindow(state.platform->handle);
        const auto physicalWidth = static_cast<std::uint32_t>(std::max<LONG>(0, client.right - client.left));
        const auto physicalHeight = static_cast<std::uint32_t>(std::max<LONG>(0, client.bottom - client.top));
        state.framebufferSize = {physicalWidth, physicalHeight};
        const RendererIntegrationState *renderer = state.rendererIntegration;

        if(renderer != nullptr && ((renderer->pointerHitMaskActiveGeneration != 0 && renderer->pointerHitMaskSize != state.framebufferSize) ||
                                   (renderer->pointerHitMaskTargetGeneration != 0 && renderer->pointerHitMaskTargetSize != state.framebufferSize)))
        {
            invalidatePointerHitMask(state);
        }
        state.clientSize = physicalToLogicalSize(physicalWidth, physicalHeight, dpi);
        state.clientPosition = {clientOrigin.x, clientOrigin.y};
        state.frameRect = {
            {frame.left, frame.top},
            {static_cast<std::uint32_t>(std::max<LONG>(0, frame.right - frame.left)),
             static_cast<std::uint32_t>(std::max<LONG>(0, frame.bottom - frame.top))}};
        state.frameInsets = {
            static_cast<std::uint32_t>(std::max(0, physicalToLogical(clientOrigin.x - frame.left, dpi))),
            static_cast<std::uint32_t>(std::max(0, physicalToLogical(clientOrigin.y - frame.top, dpi))),
            static_cast<std::uint32_t>(std::max(0, physicalToLogical(frame.right - (clientOrigin.x + static_cast<LONG>(physicalWidth)), dpi))),
            static_cast<std::uint32_t>(std::max(0, physicalToLogical(frame.bottom - (clientOrigin.y + static_cast<LONG>(physicalHeight)), dpi)))};
        state.dpi = {static_cast<float>(dpi), static_cast<float>(dpi)};
        state.contentScale = {static_cast<float>(dpi) / static_cast<float>(kBaselineDpi), static_cast<float>(dpi) / static_cast<float>(kBaselineDpi)};

        if(state.presentationPublication != nullptr)
        {
            state.presentationPublication->publishFramebufferSize(state.framebufferSize);
            state.presentationPublication->publishClientSize(state.clientSize);
            state.presentationPublication->publishDpi(state.dpi);
            state.presentationPublication->publishContentScale(state.contentScale);
        }
        refreshChildSurfaceScreenRectsForParent(state.id);
        publishAccessibilityGeometry(state);
        return IO::successStatus();
    }

    /// @brief Recomputes the monitor containing the Window and publishes changes.
    void updateCurrentMonitor(WindowState &state) noexcept
    {
        if(!state.platform || state.platform->handle == nullptr)
        {
            return;
        }
        HMONITOR native = MonitorFromWindow(state.platform->handle, MONITOR_DEFAULTTONEAREST);
        Types::Result<Types::Display::Info> info = monitorFromNative(native);

        if(!info.ok())
        {
            recordPumpFailure(std::move(info.status));
            return;
        }

        if(info->id != state.monitor)
        {
            const Types::Display::MonitorId previous = state.monitor;
            state.monitor = info->id;

            if(state.presentationPublication != nullptr)
            {
                state.presentationPublication->publishMonitor(state.monitor);
            }
            routeEvent(state, Types::Events::MonitorChanged{previous, state.monitor});
        }
    }

    /// @brief Applies cursor visibility, confinement, and relative-motion policy.
    IO::Types::Status applyCursorState(WindowState &state) noexcept
    {
        if(!state.platform || state.platform->handle == nullptr)
        {
            return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
        }

        if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::Cursor))
        {
            return IO::makeStatus(IO::Types::ErrorCode::NativeFailure);
        }

        WindowData &data = *state.platform;
        const bool confined = state.cursorMode == Types::CursorMode::Confined || state.cursorMode == Types::CursorMode::HiddenConfined ||
                              state.cursorMode == Types::CursorMode::Relative;
        const bool shouldClip = confined && state.focused && state.visible && state.presentation != Types::PresentationState::Minimized;

        if(!shouldClip)
        {
            if(data.cursorClipApplied && ClipCursor(nullptr) == FALSE)
            {
                return statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "ClipCursor release");
            }
            data.cursorClipApplied = false;
            return IO::successStatus();
        }

        RECT client{};
        POINT topLeft{};
        POINT bottomRight{};

        if(GetClientRect(data.handle, &client) == FALSE)
        {
            return statusFromWin32(IO::Types::ErrorCode::StatFailed, GetLastError(), "GetClientRect for cursor");
        }
        topLeft = {client.left, client.top};
        bottomRight = {client.right, client.bottom};

        if(ClientToScreen(data.handle, &topLeft) == FALSE || ClientToScreen(data.handle, &bottomRight) == FALSE)
        {
            return statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "ClientToScreen for cursor");
        }
        RECT clip{topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};

        if(ClipCursor(&clip) == FALSE)
        {
            return statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "ClipCursor");
        }

        data.cursorClipApplied = true;

        if(state.cursorMode == Types::CursorMode::Relative)
        {
            const int x = topLeft.x + (bottomRight.x - topLeft.x) / 2;
            const int y = topLeft.y + (bottomRight.y - topLeft.y) / 2;

            if(SetCursorPos(x, y) == FALSE)
            {
                return statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "SetCursorPos relative center");
            }
        }

        return IO::successStatus();
    }

    IO::Types::Status applyStyle(WindowState &state) noexcept
    {
        if(!state.platform || state.platform->handle == nullptr)
        {
            return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
        }

        // ShowWindow, EnableWindow, minimize, and maximize own these runtime bits. Rebuilding
        // the configurable frame must not make a still-painted HWND invisible to Explorer or
        // silently re-enable/restore it by replacing the complete style word.
        constexpr DWORD runtimeStyleBits = WS_VISIBLE | WS_DISABLED | WS_MINIMIZE | WS_MAXIMIZE;
        LONG_PTR currentStyleValue = 0;
        IO::Types::Status queryStatus = queryWindowLong(state.platform->handle, GWL_STYLE, currentStyleValue, "GetWindowLongPtrW style");

        if(!queryStatus.ok())
        {
            return queryStatus;
        }

        const DWORD currentStyle = static_cast<DWORD>(currentStyleValue);
        const DWORD desiredStyle = styleFor(state) | (currentStyle & runtimeStyleBits);

        // Window owns these extended-style policies. Preserve unrelated native state such as
        // WS_EX_ACCEPTFILES, which DragAcceptFiles manages independently.
        constexpr DWORD controlledExtendedStyleBits = WS_EX_APPWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT;
        LONG_PTR currentExtendedStyleValue = 0;
        queryStatus = queryWindowLong(state.platform->handle, GWL_EXSTYLE, currentExtendedStyleValue, "GetWindowLongPtrW extended style");

        if(!queryStatus.ok())
        {
            return queryStatus;
        }

        const DWORD currentExtendedStyle = static_cast<DWORD>(currentExtendedStyleValue);
        const DWORD desiredExtendedStyle = extendedStyleFor(state) | (currentExtendedStyle & ~controlledExtendedStyleBits);
        DWORD nativeCode = ERROR_SUCCESS;

        if(!setLong(state.platform->handle, GWL_STYLE, desiredStyle, nativeCode))
        {
            return statusFromWin32(IO::Types::ErrorCode::NativeFailure, nativeCode, "SetWindowLongPtrW style");
        }

        if(!setLong(state.platform->handle, GWL_EXSTYLE, desiredExtendedStyle, nativeCode))
        {
            return statusFromWin32(IO::Types::ErrorCode::NativeFailure, nativeCode, "SetWindowLongPtrW extended style");
        }

        if(SetWindowPos(
               state.platform->handle,
               state.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
               0,
               0,
               0,
               0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED) == FALSE)
        {
            return statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "SetWindowPos frame change");
        }
        return refreshCachedGeometry(state);
    }

} // namespace GameWIP::Desktop::Platform
