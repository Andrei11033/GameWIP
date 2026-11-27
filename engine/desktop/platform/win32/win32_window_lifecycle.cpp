/// @file win32_window_lifecycle.cpp
/// @brief Win32 Window open, close, deferred cleanup, and native-handle access.

#include "desktop/internal/results.h"
#include "desktop/platform/win32/internal/win32_window_backend.h"
#include "desktop/platform/dialogs.h"
#include "desktop/platform/drag_drop.h"
#include "desktop/platform/win32/internal/win32_compat.h"
#include "desktop/platform/win32/internal/win32_unicode.h"

#include "desktop/native/win32.h"

#include <limits>
#include <new>
#include <utility>

namespace GameWIP::Desktop::Platform
{
    /// ------------------------------------------------------------
    /// Native lifecycle
    /// ------------------------------------------------------------

    /// @brief Deletes portable Window backend storage after native cleanup is complete.
    void WindowDataDeleter::operator()(WindowData *data) const noexcept
    {
        delete data;
    }

    /// @brief Creates the native HWND and publishes the initialized Window state.
    GameWIP::Status::Types::Status open(WindowState &state, const Types::Description &description) noexcept
    {
        try
        {
            // Check native DPI requirements and optional capabilities before allocating state.
            const DPI_AWARENESS_CONTEXT context = GetThreadDpiAwarenessContext();

            if(context == nullptr || AreDpiAwarenessContextsEqual(context, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) == FALSE)
            {
                return GameWIP::Desktop::Internal::statusFor(
                    GameWIP::Status::Types::Code::Unsupported,
                    0,
                    "Window requires a Per-Monitor-V2-aware executable manifest");
            }
            const auto capabilitiesResult = getCapabilities();

            if(!capabilitiesResult.ok())
            {
                return capabilitiesResult.status();
            }

            const Types::Capabilities capabilities = capabilitiesResult.value();

            if(description.transparentFramebuffer && !capabilities.supports(Types::Capability::TransparentFramebuffer))
            {
                return GameWIP::Desktop::Internal::statusFor(
                    GameWIP::Status::Types::Code::Unsupported,
                    0,
                    "transparentFramebuffer requires Windows 11 build 26100 or newer");
            }

            if(description.backdropEffect != Types::BackdropEffect::None && !capabilities.supports(Types::Capability::SystemBackdrop))
            {
                return GameWIP::Desktop::Internal::statusFor(
                    GameWIP::Status::Types::Code::Unsupported,
                    0,
                    "system backdrop effects require Windows 11 build 22621 or newer");
            }

            // Allocate owner-thread state and acquire the process-wide Window class.
            auto data = std::unique_ptr<WindowData, WindowDataDeleter>(new WindowData{});
            data->owner = &state;
            data->instance = GetModuleHandleW(nullptr);
            data->ownerThreadId = GetCurrentThreadId();
            data->windowedPlacement.length = sizeof(WINDOWPLACEMENT);

            if(data->instance == nullptr)
            {
                return statusFromWin32(GameWIP::Status::Types::Code::OpenFailed, GetLastError(), "GetModuleHandleW");
            }

            GameWIP::Status::Types::Status status = acquireWindowClass(data->instance);

            if(!status.ok())
            {
                return status;
            }
            data->classReferenceHeld = true;
            state.platform = std::move(data);

            // Validate monitor identities and register the owner dispatcher.
            const std::optional<Types::Display::MonitorId> requestedModeMonitor = std::visit(
                [](const auto &request) -> std::optional<Types::Display::MonitorId>
                {
                    if constexpr(requires { request.targetMonitor; })
                    {
                        return request.targetMonitor;
                    }
                    else
                    {
                        return std::nullopt;
                    }
                },
                description.initialWindowMode);

            if((description.windowed.placement.monitor && nativeMonitor(*description.windowed.placement.monitor) == nullptr) ||
               (requestedModeMonitor && nativeMonitor(*requestedModeMonitor) == nullptr))
            {
                return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::InvalidArgument);
            }

            if(wakeMessage() == 0)
            {
                return statusFromWin32(GameWIP::Status::Types::Code::OpenFailed, wakeMessageError(), "RegisterWindowMessageW wake");
            }

            if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::Dispatcher))
            {
                return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::OpenFailed);
            }
            registerOpenState(state);

            // Prepare title and owner data for native creation.
            DWORD nativeCode = ERROR_SUCCESS;

            if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::TitleConversion))
            {
                return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::EncodingFailed);
            }

            if(!utf8ToUtf16(description.title, state.platform->utf16Scratch, nativeCode))
            {
                return statusFromWin32(
                    unicodeConversionError(nativeCode, GameWIP::Status::Types::Code::InvalidArgument),
                    nativeCode,
                    "convert window title to UTF-16");
            }

            HWND ownerHandle = nullptr;

            if(description.owner)
            {
                WindowState *owner = resolveWindowId(*description.owner);

                if(owner == nullptr || !owner->platform || owner->platform->ownerThreadId != state.platform->ownerThreadId)
                {
                    return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::InvalidArgument);
                }
                ownerHandle = owner->platform->handle;
            }

            // Convert the client size and resolve its initial frame placement.
            const UINT dpi = dpiForWindow(nullptr);
            const Types::PixelSize physicalClient = logicalToPhysicalSize(description.windowed.clientSize, dpi);

            if(physicalClient.width == 0 || physicalClient.height == 0 ||
               physicalClient.width > static_cast<std::uint32_t>(std::numeric_limits<LONG>::max()) ||
               physicalClient.height > static_cast<std::uint32_t>(std::numeric_limits<LONG>::max()))
            {
                return GameWIP::Desktop::Internal::statusFor(
                    GameWIP::Status::Types::Code::InvalidArgument,
                    ERROR_ARITHMETIC_OVERFLOW,
                    "initial client size exceeds Win32 range at the effective DPI");
            }

            RECT outer{0, 0, static_cast<LONG>(physicalClient.width), static_cast<LONG>(physicalClient.height)};
            const DWORD style = styleFor(state);
            const DWORD extendedStyle = extendedStyleFor(state);

            if(AdjustWindowRectExForDpi(&outer, style, FALSE, extendedStyle, dpi) == FALSE)
            {
                return statusFromWin32(GameWIP::Status::Types::Code::OpenFailed, GetLastError(), "AdjustWindowRectExForDpi create");
            }
            const std::int64_t outerWidth = static_cast<std::int64_t>(outer.right) - outer.left;
            const std::int64_t outerHeight = static_cast<std::int64_t>(outer.bottom) - outer.top;

            if(outerWidth <= 0 || outerWidth > std::numeric_limits<int>::max() || outerHeight <= 0 || outerHeight > std::numeric_limits<int>::max())
            {
                return GameWIP::Desktop::Internal::statusFor(
                    GameWIP::Status::Types::Code::InvalidArgument,
                    ERROR_ARITHMETIC_OVERFLOW,
                    "initial outer frame size exceeds Win32 range");
            }

            int x = CW_USEDEFAULT;
            int y = CW_USEDEFAULT;

            if(description.windowed.placement.kind == Types::PlacementKind::Explicit)
            {
                const std::int64_t wideX = static_cast<std::int64_t>(description.windowed.placement.position.x) + outer.left;
                const std::int64_t wideY = static_cast<std::int64_t>(description.windowed.placement.position.y) + outer.top;

                if(wideX < std::numeric_limits<int>::min() || wideX > std::numeric_limits<int>::max() || wideY < std::numeric_limits<int>::min() ||
                   wideY > std::numeric_limits<int>::max())
                {
                    return GameWIP::Desktop::Internal::statusFor(
                        GameWIP::Status::Types::Code::InvalidArgument,
                        ERROR_ARITHMETIC_OVERFLOW,
                        "initial outer frame position exceeds Win32 range");
                }
                x = static_cast<int>(wideX);
                y = static_cast<int>(wideY);
            }
            else if(description.windowed.placement.kind == Types::PlacementKind::Centered)
            {
                HMONITOR monitor = description.windowed.placement.monitor ? nativeMonitor(*description.windowed.placement.monitor)
                                                                          : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
                MONITORINFO info{};
                info.cbSize = sizeof(info);

                if(monitor == nullptr)
                {
                    return statusFromWin32(GameWIP::Status::Types::Code::InvalidArgument, ERROR_NOT_FOUND, "resolve centered monitor");
                }

                if(GetMonitorInfoW(monitor, &info) == FALSE)
                {
                    const DWORD error = GetLastError();
                    return statusFromWin32(GameWIP::Status::Types::Code::InvalidArgument, error, "resolve centered monitor");
                }
                const int width = static_cast<int>(outerWidth);
                const int height = static_cast<int>(outerHeight);
                x = info.rcWork.left + (info.rcWork.right - info.rcWork.left - width) / 2;
                y = info.rcWork.top + (info.rcWork.bottom - info.rcWork.top - height) / 2;
            }

            if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::NativeCreation))
            {
                return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::OpenFailed);
            }

            // Create the HWND, then publish its handle and stable Window identity.
            HWND handle = CreateWindowExW(
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

            if(handle == nullptr)
            {
                return statusFromWin32(GameWIP::Status::Types::Code::OpenFailed, GetLastError(), "CreateWindowExW");
            }
            // CreateWindowExW dispatches synchronous construction messages before returning.
            // Publish only its returned HWND; windowProc keeps runtime-only work out of that interval.
            state.platform->handle = handle;
            state.platform->lifecycle = NativeWindowLifecycle::Published;

            registerWindowId(state);

            if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::PartialOpen))
            {
                return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::NativeFailure);
            }
            state.platform->windowedStyle = style;
            state.platform->windowedExtendedStyle = extendedStyle;
            state.platform->cursor = loadCursor(state.cursorShape);

            if(state.platform->cursor == nullptr)
            {
                return statusFromWin32(GameWIP::Status::Types::Code::OpenFailed, GetLastError(), "LoadCursorW");
            }

            status = refreshCachedGeometry(state);

            if(!status.ok())
            {
                return status;
            }
            updateCurrentMonitor(state);

            // Construction uses a provisional system DPI. Restore the requested logical
            // rectangle at the HWND's effective DPI before fullscreen transitions or showing it.
            state.mode = Types::WindowMode::Windowed;
            state.fullscreen = {};
            status = description.windowed.placement.kind == Types::PlacementKind::Explicit
                         ? setClientRect(state, description.windowed.placement.position, description.windowed.clientSize)
                         : setClientSize(state, description.windowed.clientSize);

            if(!status.ok())
            {
                return status;
            }

            if(description.windowed.placement.kind == Types::PlacementKind::Centered)
            {
                status = centerOn(state, description.windowed.placement.monitor.value_or(Types::Display::MonitorId{}));

                if(!status.ok())
                {
                    return status;
                }
            }

            // Apply requested shell, display, and cursor policies before showing the Window.
            if(state.transparentFramebuffer)
            {
                const BOOL enabled = TRUE;
                const HRESULT result = DwmSetWindowAttribute(state.platform->handle, Compat::kRedirectionBitmapAlpha, &enabled, sizeof(enabled));

                if(FAILED(result))
                {
                    return GameWIP::Desktop::Internal::statusFor(
                        GameWIP::Status::Types::Code::Unsupported,
                        result,
                        GameWIP::Desktop::Internal::NativeCodeDomain::HResult);
                }
            }

            status = setFileDropEnabled(state, description.fileDropEnabled);

            if(!status.ok())
            {
                return status;
            }

            status = setOpacity(state, description.opacity);

            if(!status.ok())
            {
                return status;
            }

            status = setBackdropEffect(state, description.backdropEffect);

            if(!status.ok())
            {
                return status;
            }

            status = setWindowMode(state, description.initialWindowMode);

            if(!status.ok())
            {
                return status;
            }

            status = applyCursorState(state);

            if(!status.ok())
            {
                return status;
            }

            // Show the initialized Window and publish its initial state.
            int showCommand = SW_HIDE;

            if(description.activation.visible)
            {
                showCommand = description.presentation == Types::PresentationState::Minimized           ? SW_SHOWMINIMIZED
                              : description.presentation == Types::PresentationState::Maximized         ? SW_SHOWMAXIMIZED
                              : description.activation.requestFocus && description.activation.focusable ? SW_SHOW
                                                                                                        : SW_SHOWNOACTIVATE;
            }
            ShowWindow(state.platform->handle, showCommand);
            state.visible = IsWindowVisible(state.platform->handle) != FALSE;
            state.presentation = IsIconic(state.platform->handle)   ? Types::PresentationState::Minimized
                                 : IsZoomed(state.platform->handle) ? Types::PresentationState::Maximized
                                                                    : Types::PresentationState::Normal;

            if(state.presentationPublication != nullptr)
            {
                state.presentationPublication->publishVisible(state.visible);
                state.presentationPublication->publishPresentationState(state.presentation);
            }

            if(description.activation.requestFocus && description.activation.focusable && description.activation.visible)
            {
                static_cast<void>(requestFocus(state));
            }
            status = refreshCachedGeometry(state);

            if(!status.ok())
            {
                return status;
            }
            state.suppressEvents = true;
            return GameWIP::Status::Types::Status{};
        }
        catch(const std::bad_alloc &)
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::OutOfMemory);
        }
        catch(...)
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::Unknown);
        }
    }

    /// @brief Closes a Window on its owner thread and reports whether native ownership ended.
    CloseResult close(WindowState &state) noexcept
    {
        // Validate ownership and restore the desktop display mode first.
        if(!state.platform)
        {
            return Internal::result(GameWIP::Status::Types::Status{}, true);
        }

        if(state.platform->ownerThreadId != GetCurrentThreadId())
        {
            return Internal::result(GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::ResourceBusy), false);
        }

        if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::Close))
        {
            return Internal::result(GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::CloseFailed), false);
        }

        GameWIP::Status::Types::Status status = leaveExclusive(state);

        if(!status.ok())
        {
            return Internal::result(std::move(status), false);
        }

        // Release cursor confinement before notifying dependent native resources.
        if(state.platform->cursorClipApplied && ClipCursor(nullptr) == FALSE)
        {
            return Internal::result(statusFromWin32(GameWIP::Status::Types::Code::CloseFailed, GetLastError(), "release cursor confinement"), false);
        }
        state.platform->cursorClipApplied = false;

        HWND handle = state.platform->handle;
        state.platform->destroying = true;
        status = notifyProgressOwnerLoss(state);

        if(!status.ok())
        {
            state.platform->destroying = false;

            if(state.platform->handle != nullptr && IsWindow(state.platform->handle) != FALSE && !windowHasBlockingProgressDialog(state))
            {
                EnableWindow(state.platform->handle, state.interactionEnabled ? TRUE : FALSE);

                if(IsWindowEnabled(state.platform->handle) != (state.interactionEnabled ? TRUE : FALSE))
                {
                    return Internal::result(
                        statusFromWin32(GameWIP::Status::Types::Code::NativeFailure, ERROR_FUNCTION_FAILED, "EnableWindow close rollback"),
                        false);
                }
            }

            return Internal::result(std::move(status), false);
        }

        if(!windowClosingDragDrop(state))
        {
            state.platform->destroying = false;

            if(state.platform->handle != nullptr && IsWindow(state.platform->handle) != FALSE && !windowHasBlockingProgressDialog(state))
            {
                EnableWindow(state.platform->handle, state.interactionEnabled ? TRUE : FALSE);

                if(IsWindowEnabled(state.platform->handle) != (state.interactionEnabled ? TRUE : FALSE))
                {
                    return Internal::result(
                        statusFromWin32(GameWIP::Status::Types::Code::NativeFailure, ERROR_FUNCTION_FAILED, "EnableWindow close rollback"),
                        false);
                }
            }

            return Internal::result(GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::CloseFailed), false);
        }

        if(handle != nullptr && DestroyWindow(handle) == FALSE)
        {
            const DWORD nativeCode = GetLastError();
            state.platform->destroying = false;

            if(state.platform->handle != nullptr && IsWindow(state.platform->handle) != FALSE && !windowHasBlockingProgressDialog(state))
            {
                EnableWindow(state.platform->handle, state.interactionEnabled ? TRUE : FALSE);

                if(IsWindowEnabled(state.platform->handle) != (state.interactionEnabled ? TRUE : FALSE))
                {
                    return Internal::result(
                        statusFromWin32(GameWIP::Status::Types::Code::NativeFailure, ERROR_FUNCTION_FAILED, "EnableWindow close rollback"),
                        false);
                }
            }

            return Internal::result(statusFromWin32(GameWIP::Status::Types::Code::CloseFailed, nativeCode, "DestroyWindow"), false);
        }

        // Release owned icons, registry entries, and the native class reference.
        if(state.platform->largeIcon != nullptr)
        {
            DestroyIcon(state.platform->largeIcon);
        }

        if(state.platform->smallIcon != nullptr && state.platform->smallIcon != state.platform->largeIcon)
        {
            DestroyIcon(state.platform->smallIcon);
        }
        state.platform->largeIcon = nullptr;
        state.platform->smallIcon = nullptr;

        unregisterWindowId(state);
        unregisterOpenState(state);
        GameWIP::Status::Types::Status classStatus = GameWIP::Status::Types::Status{};

        if(state.platform->classReferenceHeld)
        {
            classStatus = releaseWindowClass();
            state.platform->classReferenceHeld = false;
        }
        state.platform.reset();
        state.nativeDestroyedPendingFinalize = false;
        return Internal::result(std::move(classStatus), true);
    }

    /// @brief Performs best-effort Window teardown for normal and dispatcher-exit paths.
    void closeBestEffort(WindowState &state) noexcept
    {
        // Finish bookkeeping directly if the owner dispatcher has already exited.
        if(!state.platform)
        {
            return;
        }

        if(state.platform->ownerThreadId != GetCurrentThreadId())
        {
            // Normal wrong-thread destruction transfers ownership through
            // deferCleanupToOwner(). Reaching this fallback means the owner dispatcher has
            // already exited; its destructor has therefore destroyed the HWND and restored
            // exclusive state. Finish only non-thread-affine bookkeeping.
            if(state.platform->handle != nullptr && IsWindow(state.platform->handle) != FALSE)
            {
                return;
            }
            unregisterWindowId(state);

            if(state.platform->largeIcon != nullptr)
            {
                DestroyIcon(state.platform->largeIcon);
            }

            if(state.platform->smallIcon != nullptr && state.platform->smallIcon != state.platform->largeIcon)
            {
                DestroyIcon(state.platform->smallIcon);
            }

            if(state.platform->classReferenceHeld)
            {
                static_cast<void>(releaseWindowClass());
            }
            state.platform.reset();
            return;
        }

        // Tear down owner-thread resources without interrupting dispatcher shutdown.
        static_cast<void>(leaveExclusive(state));

        if(state.platform->cursorClipApplied)
        {
            static_cast<void>(ClipCursor(nullptr));
        }
        state.platform->destroying = true;
        notifyProgressOwnerLossBestEffort(state);

        if(state.platform->handle != nullptr)
        {
            static_cast<void>(windowClosingDragDrop(state));
            static_cast<void>(DestroyWindow(state.platform->handle));
        }

        if(state.platform->largeIcon != nullptr)
        {
            DestroyIcon(state.platform->largeIcon);
        }

        if(state.platform->smallIcon != nullptr && state.platform->smallIcon != state.platform->largeIcon)
        {
            DestroyIcon(state.platform->smallIcon);
        }
        unregisterWindowId(state);
        unregisterOpenState(state);

        if(state.platform->classReferenceHeld)
        {
            static_cast<void>(releaseWindowClass());
        }
        state.platform.reset();
    }

    /// ------------------------------------------------------------
    /// Ownership, wakeup, and native state
    /// ------------------------------------------------------------

    /// @brief Tests whether the current thread owns the Window's native resources.
    bool ownedByCurrentThread(const WindowState &state) noexcept
    {
        return state.platform && state.platform->ownerThreadId == GetCurrentThreadId();
    }

    /// @brief Wakes the owner thread so a pending event wait can observe cross-thread work.
    GameWIP::Status::Types::Status wakeEventWait(const WindowState &state) noexcept
    {
        if(!state.platform)
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::NotOpen);
        }
        return postWakeMessage(state.platform->ownerThreadId, "PostThreadMessageW wake");
    }

    /// @brief Returns opaque native handles for owner-thread interop.
    NativeHandleView nativeHandle(const WindowState &state) noexcept
    {
        if(!state.platform)
        {
            return {};
        }
        return {state.platform->instance, state.platform->handle};
    }

    /// @brief Tests whether the Window still has a live HWND.
    bool hasLiveNativeWindow(const WindowState &state) noexcept
    {
        return state.platform && state.platform->handle != nullptr && IsWindow(state.platform->handle) != FALSE;
    }
} // namespace GameWIP::Desktop::Platform

namespace GameWIP::Desktop::Native::Win32
{
    /// ------------------------------------------------------------
    /// Native interop
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Result<HandleView> getHandle(const GameWIP::Desktop::Window &window) noexcept
    {
        const Internal::WindowState *state = Internal::WindowAccess::state(window);

        if(state == nullptr || !state->platform)
        {
            return GameWIP::Status::Types::Result<HandleView>::failure(GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::NotOpen));
        }

        if(!Platform::ownedByCurrentThread(*state))
        {
            return GameWIP::Status::Types::Result<HandleView>::failure(
                GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::ResourceBusy));
        }
        const Platform::NativeHandleView handles = Platform::nativeHandle(*state);

        if(handles.window == nullptr)
        {
            return GameWIP::Status::Types::Result<HandleView>::failure(GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::NotOpen));
        }

        return GameWIP::Status::Types::Result<HandleView>::success(
            HandleView{static_cast<HINSTANCE>(handles.instance), static_cast<HWND>(handles.window)});
    }

    GameWIP::Status::Types::Result<void> attachMessageSink(GameWIP::Desktop::Window &window, MessageSink sink, void *context) noexcept
    {
        if(sink == nullptr)
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::InvalidArgument);
        }

        Internal::WindowState *state = Internal::WindowAccess::state(window);

        if(state == nullptr || !state->platform || state->platform->handle == nullptr)
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::NotOpen);
        }

        if(!Platform::ownedByCurrentThread(*state))
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::ResourceBusy);
        }

        if(state->platform->messageSink != nullptr)
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::AlreadyOpen);
        }
        state->platform->messageSink = sink;
        state->platform->messageSinkContext = context;
        return GameWIP::Status::Types::Status{};
    }

    GameWIP::Status::Types::Result<void> detachMessageSink(GameWIP::Desktop::Window &window) noexcept
    {
        Internal::WindowState *state = Internal::WindowAccess::state(window);

        if(state == nullptr || !state->platform || state->platform->handle == nullptr)
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::NotOpen);
        }

        if(!Platform::ownedByCurrentThread(*state))
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::ResourceBusy);
        }
        state->platform->messageSink = nullptr;
        state->platform->messageSinkContext = nullptr;
        return GameWIP::Status::Types::Status{};
    }
} // namespace GameWIP::Desktop::Native::Win32

#if DESKTOP_INTERNAL_TEST_HOOKS
namespace GameWIP::Desktop::Internal::TestHooks
{
    /// ------------------------------------------------------------
    /// Validation hooks
    /// ------------------------------------------------------------

    Types::Events::PumpResult pumpReentrantly() noexcept
    {
        Platform::Dispatcher &current = Platform::dispatcher();
        const bool previous = current.pumping;
        current.pumping = true;
        Types::Events::PumpResult result = Platform::pumpEvents(std::chrono::milliseconds{0}, false);
        current.pumping = previous;
        return result;
    }

    GameWIP::Status::Types::Status destroyNativeWindow(Window &window) noexcept
    {
        Internal::WindowState *state = Internal::WindowAccess::state(window);

        if(state == nullptr || !state->platform || state->platform->handle == nullptr)
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::NotOpen);
        }

        if(!Platform::ownedByCurrentThread(*state))
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::ResourceBusy);
        }

        if(DestroyWindow(state->platform->handle) == FALSE)
        {
            return Platform::statusFromWin32(GameWIP::Status::Types::Code::CloseFailed, GetLastError(), "test-hook unexpected DestroyWindow");
        }

        return GameWIP::Status::Types::Status{};
    }

    GameWIP::Status::Types::Status simulateFullscreenMonitorRemoval(Window &window) noexcept
    {
        Internal::WindowState *state = Internal::WindowAccess::state(window);

        if(state == nullptr || !state->platform || state->platform->handle == nullptr)
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::NotOpen);
        }

        if(!Platform::ownedByCurrentThread(*state))
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::ResourceBusy);
        }

        if(state->mode == Types::WindowMode::Windowed)
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::InvalidArgument);
        }
        return Platform::recoverAfterDisplayChange(*state, true);
    }

    DpiTransitionResult calculateDpiTransition(
        Types::LogicalSize logicalSize,
        Types::PixelSize framebufferSize,
        std::uint32_t newDpi,
        Types::DpiResizePolicy policy) noexcept
    {
        if(newDpi == 0)
        {
            return {};
        }

        if(policy == Types::DpiResizePolicy::PreserveLogicalClientSize)
        {
            return {logicalSize, Platform::logicalToPhysicalSize(logicalSize, newDpi)};
        }

        if(policy == Types::DpiResizePolicy::PreservePhysicalClientSize)
        {
            return {Platform::physicalToLogicalSize(framebufferSize.width, framebufferSize.height, newDpi), framebufferSize};
        }

        return {};
    }
} // namespace GameWIP::Desktop::Internal::TestHooks
#endif
