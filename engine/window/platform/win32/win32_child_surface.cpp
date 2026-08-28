/// @file win32_child_surface.cpp
/// @brief Win32 ChildSurface lifecycle, DPI, geometry, ordering, and native interop.

#include "window/platform/win32/internal/win32_window_backend.h"

#include "window/child_surface.h"
#include "window/internal/child_surface_platform.h"
#include "window/native/win32.h"

#include <algorithm>
#include <limits>
#include <mutex>
#include <utility>

namespace GameWIP::Window::Detail::Platform
{
    namespace
    {
        inline constexpr wchar_t kChildSurfaceClassName[] = L"GameWIP.Window.ChildSurface";
        std::mutex childClassMutex;
        std::size_t childClassUsers = 0;
        HINSTANCE childClassInstance = nullptr;
        bool childClassOwned = false;

        [[nodiscard]] IO::Types::Status acquireChildSurfaceClass(HINSTANCE instance) noexcept;
        [[nodiscard]] IO::Types::Status releaseChildSurfaceClass() noexcept;
        [[nodiscard]] LRESULT CALLBACK childSurfaceProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

        class DpiHostingScope final
        {
        public:
            DpiHostingScope() noexcept
                : previous_(SetThreadDpiHostingBehavior(DPI_HOSTING_BEHAVIOR_MIXED))
            {
            }

            ~DpiHostingScope() noexcept
            {
                if (previous_ != DPI_HOSTING_BEHAVIOR_INVALID)
                    static_cast<void>(SetThreadDpiHostingBehavior(previous_));
            }

            [[nodiscard]] bool applied() const noexcept
            {
                return previous_ != DPI_HOSTING_BEHAVIOR_INVALID;
            }

        private:
            DPI_HOSTING_BEHAVIOR previous_ = DPI_HOSTING_BEHAVIOR_INVALID;
        };

        [[nodiscard]] bool physicalRect(const Types::LogicalRect &rect, UINT dpi, int &x, int &y, int &width, int &height) noexcept
        {
            LONG nativeX = 0;
            LONG nativeY = 0;
            if (!logicalToPhysicalChecked(rect.position.x, dpi, nativeX) || !logicalToPhysicalChecked(rect.position.y, dpi, nativeY))
                return false;
            const Types::PixelSize size = logicalToPhysicalSize(rect.size, dpi);
            if (size.width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
                size.height > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
                return false;
            x = nativeX;
            y = nativeY;
            width = static_cast<int>(size.width);
            height = static_cast<int>(size.height);
            return true;
        }

        void refreshChildScreenRect(ChildSurfaceState &state) noexcept
        {
            if (!state.platform || state.platform->handle == nullptr)
                return;
            RECT native{};
            if (GetWindowRect(state.platform->handle, &native) == FALSE)
                return;
            state.screenRect = {
                {native.left, native.top},
                {static_cast<std::uint32_t>(std::max<LONG>(0, native.right - native.left)),
                 static_cast<std::uint32_t>(std::max<LONG>(0, native.bottom - native.top))}};
        }

        [[nodiscard]] bool childVisible(HWND window) noexcept
        {
            return (static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE)) & WS_VISIBLE) != 0;
        }

        [[nodiscard]] IO::Types::Status acquireChildSurfaceClass(HINSTANCE instance) noexcept
        {
            std::scoped_lock lock(childClassMutex);
            if (childClassUsers != 0)
            {
                ++childClassUsers;
                return IO::successStatus();
            }
            WNDCLASSEXW nativeClass{};
            nativeClass.cbSize = sizeof(nativeClass);
            nativeClass.style = CS_DBLCLKS;
            nativeClass.lpfnWndProc = childSurfaceProc;
            nativeClass.hInstance = instance;
            nativeClass.lpszClassName = kChildSurfaceClassName;
            if (RegisterClassExW(&nativeClass) == 0)
            {
                const DWORD code = GetLastError();
                if (code != ERROR_CLASS_ALREADY_EXISTS)
                    return statusFromWin32(IO::Types::ErrorCode::OpenFailed, code, "register ChildSurface class");
                childClassOwned = false;
            }
            else
            {
                childClassOwned = true;
            }
            childClassInstance = instance;
            childClassUsers = 1;
            return IO::successStatus();
        }

        [[nodiscard]] IO::Types::Status releaseChildSurfaceClass() noexcept
        {
            std::scoped_lock lock(childClassMutex);
            if (childClassUsers == 0)
                return IO::successStatus();
            --childClassUsers;
            if (childClassUsers != 0 || !childClassOwned)
                return IO::successStatus();
            if (UnregisterClassW(kChildSurfaceClassName, childClassInstance) == FALSE)
            {
                ++childClassUsers;
                return statusFromWin32(IO::Types::ErrorCode::CloseFailed, GetLastError(), "unregister ChildSurface class");
            }
            childClassInstance = nullptr;
            childClassOwned = false;
            return IO::successStatus();
        }

        [[nodiscard]] LRESULT CALLBACK childSurfaceProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
        {
            auto *state = reinterpret_cast<ChildSurfaceState *>(GetWindowLongPtrW(window, GWLP_USERDATA));
            if (message == WM_NCCREATE)
            {
                const auto *create = reinterpret_cast<const CREATESTRUCTW *>(lParam);
                state = static_cast<ChildSurfaceState *>(create->lpCreateParams);
                if (state == nullptr)
                    return FALSE;
                SetLastError(ERROR_SUCCESS);
                if (SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state)) == 0 && GetLastError() != ERROR_SUCCESS)
                    return FALSE;
            }
            if (state == nullptr)
                return DefWindowProcW(window, message, wParam, lParam);

            switch (message)
            {
            case WM_ERASEBKGND:
                return 1;
            case WM_DPICHANGED_BEFOREPARENT:
                return 0;
            case WM_DPICHANGED_AFTERPARENT:
            {
                const UINT newDpi = dpiForWindow(window);
                const auto oldDpiValue = static_cast<UINT>(state->dpi.x);
                if (newDpi == 0 || newDpi == oldDpiValue)
                    return 0;
                const Types::ContentScale previousScale = state->contentScale;
                const Types::Dpi previousDpi = state->dpi;
                const Types::PixelSize previousPixels = state->pixelSize;
                int x = 0;
                int y = 0;
                int width = 0;
                int height = 0;
                if (!physicalRect(state->rect, newDpi, x, y, width, height))
                {
                    recordPumpFailure(IO::makeStatus(IO::Types::ErrorCode::NativeFailure, ERROR_ARITHMETIC_OVERFLOW, "scale ChildSurface geometry"));
                    return 0;
                }
                if (SetWindowPos(window, nullptr, x, y, width, height, SWP_NOACTIVATE | SWP_NOZORDER) == FALSE)
                {
                    recordPumpFailure(statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "apply ChildSurface DPI geometry"));
                    return 0;
                }
                state->dpi = {static_cast<float>(newDpi), static_cast<float>(newDpi)};
                state->contentScale = {
                    static_cast<float>(newDpi) / static_cast<float>(kBaselineDpi),
                    static_cast<float>(newDpi) / static_cast<float>(kBaselineDpi)};
                state->pixelSize = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
                refreshChildScreenRect(*state);
                routeChildSurfaceEvent(
                    *state,
                    Types::ChildSurface::Events::ContentScaleChanged{previousScale, state->contentScale, previousDpi, state->dpi, state->pixelSize});
                if (previousPixels != state->pixelSize)
                    routeChildSurfaceEvent(*state, Types::ChildSurface::Events::PixelSizeChanged{state->pixelSize});
                return 0;
            }
            case WM_NCDESTROY:
            {
                const bool unexpected = !state->platform || !state->platform->destroying;
                SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                if (state->platform)
                {
                    state->platform->handle = nullptr;
                    if (state->platform->registered)
                    {
                        unregisterOpenChildSurface(*state);
                        state->platform->registered = false;
                    }
                }
                if (unexpected)
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

    void ChildSurfaceDataDeleter::operator()(ChildSurfaceData *data) const noexcept
    {
        delete data;
    }

    void refreshChildSurfaceScreenRect(ChildSurfaceState &state) noexcept
    {
        refreshChildScreenRect(state);
    }

    IO::Types::Status openChildSurface(ChildSurfaceState &state, WindowState &parent) noexcept
    {
        try
        {
            if (!parent.platform || parent.platform->handle == nullptr)
                return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
            auto data = std::unique_ptr<ChildSurfaceData, ChildSurfaceDataDeleter>(new ChildSurfaceData{});
            data->owner = &state;
            data->instance = parent.platform->instance;
            data->ownerThreadId = parent.platform->ownerThreadId;
            IO::Types::Status status = acquireChildSurfaceClass(data->instance);
            if (!status.ok())
                return status;
            data->classReferenceHeld = true;
            state.platform = std::move(data);
            registerOpenChildSurface(state);
            state.platform->registered = true;

            const UINT dpi = dpiForWindow(parent.platform->handle);
            int x = 0;
            int y = 0;
            int width = 0;
            int height = 0;
            if (!physicalRect(state.rect, dpi, x, y, width, height))
                return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument, ERROR_ARITHMETIC_OVERFLOW, "ChildSurface rect exceeds Win32 range");
            if (Detail::consumeFailure(TestHooks::FailurePoint::NativeCreation))
                return IO::makeStatus(IO::Types::ErrorCode::OpenFailed);

            {
                DpiHostingScope hosting;
                if (!hosting.applied())
                    return statusFromWin32(IO::Types::ErrorCode::OpenFailed, GetLastError(), "enable mixed DPI hosting for ChildSurface");
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
            }
            if (state.platform->handle == nullptr)
                return statusFromWin32(IO::Types::ErrorCode::OpenFailed, GetLastError(), "create ChildSurface host");

            state.dpi = {static_cast<float>(dpi), static_cast<float>(dpi)};
            state.contentScale = {
                static_cast<float>(dpi) / static_cast<float>(kBaselineDpi),
                static_cast<float>(dpi) / static_cast<float>(kBaselineDpi)};
            state.pixelSize = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
            static_cast<void>(EnableWindow(state.platform->handle, state.interactionEnabled ? TRUE : FALSE));
            if ((IsWindowEnabled(state.platform->handle) != FALSE) != state.interactionEnabled)
                return statusFromWin32(IO::Types::ErrorCode::OpenFailed, GetLastError(), "set initial ChildSurface interaction");
            ShowWindow(state.platform->handle, state.visible ? SW_SHOWNOACTIVATE : SW_HIDE);
            state.visible = childVisible(state.platform->handle);
            state.interactionEnabled = IsWindowEnabled(state.platform->handle) != FALSE;
            refreshChildScreenRect(state);
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

    CloseResult closeChildSurface(ChildSurfaceState &state) noexcept
    {
        if (!state.platform)
            return {IO::successStatus(), true};
        if (state.platform->ownerThreadId != GetCurrentThreadId())
            return {IO::makeStatus(IO::Types::ErrorCode::ResourceBusy), false};
        if (Detail::consumeFailure(TestHooks::FailurePoint::Close))
            return {IO::makeStatus(IO::Types::ErrorCode::CloseFailed), false};
        if (state.platform->handle != nullptr)
        {
            state.platform->destroying = true;
            if (DestroyWindow(state.platform->handle) == FALSE)
            {
                state.platform->destroying = false;
                return {statusFromWin32(IO::Types::ErrorCode::CloseFailed, GetLastError(), "destroy ChildSurface host"), false};
            }
        }
        if (state.platform->registered)
        {
            unregisterOpenChildSurface(state);
            state.platform->registered = false;
        }
        IO::Types::Status status = IO::successStatus();
        if (state.platform->classReferenceHeld)
        {
            status = releaseChildSurfaceClass();
            state.platform->classReferenceHeld = false;
        }
        state.platform.reset();
        state.nativeDestroyedPendingFinalize = false;
        return {std::move(status), true};
    }

    void closeChildSurfaceBestEffort(ChildSurfaceState &state) noexcept
    {
        if (!state.platform)
            return;
        if (state.platform->ownerThreadId != GetCurrentThreadId())
        {
            if (state.platform->handle != nullptr && IsWindow(state.platform->handle) != FALSE)
                return;
            if (state.platform->classReferenceHeld)
                static_cast<void>(releaseChildSurfaceClass());
            state.platform.reset();
            return;
        }
        if (state.platform->handle != nullptr)
        {
            state.platform->destroying = true;
            static_cast<void>(DestroyWindow(state.platform->handle));
        }
        if (state.platform->registered)
            unregisterOpenChildSurface(state);
        if (state.platform->classReferenceHeld)
            static_cast<void>(releaseChildSurfaceClass());
        state.platform.reset();
    }

    bool isChildSurfaceOwnedByCurrentThread(const ChildSurfaceState &state) noexcept
    {
        return state.platform && state.platform->ownerThreadId == GetCurrentThreadId();
    }

    bool hasLiveNativeChildSurface(const ChildSurfaceState &state) noexcept
    {
        return state.platform && state.platform->handle != nullptr && IsWindow(state.platform->handle) != FALSE;
    }

    NativeHandleView childSurfaceNativeHandle(const ChildSurfaceState &state) noexcept
    {
        return state.platform ? NativeHandleView{state.platform->instance, state.platform->handle} : NativeHandleView{};
    }

    IO::Types::Status setChildSurfaceRect(ChildSurfaceState &state, Types::LogicalRect rect) noexcept
    {
        if (!state.platform || state.platform->handle == nullptr)
            return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        const UINT dpi = dpiForWindow(state.platform->handle);
        if (!physicalRect(rect, dpi, x, y, width, height))
            return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
        if (SetWindowPos(state.platform->handle, nullptr, x, y, width, height, SWP_NOACTIVATE | SWP_NOZORDER) == FALSE)
            return statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "set ChildSurface rect");
        refreshChildScreenRect(state);
        return IO::successStatus();
    }

    Types::ScreenPositionResult childSurfaceClientToScreen(const ChildSurfaceState &state, Types::LogicalPosition position) noexcept
    {
        LONG x = 0;
        LONG y = 0;
        const UINT dpi = dpiForWindow(state.platform->handle);
        if (!logicalToPhysicalChecked(position.x, dpi, x) || !logicalToPhysicalChecked(position.y, dpi, y))
            return {.status = IO::makeStatus(IO::Types::ErrorCode::InvalidArgument)};
        POINT point{x, y};
        if (ClientToScreen(state.platform->handle, &point) == FALSE)
            return {.status = statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "ChildSurface client to screen")};
        return {.status = IO::successStatus(), .position = {point.x, point.y}};
    }

    Types::LogicalPositionResult childSurfaceScreenToClient(const ChildSurfaceState &state, Types::ScreenPosition position) noexcept
    {
        POINT point{position.x, position.y};
        if (ScreenToClient(state.platform->handle, &point) == FALSE)
            return {.status = statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "ChildSurface screen to client")};
        const UINT dpi = dpiForWindow(state.platform->handle);
        return {.status = IO::successStatus(), .position = {physicalToLogical(point.x, dpi), physicalToLogical(point.y, dpi)}};
    }

    IO::Types::Status showChildSurface(ChildSurfaceState &state, bool visible) noexcept
    {
        ShowWindow(state.platform->handle, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
        if (childVisible(state.platform->handle) != visible)
            return statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), visible ? "show ChildSurface" : "hide ChildSurface");
        return IO::successStatus();
    }

    IO::Types::Status setChildSurfaceInteractionEnabled(ChildSurfaceState &state, bool enabled) noexcept
    {
        static_cast<void>(EnableWindow(state.platform->handle, enabled ? TRUE : FALSE));
        if ((IsWindowEnabled(state.platform->handle) != FALSE) != enabled)
            return statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "set ChildSurface interaction");
        return IO::successStatus();
    }

    IO::Types::Status orderChildSurfaceEdge(ChildSurfaceState &state, bool front) noexcept
    {
        if (SetWindowPos(state.platform->handle, front ? HWND_TOP : HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) == FALSE)
            return statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "order ChildSurface");
        return IO::successStatus();
    }

    IO::Types::Status orderChildSurface(ChildSurfaceState &state, const ChildSurfaceState *sibling, bool above) noexcept
    {
        HWND insertAfter = sibling->platform->handle;
        if (above)
        {
            insertAfter = GetWindow(sibling->platform->handle, GW_HWNDPREV);
            if (insertAfter == nullptr)
                insertAfter = HWND_TOP;
        }
        if (SetWindowPos(state.platform->handle, insertAfter, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) == FALSE)
            return statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "order ChildSurface relative to sibling");
        return IO::successStatus();
    }
} // namespace GameWIP::Window::Detail::Platform

namespace GameWIP::Window::Native::Win32
{
    HandleResult getHandle(const GameWIP::Window::ChildSurface &surface) noexcept
    {
        const Detail::ChildSurfaceState *state = Detail::ChildSurfaceAccess::state(surface);
        if (state == nullptr || !Detail::Platform::hasLiveNativeChildSurface(*state))
            return {.status = IO::makeStatus(IO::Types::ErrorCode::NotOpen)};
        if (!Detail::Platform::isChildSurfaceOwnedByCurrentThread(*state))
            return {.status = IO::makeStatus(IO::Types::ErrorCode::ResourceBusy)};
        const Detail::Platform::NativeHandleView handles = Detail::Platform::childSurfaceNativeHandle(*state);
        return {.status = IO::successStatus(), .handle = {static_cast<HINSTANCE>(handles.instance), static_cast<HWND>(handles.window)}};
    }
} // namespace GameWIP::Window::Native::Win32

#if WINDOW_INTERNAL_TEST_HOOKS
namespace GameWIP::Window::TestHooks
{
    ChildSurfaceDpiTransitionResult calculateChildSurfaceDpiTransition(Types::LogicalRect logicalRect, std::uint32_t newDpi) noexcept
    {
        if (newDpi == 0)
            return {};
        return {logicalRect, Detail::Platform::logicalToPhysicalSize(logicalRect.size, newDpi)};
    }

    IO::Types::Status destroyNativeChildSurface(ChildSurface &surface) noexcept
    {
        Detail::ChildSurfaceState *state = Detail::ChildSurfaceAccess::state(surface);
        if (state == nullptr || !Detail::Platform::hasLiveNativeChildSurface(*state))
            return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
        if (!Detail::Platform::isChildSurfaceOwnedByCurrentThread(*state))
            return IO::makeStatus(IO::Types::ErrorCode::ResourceBusy);
        const Detail::Platform::NativeHandleView handle = Detail::Platform::childSurfaceNativeHandle(*state);
        if (DestroyWindow(static_cast<HWND>(handle.window)) == FALSE)
            return Detail::Platform::statusFromWin32(IO::Types::ErrorCode::CloseFailed, GetLastError(), "test-hook destroy ChildSurface");
        return IO::successStatus();
    }
} // namespace GameWIP::Window::TestHooks
#endif
