/// @file win32_window_lifecycle.cpp
/// @brief Win32 Window open, close, deferred cleanup, and native-handle access.

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
    // Native lifecycle
    // ------------------------------------------------------------
    void WindowDataDeleter::operator()(WindowData *data) const noexcept
    {
        delete data;
    }

    IO::Types::Status open(WindowState &state, const Types::Description &description) noexcept
    {
        try
        {
            const DPI_AWARENESS_CONTEXT context = GetThreadDpiAwarenessContext();
            if (context == nullptr || AreDpiAwarenessContextsEqual(context, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) == FALSE)
            {
                return IO::makeStatus(IO::Types::ErrorCode::Unsupported, 0, "Window requires a Per-Monitor-V2-aware executable manifest");
            }
            const Types::Capabilities capabilities = getCapabilities().capabilities;
            if (description.transparentFramebuffer && !capabilities.supports(Types::Capability::TransparentFramebuffer))
            {
                return IO::makeStatus(IO::Types::ErrorCode::Unsupported, 0, "transparentFramebuffer requires Windows 11 build 26100 or newer");
            }
            if (description.backdropEffect != Types::BackdropEffect::None && !capabilities.supports(Types::Capability::SystemBackdrop))
            {
                return IO::makeStatus(IO::Types::ErrorCode::Unsupported, 0, "system backdrop effects require Windows 11 build 22621 or newer");
            }

            auto data = std::unique_ptr<WindowData, WindowDataDeleter>(new WindowData{});
            data->owner = &state;
            data->instance = GetModuleHandleW(nullptr);
            data->ownerThreadId = GetCurrentThreadId();
            data->windowedPlacement.length = sizeof(WINDOWPLACEMENT);
            if (data->instance == nullptr)
                return statusFromWin32(IO::Types::ErrorCode::OpenFailed, GetLastError(), "GetModuleHandleW");

            IO::Types::Status status = acquireWindowClass(data->instance);
            if (!status.ok())
                return status;
            data->classReferenceHeld = true;
            state.platform = std::move(data);

            if ((description.placement.monitor.isValid() && nativeMonitor(description.placement.monitor) == nullptr) ||
                (description.mode.monitor.isValid() && nativeMonitor(description.mode.monitor) == nullptr))
            {
                return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
            }

            if (wakeMessage() == 0)
                return statusFromWin32(IO::Types::ErrorCode::OpenFailed, GetLastError(), "RegisterWindowMessageW wake");

            if (Detail::consumeFailure(TestHooks::FailurePoint::Dispatcher))
                return IO::makeStatus(IO::Types::ErrorCode::OpenFailed);

            registerOpenState(state);

            DWORD nativeCode = ERROR_SUCCESS;
            if (Detail::consumeFailure(TestHooks::FailurePoint::TitleConversion))
                return IO::makeStatus(IO::Types::ErrorCode::EncodingFailed);
            if (!utf8ToUtf16(description.title, state.platform->utf16Scratch, nativeCode))
                return statusFromWin32(IO::Types::ErrorCode::InvalidArgument, nativeCode, "convert window title to UTF-16");

            HWND ownerHandle = nullptr;
            if (description.owner.isValid())
            {
                WindowState *owner = resolveWindowId(description.owner);
                if (owner == nullptr || !owner->platform || owner->platform->ownerThreadId != state.platform->ownerThreadId)
                {
                    return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
                }
                ownerHandle = owner->platform->handle;
            }

            const UINT dpi = dpiForWindow(nullptr);
            const Types::PixelSize physicalClient = logicalToPhysicalSize(description.clientSize, dpi);
            if (physicalClient.width == 0 || physicalClient.height == 0 ||
                physicalClient.width > static_cast<std::uint32_t>(std::numeric_limits<LONG>::max()) ||
                physicalClient.height > static_cast<std::uint32_t>(std::numeric_limits<LONG>::max()))
            {
                return IO::makeStatus(
                    IO::Types::ErrorCode::InvalidArgument,
                    ERROR_ARITHMETIC_OVERFLOW,
                    "initial client size exceeds Win32 range at the effective DPI");
            }
            RECT outer{0, 0, static_cast<LONG>(physicalClient.width), static_cast<LONG>(physicalClient.height)};
            const DWORD style = styleFor(state);
            const DWORD extendedStyle = extendedStyleFor(state);
            if (AdjustWindowRectExForDpi(&outer, style, FALSE, extendedStyle, dpi) == FALSE)
                return statusFromWin32(IO::Types::ErrorCode::OpenFailed, GetLastError(), "AdjustWindowRectExForDpi create");
            const std::int64_t outerWidth = static_cast<std::int64_t>(outer.right) - outer.left;
            const std::int64_t outerHeight = static_cast<std::int64_t>(outer.bottom) - outer.top;
            if (outerWidth <= 0 || outerWidth > std::numeric_limits<int>::max() || outerHeight <= 0 || outerHeight > std::numeric_limits<int>::max())
            {
                return IO::makeStatus(
                    IO::Types::ErrorCode::InvalidArgument,
                    ERROR_ARITHMETIC_OVERFLOW,
                    "initial outer frame size exceeds Win32 range");
            }

            int x = CW_USEDEFAULT;
            int y = CW_USEDEFAULT;
            if (description.placement.kind == Types::PlacementKind::Explicit)
            {
                const std::int64_t wideX = static_cast<std::int64_t>(description.placement.position.x) + outer.left;
                const std::int64_t wideY = static_cast<std::int64_t>(description.placement.position.y) + outer.top;
                if (wideX < std::numeric_limits<int>::min() || wideX > std::numeric_limits<int>::max() || wideY < std::numeric_limits<int>::min() ||
                    wideY > std::numeric_limits<int>::max())
                {
                    return IO::makeStatus(
                        IO::Types::ErrorCode::InvalidArgument,
                        ERROR_ARITHMETIC_OVERFLOW,
                        "initial outer frame position exceeds Win32 range");
                }
                x = static_cast<int>(wideX);
                y = static_cast<int>(wideY);
            }
            else if (description.placement.kind == Types::PlacementKind::Centered)
            {
                HMONITOR monitor = description.placement.monitor.isValid() ? nativeMonitor(description.placement.monitor)
                                                                           : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
                MONITORINFO info{};
                info.cbSize = sizeof(info);
                if (monitor == nullptr || GetMonitorInfoW(monitor, &info) == FALSE)
                    return statusFromWin32(IO::Types::ErrorCode::InvalidArgument, GetLastError(), "resolve centered monitor");
                const int width = static_cast<int>(outerWidth);
                const int height = static_cast<int>(outerHeight);
                x = info.rcWork.left + (info.rcWork.right - info.rcWork.left - width) / 2;
                y = info.rcWork.top + (info.rcWork.bottom - info.rcWork.top - height) / 2;
            }

            if (Detail::consumeFailure(TestHooks::FailurePoint::NativeCreation))
                return IO::makeStatus(IO::Types::ErrorCode::OpenFailed);

            state.platform->handle = CreateWindowExW(
                extendedStyle,
                kWindowClassName,
                state.platform->utf16Scratch.c_str(),
                style,
                x,
                y,
                static_cast<int>(outerWidth),
                static_cast<int>(outerHeight),
                ownerHandle,
                nullptr,
                state.platform->instance,
                &state);
            if (state.platform->handle == nullptr)
                return statusFromWin32(IO::Types::ErrorCode::OpenFailed, GetLastError(), "CreateWindowExW");

            registerWindowId(state);
            if (Detail::consumeFailure(TestHooks::FailurePoint::PartialOpen))
                return IO::makeStatus(IO::Types::ErrorCode::NativeFailure);
            state.platform->windowedStyle = style;
            state.platform->windowedExtendedStyle = extendedStyle;
            state.platform->cursor = loadCursor(state.cursorShape);
            if (state.platform->cursor == nullptr)
                return statusFromWin32(IO::Types::ErrorCode::OpenFailed, GetLastError(), "LoadCursorW");

            status = refreshCachedGeometry(state);
            if (!status.ok())
                return status;
            updateCurrentMonitor(state);

            if (state.transparentFramebuffer)
            {
                const BOOL enabled = TRUE;
                const HRESULT result = DwmSetWindowAttribute(state.platform->handle, Compat::kRedirectionBitmapAlpha, &enabled, sizeof(enabled));
                if (FAILED(result))
                    return IO::makeStatus(IO::Types::ErrorCode::Unsupported, result);
            }

            status = setFileDropEnabled(state, description.fileDropEnabled);
            if (!status.ok())
                return status;
            status = setOpacity(state, description.opacity);
            if (!status.ok())
                return status;
            status = setBackdropEffect(state, description.backdropEffect);
            if (!status.ok())
                return status;
            // Portable state is initialized from Description before native creation. The native
            // window itself starts windowed, so make that transition origin explicit.
            state.mode = Types::Mode::Windowed;
            state.fullscreen = {};
            status = setMode(state, description.mode);
            if (!status.ok())
                return status;
            status = applyCursorState(state);
            if (!status.ok())
                return status;

            int showCommand = SW_HIDE;
            if (description.visible)
            {
                showCommand = description.presentation == Types::PresentationState::Minimized   ? SW_SHOWMINIMIZED
                              : description.presentation == Types::PresentationState::Maximized ? SW_SHOWMAXIMIZED
                              : description.requestFocus && description.focusable               ? SW_SHOW
                                                                                                : SW_SHOWNOACTIVATE;
            }
            ShowWindow(state.platform->handle, showCommand);
            state.visible = IsWindowVisible(state.platform->handle) != FALSE;
            state.presentation = IsIconic(state.platform->handle)   ? Types::PresentationState::Minimized
                                 : IsZoomed(state.platform->handle) ? Types::PresentationState::Maximized
                                                                    : Types::PresentationState::Normal;
            if (description.requestFocus && description.focusable && description.visible)
            {
                static_cast<void>(requestFocus(state));
            }
            status = refreshCachedGeometry(state);
            if (!status.ok())
                return status;
            state.suppressEvents = true;
            return IO::successStatus();
        }
        catch (const std::bad_alloc &)
        {
            return IO::makeStatus(IO::Types::ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            return IO::makeStatus(IO::Types::ErrorCode::Unknown);
        }
    }

    CloseResult close(WindowState &state) noexcept
    {
        if (!state.platform)
            return {IO::successStatus(), true};
        if (state.platform->ownerThreadId != GetCurrentThreadId())
            return {IO::makeStatus(IO::Types::ErrorCode::ResourceBusy), false};
        if (Detail::consumeFailure(TestHooks::FailurePoint::Close))
            return {IO::makeStatus(IO::Types::ErrorCode::CloseFailed), false};

        IO::Types::Status status = leaveExclusive(state);
        if (!status.ok())
            return {std::move(status), false};
        if (state.platform->cursorClipApplied && ClipCursor(nullptr) == FALSE)
            return {statusFromWin32(IO::Types::ErrorCode::CloseFailed, GetLastError(), "release cursor confinement"), false};
        state.platform->cursorClipApplied = false;

        HWND handle = state.platform->handle;
        state.platform->destroying = true;
        if (handle != nullptr && DestroyWindow(handle) == FALSE)
        {
            state.platform->destroying = false;
            return {statusFromWin32(IO::Types::ErrorCode::CloseFailed, GetLastError(), "DestroyWindow"), false};
        }

        if (state.platform->largeIcon != nullptr)
            DestroyIcon(state.platform->largeIcon);
        if (state.platform->smallIcon != nullptr && state.platform->smallIcon != state.platform->largeIcon)
            DestroyIcon(state.platform->smallIcon);
        state.platform->largeIcon = nullptr;
        state.platform->smallIcon = nullptr;

        unregisterWindowId(state);
        unregisterOpenState(state);
        IO::Types::Status classStatus = IO::successStatus();
        if (state.platform->classReferenceHeld)
        {
            classStatus = releaseWindowClass();
            state.platform->classReferenceHeld = false;
        }
        state.platform.reset();
        state.nativeDestroyedPendingFinalize = false;
        return {std::move(classStatus), true};
    }

    void closeBestEffort(WindowState &state) noexcept
    {
        if (!state.platform)
            return;
        if (state.platform->ownerThreadId != GetCurrentThreadId())
        {
            // Normal wrong-thread destruction transfers ownership through
            // deferCleanupToOwner(). Reaching this fallback means the owner dispatcher has
            // already exited; its destructor has therefore destroyed the HWND and restored
            // exclusive state. Finish only non-thread-affine bookkeeping.
            if (state.platform->handle != nullptr && IsWindow(state.platform->handle) != FALSE)
                return;
            unregisterWindowId(state);
            if (state.platform->largeIcon != nullptr)
                DestroyIcon(state.platform->largeIcon);
            if (state.platform->smallIcon != nullptr && state.platform->smallIcon != state.platform->largeIcon)
                DestroyIcon(state.platform->smallIcon);
            if (state.platform->classReferenceHeld)
                static_cast<void>(releaseWindowClass());
            state.platform.reset();
            return;
        }

        static_cast<void>(leaveExclusive(state));
        if (state.platform->cursorClipApplied)
            static_cast<void>(ClipCursor(nullptr));
        if (state.platform->handle != nullptr)
        {
            state.platform->destroying = true;
            static_cast<void>(DestroyWindow(state.platform->handle));
        }
        if (state.platform->largeIcon != nullptr)
            DestroyIcon(state.platform->largeIcon);
        if (state.platform->smallIcon != nullptr && state.platform->smallIcon != state.platform->largeIcon)
            DestroyIcon(state.platform->smallIcon);
        unregisterWindowId(state);
        unregisterOpenState(state);
        if (state.platform->classReferenceHeld)
            static_cast<void>(releaseWindowClass());
        state.platform.reset();
    }

    // ------------------------------------------------------------
    // Ownership, wakeup, and native state
    // ------------------------------------------------------------
    bool ownedByCurrentThread(const WindowState &state) noexcept
    {
        return state.platform && state.platform->ownerThreadId == GetCurrentThreadId();
    }

    IO::Types::Status wakeEventWait(const WindowState &state) noexcept
    {
        if (!state.platform)
            return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
        if (PostThreadMessageW(state.platform->ownerThreadId, wakeMessage(), 0, 0) == FALSE)
            return statusFromWin32(IO::Types::ErrorCode::Interrupted, GetLastError(), "PostThreadMessageW wake");
        return IO::successStatus();
    }

    NativeHandleView nativeHandle(const WindowState &state) noexcept
    {
        if (!state.platform)
            return {};
        return {state.platform->instance, state.platform->handle};
    }

    bool hasLiveNativeWindow(const WindowState &state) noexcept
    {
        return state.platform && state.platform->handle != nullptr && IsWindow(state.platform->handle) != FALSE;
    }

} // namespace GameWIP::Window::Detail::Platform

namespace GameWIP::Window::Native::Win32
{
    // ------------------------------------------------------------
    // Native interop
    // ------------------------------------------------------------
    HandleResult getHandle(const GameWIP::Window::Window &window) noexcept
    {
        const Detail::WindowState *state = Detail::WindowAccess::state(window);
        if (state == nullptr || !state->platform)
            return {.status = IO::makeStatus(IO::Types::ErrorCode::NotOpen)};
        if (!Detail::Platform::ownedByCurrentThread(*state))
            return {.status = IO::makeStatus(IO::Types::ErrorCode::ResourceBusy)};
        const Detail::Platform::NativeHandleView handles = Detail::Platform::nativeHandle(*state);
        if (handles.window == nullptr)
            return {.status = IO::makeStatus(IO::Types::ErrorCode::NotOpen)};
        return {.status = IO::successStatus(), .handle = {static_cast<HINSTANCE>(handles.instance), static_cast<HWND>(handles.window)}};
    }
} // namespace GameWIP::Window::Native::Win32

#if WINDOW_INTERNAL_TEST_HOOKS
namespace GameWIP::Window::TestHooks
{
    // ------------------------------------------------------------
    // Validation hooks
    // ------------------------------------------------------------
    Types::Events::PumpResult pumpReentrantly() noexcept
    {
        Detail::Platform::Dispatcher &current = Detail::Platform::dispatcher();
        const bool previous = current.pumping;
        current.pumping = true;
        Types::Events::PumpResult result = Detail::Platform::pumpEvents(std::chrono::milliseconds{0}, false);
        current.pumping = previous;
        return result;
    }

    IO::Types::Status destroyNativeWindow(Window &window) noexcept
    {
        Detail::WindowState *state = Detail::WindowAccess::state(window);
        if (state == nullptr || !state->platform || state->platform->handle == nullptr)
            return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
        if (!Detail::Platform::ownedByCurrentThread(*state))
            return IO::makeStatus(IO::Types::ErrorCode::ResourceBusy);
        if (DestroyWindow(state->platform->handle) == FALSE)
            return Detail::Platform::statusFromWin32(IO::Types::ErrorCode::CloseFailed, GetLastError(), "test-hook unexpected DestroyWindow");
        return IO::successStatus();
    }

    IO::Types::Status simulateFullscreenMonitorRemoval(Window &window) noexcept
    {
        Detail::WindowState *state = Detail::WindowAccess::state(window);
        if (state == nullptr || !state->platform || state->platform->handle == nullptr)
            return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
        if (!Detail::Platform::ownedByCurrentThread(*state))
            return IO::makeStatus(IO::Types::ErrorCode::ResourceBusy);
        if (state->mode == Types::Mode::Windowed)
            return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
        return Detail::Platform::recoverAfterDisplayChange(*state, true);
    }

    DpiTransitionResult calculateDpiTransition(
        Types::LogicalSize logicalSize,
        Types::PixelSize framebufferSize,
        std::uint32_t newDpi,
        Types::DpiResizePolicy policy) noexcept
    {
        if (newDpi == 0)
            return {};
        if (policy == Types::DpiResizePolicy::PreserveLogicalClientSize)
            return {logicalSize, Detail::Platform::logicalToPhysicalSize(logicalSize, newDpi)};
        if (policy == Types::DpiResizePolicy::PreservePhysicalClientSize)
        {
            return {Detail::Platform::physicalToLogicalSize(framebufferSize.width, framebufferSize.height, newDpi), framebufferSize};
        }
        return {};
    }
} // namespace GameWIP::Window::TestHooks
#endif
