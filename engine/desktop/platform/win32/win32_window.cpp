/// @file win32_window.cpp
/// @brief Win32 process runtime, registries, dispatcher ownership, and native operations.

#include "desktop/platform/win32/internal/win32_window_backend.h"
#include "desktop/platform/win32/internal/win32_compat.h"

#include "desktop/native/win32.h"
#include "desktop/internal/child_surface_platform.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <new>
#include <utility>

namespace GameWIP::Desktop::Detail::Platform
{
    // ------------------------------------------------------------
    // Process and thread registries
    // ------------------------------------------------------------
    namespace
    {
        std::mutex classMutex;
        std::size_t classUsers = 0;
        HINSTANCE classInstance = nullptr;
        bool classOwned = false;

        std::mutex windowRegistryMutex;
        std::unordered_map<std::uint64_t, WindowState *> windowRegistry;
        std::atomic_uint64_t nextWindowId{1};

        std::mutex dispatcherRegistryMutex;
        std::unordered_map<DWORD, Dispatcher *> dispatcherRegistry;

        thread_local Dispatcher threadDispatcher{GetCurrentThreadId()};

    } // namespace

    // ------------------------------------------------------------
    // Native class lifetime
    // ------------------------------------------------------------
    IO::Types::Status acquireWindowClass(HINSTANCE instance) noexcept
    {
        std::scoped_lock lock(classMutex);
        if (classUsers != 0)
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
        if (RegisterClassExW(&windowClass) == 0)
        {
            const DWORD nativeCode = GetLastError();
            if (nativeCode != ERROR_CLASS_ALREADY_EXISTS)
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

    IO::Types::Status releaseWindowClass() noexcept
    {
        std::scoped_lock lock(classMutex);
        if (classUsers == 0)
        {
            return IO::successStatus();
        }
        --classUsers;
        if (classUsers != 0 || !classOwned)
        {
            return IO::successStatus();
        }
        if (UnregisterClassW(kWindowClassName, classInstance) == FALSE)
        {
            ++classUsers;
            return statusFromWin32(IO::Types::ErrorCode::CloseFailed, GetLastError(), "UnregisterClassW");
        }
        classInstance = nullptr;
        classOwned = false;
        return IO::successStatus();
    }

    void registerWindowId(WindowState &state)
    {
        std::uint64_t value = nextWindowId.fetch_add(1, std::memory_order_relaxed);
        if (value == 0)
        {
            value = nextWindowId.fetch_add(1, std::memory_order_relaxed);
        }
        std::scoped_lock lock(windowRegistryMutex);
        windowRegistry.emplace(value, &state);
        state.id = {value};
    }

    // ------------------------------------------------------------
    // Window identity and native styles
    // ------------------------------------------------------------
    void unregisterWindowId(WindowState &state) noexcept
    {
        std::scoped_lock lock(windowRegistryMutex);
        const Types::WindowId removed = state.id;
        windowRegistry.erase(state.id.value);
        state.id = {};
        state.owner = {};
        if (!removed.isValid())
            return;
        for (const auto &[id, candidate] : windowRegistry)
        {
            static_cast<void>(id);
            if (candidate != nullptr && candidate->owner == removed)
            {
                candidate->owner = {};
                if (candidate->platform && candidate->platform->handle != nullptr)
                {
                    SetLastError(ERROR_SUCCESS);
                    if (SetWindowLongPtrW(candidate->platform->handle, GWLP_HWNDPARENT, 0) == 0 && GetLastError() != ERROR_SUCCESS)
                    {
                        recordPumpFailure(statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "clear destroyed window owner"));
                    }
                    if (IO::Types::Status styleStatus = applyStyle(*candidate); !styleStatus.ok())
                        recordPumpFailure(std::move(styleStatus));
                }
                routeEvent(*candidate, Types::Events::OwnerChanged{removed, {}});
            }
        }
    }

    DWORD styleFor(const WindowState &state) noexcept
    {
        DWORD style = WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
        if (state.mode != Types::Mode::Windowed)
        {
            return style | WS_POPUP;
        }
        if (state.decoration == Types::DecorationMode::System)
        {
            style |= WS_OVERLAPPED | WS_CAPTION;
        }
        else
        {
            style |= WS_POPUP;
        }
        if (state.controls.closable || state.controls.minimizable || state.controls.maximizable)
            style |= WS_SYSMENU;
        if (state.controls.minimizable)
            style |= WS_MINIMIZEBOX;
        if (state.controls.maximizable)
            style |= WS_MAXIMIZEBOX;
        if (state.resizable)
            style |= WS_THICKFRAME;
        return style;
    }

    DWORD extendedStyleFor(const WindowState &state) noexcept
    {
        DWORD style = state.owner.isValid() ? 0 : WS_EX_APPWINDOW;
        if (!state.focusable)
            style |= WS_EX_NOACTIVATE;
        if (state.alwaysOnTop)
            style |= WS_EX_TOPMOST;
        if (state.opacity < 1.0F || state.pointerInputMode == Types::PointerInputMode::ClickThrough)
            style |= WS_EX_LAYERED;
        if (state.pointerInputMode == Types::PointerInputMode::ClickThrough)
            style |= WS_EX_TRANSPARENT;
        return style;
    }

    namespace
    {
        bool setLong(HWND window, int index, LONG_PTR value, DWORD &nativeCode) noexcept
        {
            SetLastError(ERROR_SUCCESS);
            const LONG_PTR previous = SetWindowLongPtrW(window, index, value);
            nativeCode = GetLastError();
            return previous != 0 || nativeCode == ERROR_SUCCESS;
        }
    } // namespace

    // ------------------------------------------------------------
    // Event routing and dispatcher state
    // ------------------------------------------------------------
    Dispatcher &dispatcher() noexcept
    {
        return threadDispatcher;
    }

    UINT wakeMessage() noexcept
    {
        static const UINT value = RegisterWindowMessageW(L"GameWIP.Window.WakeEventWait");
        return value;
    }

    void routeEvent(WindowState &state, Types::Events::Payload data) noexcept
    {
        const std::uint64_t droppedBefore = state.droppedEvents;
        const EnqueueResult result = enqueueEvent(state, std::move(data));
        Dispatcher &current = dispatcher();
        if (current.activeResult != nullptr)
        {
            current.activeResult->eventsDropped += state.droppedEvents - droppedBefore;
            if (result != EnqueueResult::Dropped)
            {
                ++current.activeResult->eventsQueued;
            }
        }
    }

    void recordPumpFailure(IO::Types::Status status) noexcept
    {
        Dispatcher &current = dispatcher();
        if (current.activeResult != nullptr && current.activeResult->status.ok())
        {
            current.activeResult->status = std::move(status);
        }
    }

    void registerOpenState(WindowState &state)
    {
        Dispatcher &current = dispatcher();
        {
            std::scoped_lock lock(dispatcherRegistryMutex);
            dispatcherRegistry[current.threadId] = &current;
        }
        MSG message{};
        PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
        current.windows.push_back(&state);
    }

    void unregisterOpenState(WindowState &state) noexcept
    {
        Dispatcher &current = dispatcher();
        current.windows.erase(std::remove(current.windows.begin(), current.windows.end(), &state), current.windows.end());
    }

    void registerOpenChildSurface(ChildSurfaceState &state)
    {
        Dispatcher &current = dispatcher();
        {
            std::scoped_lock lock(dispatcherRegistryMutex);
            dispatcherRegistry[current.threadId] = &current;
        }
        MSG message{};
        PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
        current.childSurfaces.push_back(&state);
    }

    void unregisterOpenChildSurface(ChildSurfaceState &state) noexcept
    {
        Dispatcher &current = dispatcher();
        current.childSurfaces.erase(std::remove(current.childSurfaces.begin(), current.childSurfaces.end(), &state), current.childSurfaces.end());
    }

    void routeChildSurfaceEvent(ChildSurfaceState &state, Types::ChildSurface::Events::Payload data) noexcept
    {
        const std::uint64_t droppedBefore = state.droppedEvents;
        const ChildSurfaceEnqueueResult result = enqueueChildSurfaceEvent(state, data);
        Dispatcher &current = dispatcher();
        if (current.activeResult != nullptr)
        {
            current.activeResult->eventsDropped += state.droppedEvents - droppedBefore;
            if (result != ChildSurfaceEnqueueResult::Dropped)
                ++current.activeResult->eventsQueued;
        }
    }

    void refreshChildSurfaceScreenRectsForParent(Types::WindowId parentId) noexcept
    {
        if (!parentId.isValid())
            return;
        for (ChildSurfaceState *child : dispatcher().childSurfaces)
        {
            if (child != nullptr && child->parentId == parentId)
                refreshChildSurfaceScreenRect(*child);
        }
    }

    void pruneAbandonedStates(Dispatcher &current) noexcept
    {
        std::unique_ptr<WindowState> cleanup;
        {
            std::scoped_lock lock(current.deferredMutex);
            cleanup = std::move(current.deferredCleanupHead);
        }
        while (cleanup)
        {
            std::unique_ptr<WindowState> next{cleanup->deferredCleanupNext};
            cleanup->deferredCleanupNext = nullptr;
            closeBestEffort(*cleanup);
            cleanup = std::move(next);
        }
        std::unique_ptr<ChildSurfaceState> childCleanup;
        {
            std::scoped_lock lock(current.deferredMutex);
            childCleanup = std::move(current.deferredChildCleanupHead);
        }
        while (childCleanup)
        {
            std::unique_ptr<ChildSurfaceState> next{childCleanup->deferredCleanupNext};
            childCleanup->deferredCleanupNext = nullptr;
            closeChildSurfaceBestEffort(*childCleanup);
            childCleanup = std::move(next);
        }
    }

    Dispatcher::Dispatcher(DWORD owningThreadId) noexcept
        : threadId(owningThreadId)
    {
    }

    Dispatcher::~Dispatcher() noexcept
    {
        // Keep the registry locked through shutdown. A concurrent wrong-thread
        // destructor either transfers before this point or waits until every
        // owner-thread-affine native resource has been released.
        std::scoped_lock registryLock(dispatcherRegistryMutex);
        const auto found = dispatcherRegistry.find(threadId);
        if (found != dispatcherRegistry.end() && found->second == this)
            dispatcherRegistry.erase(found);

        std::unique_ptr<WindowState> deferred;
        {
            std::scoped_lock lock(deferredMutex);
            deferred = std::move(deferredCleanupHead);
        }
        while (deferred)
        {
            std::unique_ptr<WindowState> next{deferred->deferredCleanupNext};
            deferred->deferredCleanupNext = nullptr;
            closeBestEffort(*deferred);
            deferred = std::move(next);
        }

        std::unique_ptr<ChildSurfaceState> deferredChild;
        {
            std::scoped_lock lock(deferredMutex);
            deferredChild = std::move(deferredChildCleanupHead);
        }
        while (deferredChild)
        {
            std::unique_ptr<ChildSurfaceState> next{deferredChild->deferredCleanupNext};
            deferredChild->deferredCleanupNext = nullptr;
            closeChildSurfaceBestEffort(*deferredChild);
            deferredChild = std::move(next);
        }

        while (!childSurfaces.empty())
        {
            ChildSurfaceState *state = childSurfaces.back();
            if (state == nullptr)
            {
                childSurfaces.pop_back();
                continue;
            }
            closeChildSurfaceBestEffort(*state);
            state->clearRetainedEvents();
        }

        while (!windows.empty())
        {
            WindowState *state = windows.back();
            if (state == nullptr)
            {
                windows.pop_back();
                continue;
            }
            closeBestEffort(*state);
            state->clearRetainedEvents();
        }
    }

    WindowState *resolveWindowId(Types::WindowId id) noexcept
    {
        if (!id.isValid())
            return nullptr;
        std::scoped_lock lock(windowRegistryMutex);
        const auto found = windowRegistry.find(id.value);
        return found == windowRegistry.end() ? nullptr : found->second;
    }

    // ------------------------------------------------------------
    // Native status conversion
    // ------------------------------------------------------------
    IO::Types::Status statusFromWin32(IO::Types::ErrorCode fallback, DWORD nativeCode, std::string_view operation) noexcept
    {
        using IO::Types::ErrorCode;
        ErrorCode code = fallback;
        switch (nativeCode)
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
            if (length != 0)
            {
                message.append(": ");
                message.append(buffer.data(), length);
            }
            return IO::makeStatus(code, nativeCode, std::move(message));
        }
        catch (...)
        {
            return IO::makeStatus(code, nativeCode);
        }
    }

    IO::Types::Status statusFromDisplayChange(LONG nativeCode, std::string_view operation) noexcept
    {
        IO::Types::ErrorCode code = IO::Types::ErrorCode::NativeFailure;
        if (nativeCode == DISP_CHANGE_BADMODE || nativeCode == DISP_CHANGE_BADPARAM)
            code = IO::Types::ErrorCode::InvalidArgument;
        else if (nativeCode == DISP_CHANGE_NOTUPDATED)
            code = IO::Types::ErrorCode::PermissionDenied;
        try
        {
            return IO::makeStatus(code, nativeCode, std::format("{} failed with display status {}", operation, nativeCode));
        }
        catch (...)
        {
            return IO::makeStatus(code, nativeCode);
        }
    }

    // ------------------------------------------------------------
    // Geometry and hit testing
    // ------------------------------------------------------------
    UINT dpiForWindow(HWND window) noexcept
    {
        const UINT dpi = window != nullptr ? GetDpiForWindow(window) : GetDpiForSystem();
        return dpi == 0 ? kBaselineDpi : dpi;
    }

    LONG logicalToPhysical(std::int32_t value, UINT dpi) noexcept
    {
        LONG output = -1;
        static_cast<void>(logicalToPhysicalChecked(value, dpi, output));
        return output;
    }

    bool logicalToPhysicalChecked(std::int32_t value, UINT dpi, LONG &output) noexcept
    {
        if (dpi == 0)
            return false;
        const std::int64_t product = static_cast<std::int64_t>(value) * dpi;
        const std::int64_t rounded = product >= 0 ? (product + kBaselineDpi / 2) / kBaselineDpi : (product - kBaselineDpi / 2) / kBaselineDpi;
        if (rounded < std::numeric_limits<LONG>::min() || rounded > std::numeric_limits<LONG>::max())
            return false;
        output = static_cast<LONG>(rounded);
        return true;
    }
    std::int32_t physicalToLogical(LONG value, UINT dpi) noexcept
    {
        return MulDiv(value, kBaselineDpi, static_cast<int>(dpi));
    }
    Types::PixelSize logicalToPhysicalSize(Types::LogicalSize value, UINT dpi) noexcept
    {
        return {
            static_cast<std::uint32_t>(logicalToPhysical(static_cast<std::int32_t>(value.width), dpi)),
            static_cast<std::uint32_t>(logicalToPhysical(static_cast<std::int32_t>(value.height), dpi))};
    }
    Types::LogicalSize physicalToLogicalSize(std::uint32_t width, std::uint32_t height, UINT dpi) noexcept
    {
        return {
            static_cast<std::uint32_t>(std::max(0, physicalToLogical(static_cast<LONG>(width), dpi))),
            static_cast<std::uint32_t>(std::max(0, physicalToLogical(static_cast<LONG>(height), dpi)))};
    }

    bool pointInRect(Types::LogicalPosition point, const Types::LogicalRect &rect) noexcept
    {
        const std::int64_t right = static_cast<std::int64_t>(rect.position.x) + rect.size.width;
        const std::int64_t bottom = static_cast<std::int64_t>(rect.position.y) + rect.size.height;
        return point.x >= rect.position.x && point.y >= rect.position.y && point.x < right && point.y < bottom;
    }

    HCURSOR loadCursor(Types::CursorShape shape) noexcept
    {
        if (Detail::consumeFailure(TestHooks::FailurePoint::SystemCursorLoad))
        {
            SetLastError(ERROR_GEN_FAILURE);
            return nullptr;
        }
        WORD identifier = 32512;
        switch (shape)
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

    // ------------------------------------------------------------
    // Cached native state
    // ------------------------------------------------------------
    IO::Types::Status refreshCachedGeometry(WindowState &state) noexcept
    {
        if (!state.platform || state.platform->handle == nullptr)
            return IO::makeStatus(IO::Types::ErrorCode::NotOpen);

        RECT client{};
        RECT frame{};
        POINT clientOrigin{};
        if (GetClientRect(state.platform->handle, &client) == FALSE || ClientToScreen(state.platform->handle, &clientOrigin) == FALSE ||
            GetWindowRect(state.platform->handle, &frame) == FALSE)
        {
            return statusFromWin32(IO::Types::ErrorCode::StatFailed, GetLastError(), "query window geometry");
        }

        const UINT dpi = dpiForWindow(state.platform->handle);
        const auto physicalWidth = static_cast<std::uint32_t>(std::max<LONG>(0, client.right - client.left));
        const auto physicalHeight = static_cast<std::uint32_t>(std::max<LONG>(0, client.bottom - client.top));
        state.framebufferSize = {physicalWidth, physicalHeight};
        const RendererIntegrationState *renderer = state.rendererIntegration;
        if (renderer != nullptr && ((renderer->pointerHitMaskActiveGeneration != 0 && renderer->pointerHitMaskSize != state.framebufferSize) ||
                                    (renderer->pointerHitMaskTargetGeneration != 0 && renderer->pointerHitMaskTargetSize != state.framebufferSize)))
            invalidatePointerHitMask(state);
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
        refreshChildSurfaceScreenRectsForParent(state.id);
        return IO::successStatus();
    }

    void updateCurrentMonitor(WindowState &state) noexcept
    {
        if (!state.platform || state.platform->handle == nullptr)
            return;
        HMONITOR native = MonitorFromWindow(state.platform->handle, MONITOR_DEFAULTTONEAREST);
        Types::Display::InfoResult info = monitorFromNative(native);
        if (!info.status.ok())
        {
            recordPumpFailure(std::move(info.status));
            return;
        }
        if (info.monitor.id != state.monitor)
        {
            const Types::Display::MonitorId previous = state.monitor;
            state.monitor = info.monitor.id;
            routeEvent(state, Types::Events::MonitorChanged{previous, state.monitor});
        }
    }

    IO::Types::Status applyCursorState(WindowState &state) noexcept
    {
        if (!state.platform || state.platform->handle == nullptr)
            return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
        if (Detail::consumeFailure(TestHooks::FailurePoint::Cursor))
            return IO::makeStatus(IO::Types::ErrorCode::NativeFailure);

        WindowData &data = *state.platform;
        const bool confined = state.cursorMode == Types::CursorMode::Confined || state.cursorMode == Types::CursorMode::HiddenConfined ||
                              state.cursorMode == Types::CursorMode::Relative;
        const bool shouldClip = confined && state.focused && state.visible && state.presentation != Types::PresentationState::Minimized;
        if (!shouldClip)
        {
            if (data.cursorClipApplied && ClipCursor(nullptr) == FALSE)
                return statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "ClipCursor release");
            data.cursorClipApplied = false;
            return IO::successStatus();
        }

        RECT client{};
        POINT topLeft{};
        POINT bottomRight{};
        if (GetClientRect(data.handle, &client) == FALSE)
            return statusFromWin32(IO::Types::ErrorCode::StatFailed, GetLastError(), "GetClientRect for cursor");
        topLeft = {client.left, client.top};
        bottomRight = {client.right, client.bottom};
        if (ClientToScreen(data.handle, &topLeft) == FALSE || ClientToScreen(data.handle, &bottomRight) == FALSE)
            return statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "ClientToScreen for cursor");
        RECT clip{topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
        if (ClipCursor(&clip) == FALSE)
            return statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "ClipCursor");
        data.cursorClipApplied = true;

        if (state.cursorMode == Types::CursorMode::Relative)
        {
            const int x = topLeft.x + (bottomRight.x - topLeft.x) / 2;
            const int y = topLeft.y + (bottomRight.y - topLeft.y) / 2;
            if (SetCursorPos(x, y) == FALSE)
                return statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "SetCursorPos relative center");
        }
        return IO::successStatus();
    }

    IO::Types::Status applyStyle(WindowState &state) noexcept
    {
        if (!state.platform || state.platform->handle == nullptr)
            return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
        // ShowWindow, EnableWindow, minimize, and maximize own these runtime bits. Rebuilding
        // the configurable frame must not make a still-painted HWND invisible to Explorer or
        // silently re-enable/restore it by replacing the complete style word.
        constexpr DWORD runtimeStyleBits = WS_VISIBLE | WS_DISABLED | WS_MINIMIZE | WS_MAXIMIZE;
        const DWORD currentStyle = static_cast<DWORD>(GetWindowLongPtrW(state.platform->handle, GWL_STYLE));
        const DWORD desiredStyle = styleFor(state) | (currentStyle & runtimeStyleBits);
        // Window owns these extended-style policies. Preserve unrelated native state such as
        // WS_EX_ACCEPTFILES, which DragAcceptFiles manages independently.
        constexpr DWORD controlledExtendedStyleBits = WS_EX_APPWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT;
        const DWORD currentExtendedStyle = static_cast<DWORD>(GetWindowLongPtrW(state.platform->handle, GWL_EXSTYLE));
        const DWORD desiredExtendedStyle = extendedStyleFor(state) | (currentExtendedStyle & ~controlledExtendedStyleBits);
        DWORD nativeCode = ERROR_SUCCESS;
        if (!setLong(state.platform->handle, GWL_STYLE, desiredStyle, nativeCode))
            return statusFromWin32(IO::Types::ErrorCode::NativeFailure, nativeCode, "SetWindowLongPtrW style");
        if (!setLong(state.platform->handle, GWL_EXSTYLE, desiredExtendedStyle, nativeCode))
            return statusFromWin32(IO::Types::ErrorCode::NativeFailure, nativeCode, "SetWindowLongPtrW extended style");
        if (SetWindowPos(
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

    // ------------------------------------------------------------
    // Exclusive-mode cleanup
    // ------------------------------------------------------------
    IO::Types::Status leaveExclusive(WindowState &state) noexcept
    {
        if (state.platform && state.mode != Types::Mode::Windowed && Detail::consumeFailure(TestHooks::FailurePoint::DisplayRestoration))
        {
            return IO::makeStatus(IO::Types::ErrorCode::NativeFailure);
        }
        if (!state.platform || !state.platform->hasSavedDisplayMode)
        {
            state.fullscreen = {};
            return IO::successStatus();
        }
        WindowData &data = *state.platform;
        if (!data.exclusiveSuspended)
        {
            const LONG result = ChangeDisplaySettingsExW(data.exclusiveDevice.c_str(), &data.savedDisplayMode, nullptr, 0, nullptr);
            if (result != DISP_CHANGE_SUCCESSFUL)
                return statusFromDisplayChange(result, "restore desktop display mode");
        }
        data.hasSavedDisplayMode = false;
        data.exclusiveSuspended = false;
        data.exclusiveDevice.clear();
        data.activeDisplayMode = {};
        data.activeNativeDisplayMode = {};
        state.fullscreen = {};
        return IO::successStatus();
    }

    IO::Types::Status suspendExclusive(WindowState &state) noexcept
    {
        if (!state.platform || state.platform->modeTransitionDepth != 0 || !state.platform->hasSavedDisplayMode || state.platform->exclusiveSuspended)
            return IO::successStatus();
        const LONG result = ChangeDisplaySettingsExW(state.platform->exclusiveDevice.c_str(), &state.platform->savedDisplayMode, nullptr, 0, nullptr);
        if (result != DISP_CHANGE_SUCCESSFUL)
            return statusFromDisplayChange(result, "suspend exclusive fullscreen");
        state.platform->exclusiveSuspended = true;
        state.fullscreen.suspended = true;
        return IO::successStatus();
    }

    IO::Types::Status resumeExclusive(WindowState &state) noexcept
    {
        if (!state.platform || state.platform->modeTransitionDepth != 0 || !state.platform->hasSavedDisplayMode ||
            !state.platform->exclusiveSuspended)
            return IO::successStatus();
        const LONG validationResult =
            ChangeDisplaySettingsExW(state.platform->exclusiveDevice.c_str(), &state.platform->activeNativeDisplayMode, nullptr, CDS_TEST, nullptr);
        if (validationResult != DISP_CHANGE_SUCCESSFUL)
            return statusFromDisplayChange(validationResult, "validate resumed exclusive fullscreen display mode");
        const LONG result = ChangeDisplaySettingsExW(
            state.platform->exclusiveDevice.c_str(),
            &state.platform->activeNativeDisplayMode,
            nullptr,
            CDS_FULLSCREEN,
            nullptr);
        if (result != DISP_CHANGE_SUCCESSFUL)
            return statusFromDisplayChange(result, "resume exclusive fullscreen");
        state.platform->exclusiveSuspended = false;
        state.fullscreen.suspended = false;
        return IO::successStatus();
    }

    // ------------------------------------------------------------
    // Deferred owner-thread cleanup
    // ------------------------------------------------------------
    bool deferCleanupToOwner(std::unique_ptr<WindowState> &state) noexcept
    {
        if (!state || !state->platform)
            return true;
        std::scoped_lock registryLock(dispatcherRegistryMutex);
        const auto found = dispatcherRegistry.find(state->platform->ownerThreadId);
        Dispatcher *owner = found == dispatcherRegistry.end() ? nullptr : found->second;
        if (owner == nullptr)
            return false;
        {
            std::scoped_lock lock(owner->deferredMutex);
            state->deferredCleanupNext = owner->deferredCleanupHead.release();
            owner->deferredCleanupHead = std::move(state);
        }
        static_cast<void>(PostThreadMessageW(owner->threadId, wakeMessage(), 0, 0));
        return true;
    }

    bool deferChildSurfaceCleanupToOwner(std::unique_ptr<ChildSurfaceState> &state) noexcept
    {
        if (!state || !state->platform)
            return false;
        std::scoped_lock registryLock(dispatcherRegistryMutex);
        const auto found = dispatcherRegistry.find(state->platform->ownerThreadId);
        Dispatcher *owner = found == dispatcherRegistry.end() ? nullptr : found->second;
        if (owner == nullptr)
            return false;
        {
            std::scoped_lock lock(owner->deferredMutex);
            state->deferredCleanupNext = owner->deferredChildCleanupHead.release();
            owner->deferredChildCleanupHead = std::move(state);
        }
        static_cast<void>(PostThreadMessageW(owner->threadId, wakeMessage(), 0, 0));
        return true;
    }

} // namespace GameWIP::Desktop::Detail::Platform
