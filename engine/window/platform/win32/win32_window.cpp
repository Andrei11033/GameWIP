/// @file win32_window.cpp
/// @brief Win32 Window lifecycle, dispatcher, native callback, and interoperability adapter.

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

        [[nodiscard]] IO::Types::Status acquireWindowClass(HINSTANCE instance) noexcept
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

        [[nodiscard]] IO::Types::Status releaseWindowClass() noexcept
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
                    routeEvent(*candidate, Types::OwnerChangedEvent{removed, {}});
                }
            }
        }

        [[nodiscard]] DWORD styleFor(const WindowState &state) noexcept
        {
            DWORD style = WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
            if (state.mode != Types::WindowMode::Windowed)
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

        [[nodiscard]] DWORD extendedStyleFor(const WindowState &state) noexcept
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

        [[nodiscard]] bool setLong(HWND window, int index, LONG_PTR value, DWORD &nativeCode) noexcept
        {
            SetLastError(ERROR_SUCCESS);
            const LONG_PTR previous = SetWindowLongPtrW(window, index, value);
            nativeCode = GetLastError();
            return previous != 0 || nativeCode == ERROR_SUCCESS;
        }

        void emitGeometryChanges(
            WindowState &state,
            Types::ScreenPosition previousPosition,
            Types::LogicalSize previousClient,
            Types::PixelSize previousFramebuffer) noexcept
        {
            if (state.clientPosition != previousPosition)
            {
                routeEvent(state, Types::MovedEvent{state.clientPosition});
            }
            if (state.clientSize != previousClient)
            {
                routeEvent(state, Types::ClientSizeChangedEvent{state.clientSize});
            }
            if (state.framebufferSize != previousFramebuffer)
            {
                routeEvent(state, Types::FramebufferSizeChangedEvent{state.framebufferSize});
            }
        }

        [[nodiscard]] LRESULT resizeHitTest(WindowState &state, POINT screenPoint) noexcept
        {
            if (!state.resizable || state.presentation != Types::PresentationState::Normal || state.mode != Types::WindowMode::Windowed)
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

    Dispatcher &dispatcher() noexcept
    {
        return threadDispatcher;
    }

    UINT wakeMessage() noexcept
    {
        static const UINT value = RegisterWindowMessageW(L"GameWIP.Window.WakeEventWait");
        return value;
    }

    void routeEvent(WindowState &state, Types::EventData data) noexcept
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
        if ((state.pointerHitMaskActiveGeneration != 0 && state.pointerHitMaskSize != state.framebufferSize) ||
            (state.pointerHitMaskTargetGeneration != 0 && state.pointerHitMaskTargetSize != state.framebufferSize))
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
        return IO::successStatus();
    }

    void updateCurrentMonitor(WindowState &state) noexcept
    {
        if (!state.platform || state.platform->handle == nullptr)
            return;
        HMONITOR native = MonitorFromWindow(state.platform->handle, MONITOR_DEFAULTTONEAREST);
        Types::MonitorInfoResult info = monitorFromNative(native);
        if (!info.status.ok())
        {
            recordPumpFailure(std::move(info.status));
            return;
        }
        if (info.monitor.id != state.monitor)
        {
            const Types::MonitorId previous = state.monitor;
            state.monitor = info.monitor.id;
            routeEvent(state, Types::MonitorChangedEvent{previous, state.monitor});
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

    IO::Types::Status leaveExclusive(WindowState &state) noexcept
    {
        if (state.platform && state.mode != Types::WindowMode::Windowed && Detail::consumeFailure(TestHooks::FailurePoint::DisplayRestoration))
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
        if (!state.platform || !state.platform->hasSavedDisplayMode || state.platform->exclusiveSuspended)
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
        if (!state.platform || !state.platform->hasSavedDisplayMode || !state.platform->exclusiveSuspended)
            return IO::successStatus();
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
                static_cast<void>(Detail::requestClose(*state, Types::CloseRequestSource::User));
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
                routeEvent(*state, Types::VisibilityChangedEvent{visible});
                const IO::Types::Status cursorStatus = applyCursorState(*state);
                if (!cursorStatus.ok())
                    recordPumpFailure(cursorStatus);
            }
            break;
        }
        case WM_SETFOCUS:
            if (const IO::Types::Status fullscreenStatus = resumeExclusive(*state); !fullscreenStatus.ok())
                recordPumpFailure(fullscreenStatus);
            if (!state->focused)
            {
                state->focused = true;
                routeEvent(*state, Types::FocusChangedEvent{true});
                const IO::Types::Status cursorStatus = applyCursorState(*state);
                if (!cursorStatus.ok())
                    recordPumpFailure(cursorStatus);
            }
            return 0;
        case WM_KILLFOCUS:
            if (state->focused)
            {
                state->focused = false;
                routeEvent(*state, Types::FocusChangedEvent{false});
                const IO::Types::Status cursorStatus = applyCursorState(*state);
                if (!cursorStatus.ok())
                    recordPumpFailure(cursorStatus);
            }
            if (const IO::Types::Status fullscreenStatus = suspendExclusive(*state); !fullscreenStatus.ok())
                recordPumpFailure(fullscreenStatus);
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
                routeEvent(*state, Types::PresentationStateChangedEvent{state->presentation});
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
                        Types::ContentScaleChangedEvent{previousScale, state->contentScale, previousDpi, state->dpi, state->framebufferSize});
                }
            }
            updateCurrentMonitor(*state);
            return 0;
        }
        case WM_GETMINMAXINFO:
        {
            auto *info = reinterpret_cast<MINMAXINFO *>(lParam);
            if (info == nullptr || state->mode != Types::WindowMode::Windowed)
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
            if (!state->aspectRatio || state->mode != Types::WindowMode::Windowed)
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
                    routeEvent(*state, Types::CursorPresenceChangedEvent{true});
                }
            }
            return DefWindowProcW(window, message, wParam, lParam);
        case WM_MOUSELEAVE:
            if (state->cursorInside)
            {
                state->cursorInside = false;
                routeEvent(*state, Types::CursorPresenceChangedEvent{false});
            }
            return 0;
        case WM_DROPFILES:
        {
            HDROP drop = reinterpret_cast<HDROP>(wParam);
            if (drop == nullptr)
                return 0;
            try
            {
                Types::FilesDroppedEvent event;
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
            routeEvent(*state, Types::RedrawRequestedEvent{});
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
                    routeEvent(*state, Types::ClosedEvent{});
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
                std::scoped_lock lock(windowRegistryMutex);
                const auto owner = windowRegistry.find(description.owner.value);
                if (owner == windowRegistry.end() || owner->second == nullptr || !owner->second->platform ||
                    owner->second->platform->ownerThreadId != state.platform->ownerThreadId)
                {
                    return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
                }
                ownerHandle = owner->second->platform->handle;
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
            state.mode = Types::WindowMode::Windowed;
            state.fullscreen = {};
            status = applyMode(state, description.mode);
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

    bool isOwnedByCurrentThread(const WindowState &state) noexcept
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

    Types::EventPumpResult pumpEvents(std::chrono::milliseconds timeout, bool wait) noexcept
    {
        Dispatcher &current = dispatcher();
        pruneAbandonedStates(current);
        Types::EventPumpResult result;
        if (current.windows.empty())
            return result;
        if (current.pumping)
        {
            result.status = IO::makeStatus(IO::Types::ErrorCode::ResourceBusy);
            return result;
        }
        if (Detail::consumeFailure(TestHooks::FailurePoint::EventPump))
        {
            result.status = IO::makeStatus(IO::Types::ErrorCode::NativeFailure);
            return result;
        }

        current.pumping = true;
        current.activeResult = &result;
        if (wait)
        {
            DWORD milliseconds = INFINITE;
            if (timeout != Events::kWaitForever)
            {
                const auto maximum = static_cast<std::int64_t>(INFINITE - 1);
                milliseconds = static_cast<DWORD>(std::min<std::int64_t>(timeout.count(), maximum));
            }
            const DWORD waitResult = MsgWaitForMultipleObjectsEx(0, nullptr, milliseconds, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            if (waitResult == WAIT_TIMEOUT)
            {
                result.timedOut = true;
                current.activeResult = nullptr;
                current.pumping = false;
                return result;
            }
            if (waitResult == WAIT_FAILED)
            {
                result.status = statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "MsgWaitForMultipleObjectsEx");
                current.activeResult = nullptr;
                current.pumping = false;
                return result;
            }
        }

        bool receivedDisplayChange = false;
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE)
        {
            if (message.message == wakeMessage())
                continue;
            if (message.message == WM_QUIT)
            {
                for (WindowState *state : current.windows)
                {
                    if (state != nullptr)
                        static_cast<void>(Detail::requestClose(*state, Types::CloseRequestSource::System));
                }
                continue;
            }
            if (message.message == WM_DISPLAYCHANGE)
                receivedDisplayChange = true;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        const bool displayColorChanged = consumeDisplayColorConfigurationChange();
        if (!receivedDisplayChange && displayColorChanged)
        {
            for (WindowState *state : current.windows)
            {
                if (state != nullptr)
                    routeEvent(*state, Types::DisplayConfigurationChangedEvent{});
            }
        }

        for (WindowState *state : current.windows)
        {
            if (state != nullptr && state->platform && state->cursorMode == Types::CursorMode::Relative && state->focused)
            {
                IO::Types::Status cursorStatus = applyCursorState(*state);
                if (!cursorStatus.ok() && result.status.ok())
                    result.status = std::move(cursorStatus);
            }
        }
        current.activeResult = nullptr;
        current.pumping = false;
        return result;
    }
} // namespace GameWIP::Window::Detail::Platform

namespace GameWIP::Window::Native::Win32
{
    HandleResult getHandle(const GameWIP::Window::Window &window) noexcept
    {
        const Detail::WindowState *state = Detail::WindowAccess::state(window);
        if (state == nullptr || !state->platform)
            return {.status = IO::makeStatus(IO::Types::ErrorCode::NotOpen)};
        if (!Detail::Platform::isOwnedByCurrentThread(*state))
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
        if (!Detail::Platform::isOwnedByCurrentThread(*state))
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
        if (!Detail::Platform::isOwnedByCurrentThread(*state))
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
