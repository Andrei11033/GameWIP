/// @file win32_child_surface.cpp
/// @brief Win32 ChildSurface lifecycle, DPI, geometry, ordering, and native interop.

#include "desktop/internal/results.h"
#include "desktop/platform/win32/internal/win32_window_backend.h"

#include "desktop/child_surface.h"
#include "desktop/platform/child_surface.h"
#include "desktop/native/win32.h"

#include <algorithm>
#include <limits>
#include <mutex>
#include <utility>

namespace GameWIP::Desktop::Platform
{
    /// ------------------------------------------------------------
    /// ChildSurface class and geometry helpers
    /// ------------------------------------------------------------

    namespace
    {
        inline constexpr wchar_t kChildSurfaceClassName[] = L"GameWIP.Window.ChildSurface";
        std::mutex childClassMutex;
        std::size_t childClassUsers = 0;
        HINSTANCE childClassInstance = nullptr;
        bool childClassOwned = false;

        [[nodiscard]] GameWIP::Status::Types::Status acquireChildSurfaceClass(HINSTANCE instance) noexcept;
        [[nodiscard]] GameWIP::Status::Types::Status releaseChildSurfaceClass() noexcept;
        [[nodiscard]] LRESULT CALLBACK childSurfaceProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

        /// @brief Temporarily enables mixed-DPI hosting for a child HWND operation.
        class DpiHostingScope final
        {
        public:
            DpiHostingScope() noexcept
                : previous_(SetThreadDpiHostingBehavior(DPI_HOSTING_BEHAVIOR_MIXED))
            {
            }

            ~DpiHostingScope() noexcept
            {
                if(previous_ != DPI_HOSTING_BEHAVIOR_INVALID)
                {
                    static_cast<void>(SetThreadDpiHostingBehavior(previous_));
                }
            }

            [[nodiscard]] bool applied() const noexcept
            {
                return previous_ != DPI_HOSTING_BEHAVIOR_INVALID;
            }

        private:
            DPI_HOSTING_BEHAVIOR previous_ = DPI_HOSTING_BEHAVIOR_INVALID;
        };

        /// @brief Converts logical child geometry to checked Win32 pixel coordinates.
        [[nodiscard]] bool physicalRect(const Types::LogicalRect &rect, UINT dpi, int &x, int &y, int &width, int &height) noexcept
        {
            LONG nativeX = 0;
            LONG nativeY = 0;

            if(!logicalToPhysicalChecked(rect.position.x, dpi, nativeX) || !logicalToPhysicalChecked(rect.position.y, dpi, nativeY))
            {
                return false;
            }
            const Types::PixelSize size = logicalToPhysicalSize(rect.size, dpi);

            if(size.width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
               size.height > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
            {
                return false;
            }

            x = nativeX;
            y = nativeY;
            width = static_cast<int>(size.width);
            height = static_cast<int>(size.height);
            return true;
        }

        /// @brief Refreshes cached virtual-screen geometry from the live child HWND.
        void refreshChildScreenRect(ChildSurfaceState &state) noexcept
        {
            if(!state.platform || state.platform->handle == nullptr)
            {
                return;
            }

            RECT native{};

            if(GetWindowRect(state.platform->handle, &native) == FALSE)
            {
                return;
            }
            state.screenRect = {
                {native.left, native.top},
                {static_cast<std::uint32_t>(std::max<LONG>(0, native.right - native.left)),
                 static_cast<std::uint32_t>(std::max<LONG>(0, native.bottom - native.top))}};
        }

        /// @brief Reads visibility while preserving the distinction between a zero style and API failure.
        [[nodiscard]] GameWIP::Status::Types::Status childVisible(HWND window, bool &visible) noexcept
        {
            LONG_PTR style = 0;
            const GameWIP::Status::Types::Status status = queryWindowLong(window, GWL_STYLE, style, "GetWindowLongPtrW ChildSurface visibility");

            if(!status.ok())
            {
                return status;
            }

            visible = (static_cast<DWORD>(style) & WS_VISIBLE) != 0;
            return GameWIP::Status::Types::Status{};
        }

        /// @brief Acquires the process-local child class registration with reference counting.
        [[nodiscard]] GameWIP::Status::Types::Status acquireChildSurfaceClass(HINSTANCE instance) noexcept
        {
            std::scoped_lock lock(childClassMutex);

            if(childClassUsers != 0)
            {
                ++childClassUsers;
                return GameWIP::Status::Types::Status{};
            }

            WNDCLASSEXW nativeClass{};
            nativeClass.cbSize = sizeof(nativeClass);
            nativeClass.style = CS_DBLCLKS;
            nativeClass.lpfnWndProc = childSurfaceProc;
            nativeClass.hInstance = instance;
            nativeClass.lpszClassName = kChildSurfaceClassName;

            if(RegisterClassExW(&nativeClass) == 0)
            {
                const DWORD code = GetLastError();

                if(code != ERROR_CLASS_ALREADY_EXISTS)
                {
                    return statusFromWin32(GameWIP::Status::Types::Code::OpenFailed, code, "register ChildSurface class");
                }
                childClassOwned = false;
            }
            else
            {
                childClassOwned = true;
            }
            childClassInstance = instance;
            childClassUsers = 1;
            return GameWIP::Status::Types::Status{};
        }

        /// @brief Releases the child class only after its last native user has closed.
        [[nodiscard]] GameWIP::Status::Types::Status releaseChildSurfaceClass() noexcept
        {
            std::scoped_lock lock(childClassMutex);

            if(childClassUsers == 0)
            {
                return GameWIP::Status::Types::Status{};
            }
            --childClassUsers;

            if(childClassUsers != 0 || !childClassOwned)
            {
                return GameWIP::Status::Types::Status{};
            }

            if(UnregisterClassW(kChildSurfaceClassName, childClassInstance) == FALSE)
            {
                ++childClassUsers;
                return statusFromWin32(GameWIP::Status::Types::Code::CloseFailed, GetLastError(), "unregister ChildSurface class");
            }
            childClassInstance = nullptr;
            childClassOwned = false;
            return GameWIP::Status::Types::Status{};
        }

        /// @brief Translates child HWND lifecycle and DPI messages into portable state/events.
        [[nodiscard]] LRESULT CALLBACK childSurfaceProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
        {
            auto *state = reinterpret_cast<ChildSurfaceState *>(GetWindowLongPtrW(window, GWLP_USERDATA));

            if(message == WM_NCCREATE)
            {
                const auto *create = reinterpret_cast<const CREATESTRUCTW *>(lParam);
                state = static_cast<ChildSurfaceState *>(create->lpCreateParams);

                if(state == nullptr)
                {
                    return FALSE;
                }
                SetLastError(ERROR_SUCCESS);
                const LONG_PTR previous = SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
                const DWORD error = GetLastError();

                if(previous == 0 && error != ERROR_SUCCESS)
                {
                    return FALSE;
                }
            }

            if(state == nullptr)
            {
                return DefWindowProcW(window, message, wParam, lParam);
            }

            switch(message)
            {
            case WM_ERASEBKGND:
                return 1;
            case WM_DPICHANGED_BEFOREPARENT:
                return 0;
            case WM_DPICHANGED_AFTERPARENT:
            {
                const UINT newDpi = dpiForWindow(window);
                const auto oldDpiValue = static_cast<UINT>(state->dpi.x);

                if(newDpi == 0 || newDpi == oldDpiValue)
                {
                    return 0;
                }

                // Compute the new host geometry before publishing the DPI change.
                const Types::ContentScale previousScale = state->contentScale;
                const Types::Dpi previousDpi = state->dpi;
                const Types::PixelSize previousPixels = state->pixelSize;
                int x = 0;
                int y = 0;
                int width = 0;
                int height = 0;

                if(!physicalRect(state->rect, newDpi, x, y, width, height))
                {
                    recordPumpFailure(GameWIP::Desktop::Internal::statusFor(
                        GameWIP::Status::Types::Code::NativeFailure,
                        ERROR_ARITHMETIC_OVERFLOW,
                        GameWIP::Desktop::Internal::NativeCodeDomain::Win32Error,
                        "scale ChildSurface geometry"));
                    return 0;
                }

                if(SetWindowPos(window, nullptr, x, y, width, height, SWP_NOACTIVATE | SWP_NOZORDER) == FALSE)
                {
                    recordPumpFailure(statusFromWin32(GameWIP::Status::Types::Code::NativeFailure, GetLastError(), "apply ChildSurface DPI geometry"));
                    return 0;
                }

                state->dpi = {static_cast<float>(newDpi), static_cast<float>(newDpi)};
                state->contentScale = {
                    static_cast<float>(newDpi) / static_cast<float>(kBaselineDpi),
                    static_cast<float>(newDpi) / static_cast<float>(kBaselineDpi)};
                state->pixelSize = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
                refreshChildScreenRect(*state);

                // Publish scale and pixel-size changes after the native resize succeeds.
                routeChildSurfaceEvent(
                    *state,
                    Types::ChildSurface::Events::ContentScaleChanged{previousScale, state->contentScale, previousDpi, state->dpi, state->pixelSize});

                if(previousPixels != state->pixelSize)
                {
                    routeChildSurfaceEvent(*state, Types::ChildSurface::Events::PixelSizeChanged{state->pixelSize});
                }

                return 0;
            }

            case WM_NCDESTROY:
            {
                const bool unexpected = !state->platform || !state->platform->destroying;
                SetWindowLongPtrW(window, GWLP_USERDATA, 0);

                if(state->platform)
                {
                    state->platform->handle = nullptr;

                    if(state->platform->registered)
                    {
                        unregisterOpenChildSurface(*state);
                        state->platform->registered = false;
                    }
                }

                if(unexpected)
                {
                    state->nativeDestroyedPendingFinalize = true;
                    routeChildSurfaceEvent(*state, Types::ChildSurface::Events::NativeDestroyed{});
                }

                return DefWindowProcW(window, message, wParam, lParam);
            }

            default:
                return DefWindowProcW(window, message, wParam, lParam);
            }
        }
    } // namespace

    /// ------------------------------------------------------------
    /// ChildSurface lifecycle and native operations
    /// ------------------------------------------------------------

    /// @brief Deletes backend state after owner-thread native cleanup has completed.
    void ChildSurfaceDataDeleter::operator()(ChildSurfaceData *data) const noexcept
    {
        delete data;
    }

    /// @brief Refreshes cached child geometry after a parent or DPI transition.
    void refreshChildSurfaceScreenRect(ChildSurfaceState &state) noexcept
    {
        refreshChildScreenRect(state);
    }

    /// @brief Creates and publishes a child HWND beneath a live parent Window.
    GameWIP::Status::Types::Status openChildSurface(ChildSurfaceState &state, WindowState &parent) noexcept
    {
        try
        {
            // Validate the parent and register shared child-class state.
            if(!parent.platform || parent.platform->handle == nullptr)
            {
                return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::NotOpen);
            }
            auto data = std::unique_ptr<ChildSurfaceData, ChildSurfaceDataDeleter>(new ChildSurfaceData{});
            data->owner = &state;
            data->instance = parent.platform->instance;
            data->ownerThreadId = parent.platform->ownerThreadId;
            GameWIP::Status::Types::Status status = acquireChildSurfaceClass(data->instance);

            if(!status.ok())
            {
                return status;
            }
            data->classReferenceHeld = true;
            state.platform = std::move(data);
            registerOpenChildSurface(state);
            state.platform->registered = true;

            // Convert the requested logical rectangle at the parent's effective DPI.
            const UINT dpi = dpiForWindow(parent.platform->handle);
            int x = 0;
            int y = 0;
            int width = 0;
            int height = 0;

            if(!physicalRect(state.rect, dpi, x, y, width, height))
            {
                return GameWIP::Desktop::Internal::statusFor(
                    GameWIP::Status::Types::Code::InvalidArgument,
                    ERROR_ARITHMETIC_OVERFLOW,
                    GameWIP::Desktop::Internal::NativeCodeDomain::Win32Error,
                    "ChildSurface rect exceeds Win32 range");
            }

            if(Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::NativeCreation))
            {
                return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::OpenFailed);
            }
            DWORD creationError = ERROR_SUCCESS;

            // Create the HWND while mixed-DPI child hosting is active.
            {
                DpiHostingScope hosting;

                if(!hosting.applied())
                {
                    return statusFromWin32(GameWIP::Status::Types::Code::OpenFailed, GetLastError(), "enable mixed DPI hosting for ChildSurface");
                }

                state.platform->handle = CreateWindowExW(
                    WS_EX_NOPARENTNOTIFY,
                    kChildSurfaceClassName,
                    L"",
                    WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                    x,
                    y,
                    width,
                    height,
                    parent.platform->handle,
                    nullptr,
                    state.platform->instance,
                    &state);

                if(state.platform->handle == nullptr)
                {
                    creationError = GetLastError();
                }
            }

            if(state.platform->handle == nullptr)
            {
                return statusFromWin32(
                    GameWIP::Status::Types::Code::OpenFailed,
                    creationError == ERROR_SUCCESS ? ERROR_FUNCTION_FAILED : creationError,
                    "create ChildSurface host");
            }

            // Publish the initial scale, interaction, and visibility state.
            state.dpi = {static_cast<float>(dpi), static_cast<float>(dpi)};
            state.contentScale = {
                static_cast<float>(dpi) / static_cast<float>(kBaselineDpi),
                static_cast<float>(dpi) / static_cast<float>(kBaselineDpi)};
            state.pixelSize = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
            static_cast<void>(EnableWindow(state.platform->handle, state.interactionEnabled ? TRUE : FALSE));

            if((IsWindowEnabled(state.platform->handle) != FALSE) != state.interactionEnabled)
            {
                return statusFromWin32(GameWIP::Status::Types::Code::OpenFailed, ERROR_FUNCTION_FAILED, "set initial ChildSurface interaction");
            }
            ShowWindow(state.platform->handle, state.visible ? SW_SHOWNOACTIVATE : SW_HIDE);
            bool visible = false;
            GameWIP::Status::Types::Status visibilityStatus = childVisible(state.platform->handle, visible);

            if(!visibilityStatus.ok())
            {
                return visibilityStatus;
            }
            state.visible = visible;
            state.interactionEnabled = IsWindowEnabled(state.platform->handle) != FALSE;
            refreshChildScreenRect(state);
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

    /// @brief Closes one child HWND and releases its class reference on the owner thread.
    CloseResult closeChildSurface(ChildSurfaceState &state) noexcept
    {
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

        // Destroy the HWND before unregistering state or releasing its class reference.
        if(state.platform->handle != nullptr)
        {
            state.platform->destroying = true;

            if(DestroyWindow(state.platform->handle) == FALSE)
            {
                state.platform->destroying = false;
                return Internal::result(statusFromWin32(GameWIP::Status::Types::Code::CloseFailed, GetLastError(), "destroy ChildSurface host"), false);
            }
        }

        if(state.platform->registered)
        {
            unregisterOpenChildSurface(state);
            state.platform->registered = false;
        }

        // Release the shared class registration after its final native user closes.
        GameWIP::Status::Types::Status status = GameWIP::Status::Types::Status{};

        if(state.platform->classReferenceHeld)
        {
            status = releaseChildSurfaceClass();
            state.platform->classReferenceHeld = false;
        }
        state.platform.reset();
        state.nativeDestroyedPendingFinalize = false;
        return Internal::result(std::move(status), true);
    }

    /// @brief Performs non-reporting child cleanup used by destructors and failed opens.
    void closeChildSurfaceBestEffort(ChildSurfaceState &state) noexcept
    {
        if(!state.platform)
        {
            return;
        }

        if(state.platform->ownerThreadId != GetCurrentThreadId())
        {
            if(state.platform->handle != nullptr && IsWindow(state.platform->handle) != FALSE)
            {
                return;
            }

            if(state.platform->classReferenceHeld)
            {
                static_cast<void>(releaseChildSurfaceClass());
            }
            state.platform.reset();
            return;
        }

        if(state.platform->handle != nullptr)
        {
            state.platform->destroying = true;
            static_cast<void>(DestroyWindow(state.platform->handle));
        }

        if(state.platform->registered)
        {
            unregisterOpenChildSurface(state);
        }

        if(state.platform->classReferenceHeld)
        {
            static_cast<void>(releaseChildSurfaceClass());
        }
        state.platform.reset();
    }

    /// @brief Returns whether the calling thread matches the native child owner.
    bool isChildSurfaceOwnedByCurrentThread(const ChildSurfaceState &state) noexcept
    {
        return state.platform && state.platform->ownerThreadId == GetCurrentThreadId();
    }

    /// @brief Returns whether the state still references a live child HWND.
    bool hasLiveNativeChildSurface(const ChildSurfaceState &state) noexcept
    {
        return state.platform && state.platform->handle != nullptr && IsWindow(state.platform->handle) != FALSE;
    }

    /// @brief Returns the native instance/window pair for a live child surface.
    NativeHandleView childSurfaceNativeHandle(const ChildSurfaceState &state) noexcept
    {
        return state.platform ? NativeHandleView{state.platform->instance, state.platform->handle} : NativeHandleView{};
    }

    /// @brief Applies checked logical child geometry without activating or reordering the HWND.
    GameWIP::Status::Types::Status setChildSurfaceRect(ChildSurfaceState &state, Types::LogicalRect rect) noexcept
    {
        if(!state.platform || state.platform->handle == nullptr)
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::NotOpen);
        }

        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        const UINT dpi = dpiForWindow(state.platform->handle);

        if(!physicalRect(rect, dpi, x, y, width, height))
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::InvalidArgument);
        }

        if(SetWindowPos(state.platform->handle, nullptr, x, y, width, height, SWP_NOACTIVATE | SWP_NOZORDER) == FALSE)
        {
            return statusFromWin32(GameWIP::Status::Types::Code::NativeFailure, GetLastError(), "set ChildSurface rect");
        }
        refreshChildScreenRect(state);
        return GameWIP::Status::Types::Status{};
    }

    /// @brief Converts child-local logical coordinates to virtual-screen pixels.
    GameWIP::Status::Types::Result<Types::ScreenPosition> childSurfaceClientToScreen(const ChildSurfaceState &state, Types::LogicalPosition position) noexcept
    {
        LONG x = 0;
        LONG y = 0;
        const UINT dpi = dpiForWindow(state.platform->handle);

        if(!logicalToPhysicalChecked(position.x, dpi, x) || !logicalToPhysicalChecked(position.y, dpi, y))
        {
            return GameWIP::Status::Types::Result<Types::ScreenPosition>::failure(
                GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::InvalidArgument));
        }

        POINT point{x, y};

        if(ClientToScreen(state.platform->handle, &point) == FALSE)
        {
            return GameWIP::Status::Types::Result<Types::ScreenPosition>::failure(
                statusFromWin32(GameWIP::Status::Types::Code::NativeFailure, GetLastError(), "ChildSurface client to screen"));
        }

        return GameWIP::Status::Types::Result<Types::ScreenPosition>::success(Types::ScreenPosition{point.x, point.y});
    }

    /// @brief Converts virtual-screen pixels to child-local logical coordinates.
    GameWIP::Status::Types::Result<Types::LogicalPosition> childSurfaceScreenToClient(const ChildSurfaceState &state, Types::ScreenPosition position) noexcept
    {
        POINT point{position.x, position.y};

        if(ScreenToClient(state.platform->handle, &point) == FALSE)
        {
            return GameWIP::Status::Types::Result<Types::LogicalPosition>::failure(
                statusFromWin32(GameWIP::Status::Types::Code::NativeFailure, GetLastError(), "ChildSurface screen to client"));
        }
        const UINT dpi = dpiForWindow(state.platform->handle);
        return GameWIP::Status::Types::Result<Types::LogicalPosition>::success(
            Types::LogicalPosition{physicalToLogical(point.x, dpi), physicalToLogical(point.y, dpi)});
    }

    /// @brief Changes native child visibility and verifies the resulting style bit.
    GameWIP::Status::Types::Status showChildSurface(ChildSurfaceState &state, bool visible) noexcept
    {
        const bool previousVisible = state.visible;
        ShowWindow(state.platform->handle, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
        bool actualVisible = false;
        GameWIP::Status::Types::Status visibilityStatus = childVisible(state.platform->handle, actualVisible);

        if(!visibilityStatus.ok())
        {
            // Keep native visibility aligned with the portable cache when verification fails.
            ShowWindow(state.platform->handle, previousVisible ? SW_SHOWNOACTIVATE : SW_HIDE);
            return visibilityStatus;
        }

        if(actualVisible != visible)
        {
            ShowWindow(state.platform->handle, previousVisible ? SW_SHOWNOACTIVATE : SW_HIDE);
            return GameWIP::Desktop::Internal::statusFor(
                GameWIP::Status::Types::Code::NativeFailure,
                ERROR_FUNCTION_FAILED,
                GameWIP::Desktop::Internal::NativeCodeDomain::Win32Error,
                visible ? "show ChildSurface" : "hide ChildSurface");
        }

        return GameWIP::Status::Types::Status{};
    }

    /// @brief Changes native child input enablement and verifies the resulting state.
    GameWIP::Status::Types::Status setChildSurfaceInteractionEnabled(ChildSurfaceState &state, bool enabled) noexcept
    {
        static_cast<void>(EnableWindow(state.platform->handle, enabled ? TRUE : FALSE));

        if((IsWindowEnabled(state.platform->handle) != FALSE) != enabled)
        {
            return statusFromWin32(GameWIP::Status::Types::Code::NativeFailure, ERROR_FUNCTION_FAILED, "set ChildSurface interaction");
        }

        return GameWIP::Status::Types::Status{};
    }

    /// @brief Places the child at the front or back of its native sibling order.
    GameWIP::Status::Types::Status orderChildSurfaceEdge(ChildSurfaceState &state, bool front) noexcept
    {
        if(SetWindowPos(state.platform->handle, front ? HWND_TOP : HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) == FALSE)
        {
            return statusFromWin32(GameWIP::Status::Types::Code::NativeFailure, GetLastError(), "order ChildSurface");
        }

        return GameWIP::Status::Types::Status{};
    }

    /// @brief Places the child immediately above or below a validated native sibling.
    GameWIP::Status::Types::Status orderChildSurface(ChildSurfaceState &state, const ChildSurfaceState *sibling, bool above) noexcept
    {
        HWND insertAfter = sibling->platform->handle;

        if(above)
        {
            insertAfter = GetWindow(sibling->platform->handle, GW_HWNDPREV);

            if(insertAfter == nullptr)
            {
                insertAfter = HWND_TOP;
            }
        }

        if(SetWindowPos(state.platform->handle, insertAfter, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) == FALSE)
        {
            return statusFromWin32(GameWIP::Status::Types::Code::NativeFailure, GetLastError(), "order ChildSurface relative to sibling");
        }

        return GameWIP::Status::Types::Status{};
    }
} // namespace GameWIP::Desktop::Platform

namespace GameWIP::Desktop::Native::Win32
{
    GameWIP::Status::Types::Result<HandleView> getHandle(const GameWIP::Desktop::ChildSurface &surface) noexcept
    {
        const Internal::ChildSurfaceState *state = Internal::ChildSurfaceAccess::state(surface);

        if(state == nullptr || !Platform::hasLiveNativeChildSurface(*state))
        {
            return GameWIP::Status::Types::Result<HandleView>::failure(
                GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::NotOpen));
        }

        if(!Platform::isChildSurfaceOwnedByCurrentThread(*state))
        {
            return GameWIP::Status::Types::Result<HandleView>::failure(
                GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::ResourceBusy));
        }
        const Platform::NativeHandleView handles = Platform::childSurfaceNativeHandle(*state);
        return GameWIP::Status::Types::Result<HandleView>::success(
            HandleView{static_cast<HINSTANCE>(handles.instance), static_cast<HWND>(handles.window)});
    }
} // namespace GameWIP::Desktop::Native::Win32

#if DESKTOP_INTERNAL_TEST_HOOKS
namespace GameWIP::Desktop::Internal::TestHooks
{
    ChildSurfaceDpiTransitionResult calculateChildSurfaceDpiTransition(Types::LogicalRect logicalRect, std::uint32_t newDpi) noexcept
    {
        if(newDpi == 0)
        {
            return {};
        }
        return {logicalRect, Platform::logicalToPhysicalSize(logicalRect.size, newDpi)};
    }

    GameWIP::Status::Types::Status destroyNativeChildSurface(ChildSurface &surface) noexcept
    {
        Internal::ChildSurfaceState *state = Internal::ChildSurfaceAccess::state(surface);

        if(state == nullptr || !Platform::hasLiveNativeChildSurface(*state))
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::NotOpen);
        }

        if(!Platform::isChildSurfaceOwnedByCurrentThread(*state))
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::ResourceBusy);
        }
        const Platform::NativeHandleView handle = Platform::childSurfaceNativeHandle(*state);

        if(DestroyWindow(static_cast<HWND>(handle.window)) == FALSE)
        {
            return Platform::statusFromWin32(GameWIP::Status::Types::Code::CloseFailed, GetLastError(), "test-hook destroy ChildSurface");
        }

        return GameWIP::Status::Types::Status{};
    }
} // namespace GameWIP::Desktop::Internal::TestHooks
#endif
