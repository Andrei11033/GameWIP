/// @file win32_mode.cpp
/// @brief Transactional Win32 windowed and fullscreen mode transitions.

#include "window/platform/win32/internal/win32_window_backend.h"

#include <algorithm>
#include <new>

namespace GameWIP::Window::Detail::Platform
{
    namespace
    {
        class ModeTransitionScope
        {
        public:
            explicit ModeTransitionScope(WindowData &data) noexcept
                : data_(data)
            {
                ++data_.modeTransitionDepth;
            }

            ~ModeTransitionScope() noexcept
            {
                --data_.modeTransitionDepth;
            }

            ModeTransitionScope(const ModeTransitionScope &) = delete;
            ModeTransitionScope &operator=(const ModeTransitionScope &) = delete;

        private:
            WindowData &data_;
        };

        [[nodiscard]] HMONITOR targetMonitor(WindowState &state, Types::Display::MonitorId requested) noexcept
        {
            if (requested.isValid())
                return nativeMonitor(requested);
            if (state.platform && state.platform->handle)
                return MonitorFromWindow(state.platform->handle, MONITOR_DEFAULTTONEAREST);
            return MonitorFromPoint(POINT{}, MONITOR_DEFAULTTOPRIMARY);
        }

        [[nodiscard]] bool saveWindowedPlacement(WindowState &state) noexcept
        {
            WindowData &data = *state.platform;
            data.windowedPlacement = {};
            data.windowedPlacement.length = sizeof(WINDOWPLACEMENT);
            if (GetWindowPlacement(data.handle, &data.windowedPlacement) == FALSE)
                return false;
            data.windowedStyle = static_cast<DWORD>(GetWindowLongPtrW(data.handle, GWL_STYLE));
            data.windowedExtendedStyle = static_cast<DWORD>(GetWindowLongPtrW(data.handle, GWL_EXSTYLE));
            data.hasWindowedPlacement = true;
            return true;
        }

        [[nodiscard]] bool displayModeMatches(const DEVMODEW &native, const Types::Display::Mode &mode) noexcept
        {
            const std::uint32_t frequency = native.dmDisplayFrequency > 1 ? native.dmDisplayFrequency * 1000U : 0;
            return native.dmPelsWidth == mode.resolution.width && native.dmPelsHeight == mode.resolution.height &&
                   native.dmBitsPerPel == mode.bitsPerPixel && frequency == mode.refreshRateMillihertz &&
                   ((native.dmDisplayFlags & DM_INTERLACED) != 0) == mode.interlaced;
        }

        [[nodiscard]] IO::Types::Status findNativeMode(std::wstring_view device, const Types::Display::Mode &requested, DEVMODEW &output)
        {
            const std::wstring deviceName(device);
            for (DWORD index = 0;; ++index)
            {
                DEVMODEW candidate{};
                candidate.dmSize = sizeof(candidate);
                if (EnumDisplaySettingsExW(deviceName.c_str(), index, &candidate, EDS_RAWMODE) == FALSE)
                    break;
                if (displayModeMatches(candidate, requested))
                {
                    output = candidate;
                    return IO::successStatus();
                }
            }
            return IO::makeStatus(IO::Types::ErrorCode::InvalidArgument);
        }

        void reportModeChange(WindowState &state, Types::Mode previous) noexcept
        {
            if (previous != state.mode)
                routeEvent(state, Types::Events::ModeChanged{previous, state.mode});
            updateCurrentMonitor(state);
        }

        struct ModeSnapshot
        {
            Types::Mode mode = Types::Mode::Windowed;
            Types::FullscreenInfo fullscreen;
            DWORD style = 0;
            DWORD extendedStyle = 0;
            RECT rect{};
            std::wstring exclusiveDevice;
            DEVMODEW savedDisplayMode{};
            DEVMODEW activeNativeDisplayMode{};
            Types::Display::Mode activeDisplayMode;
            bool hasSavedDisplayMode = false;
            bool exclusiveSuspended = false;
            bool exactDisplayMode = false;
        };

        [[nodiscard]] ModeSnapshot captureModeSnapshot(WindowState &state, const RECT &rect)
        {
            WindowData &data = *state.platform;
            ModeSnapshot snapshot;
            snapshot.mode = state.mode;
            snapshot.fullscreen = state.fullscreen;
            snapshot.style = static_cast<DWORD>(GetWindowLongPtrW(data.handle, GWL_STYLE));
            snapshot.extendedStyle = static_cast<DWORD>(GetWindowLongPtrW(data.handle, GWL_EXSTYLE));
            snapshot.rect = rect;
            snapshot.exclusiveDevice = data.exclusiveDevice;
            snapshot.savedDisplayMode = data.savedDisplayMode;
            snapshot.activeNativeDisplayMode = data.activeNativeDisplayMode;
            snapshot.activeDisplayMode = data.activeDisplayMode;
            snapshot.hasSavedDisplayMode = data.hasSavedDisplayMode;
            snapshot.exclusiveSuspended = data.exclusiveSuspended;
            snapshot.exactDisplayMode = data.exactDisplayMode;
            return snapshot;
        }

        [[nodiscard]] IO::Types::Status restoreModeSnapshot(WindowState &state, ModeSnapshot &snapshot) noexcept
        {
            WindowData &data = *state.platform;
            IO::Types::Status rollback = IO::successStatus();

            if (data.hasSavedDisplayMode && !data.exclusiveSuspended)
            {
                const LONG result = ChangeDisplaySettingsExW(data.exclusiveDevice.c_str(), &data.savedDisplayMode, nullptr, 0, nullptr);
                if (result != DISP_CHANGE_SUCCESSFUL)
                    rollback = statusFromDisplayChange(result, "rollback requested exclusive display mode");
            }
            if (snapshot.hasSavedDisplayMode && !snapshot.exclusiveSuspended)
            {
                const LONG result =
                    ChangeDisplaySettingsExW(snapshot.exclusiveDevice.c_str(), &snapshot.activeNativeDisplayMode, nullptr, CDS_FULLSCREEN, nullptr);
                if (result != DISP_CHANGE_SUCCESSFUL)
                    rollback = statusFromDisplayChange(result, "restore previous exclusive display mode");
            }

            data.exclusiveDevice = std::move(snapshot.exclusiveDevice);
            data.savedDisplayMode = snapshot.savedDisplayMode;
            data.activeNativeDisplayMode = snapshot.activeNativeDisplayMode;
            data.activeDisplayMode = snapshot.activeDisplayMode;
            data.hasSavedDisplayMode = snapshot.hasSavedDisplayMode;
            data.exclusiveSuspended = snapshot.exclusiveSuspended;
            data.exactDisplayMode = snapshot.exactDisplayMode;
            state.mode = snapshot.mode;
            state.fullscreen = snapshot.fullscreen;

            SetLastError(ERROR_SUCCESS);
            if (SetWindowLongPtrW(data.handle, GWL_STYLE, snapshot.style) == 0 && GetLastError() != ERROR_SUCCESS)
                rollback = statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "restore previous window style");
            SetLastError(ERROR_SUCCESS);
            if (SetWindowLongPtrW(data.handle, GWL_EXSTYLE, snapshot.extendedStyle) == 0 && GetLastError() != ERROR_SUCCESS)
                rollback = statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "restore previous extended style");
            if (SetWindowPos(
                    data.handle,
                    nullptr,
                    snapshot.rect.left,
                    snapshot.rect.top,
                    snapshot.rect.right - snapshot.rect.left,
                    snapshot.rect.bottom - snapshot.rect.top,
                    SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED) == FALSE)
            {
                rollback = statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "restore previous window bounds");
            }
            const IO::Types::Status geometry = refreshCachedGeometry(state);
            if (!geometry.ok())
                return geometry;
            return rollback;
        }

        [[nodiscard]] IO::Types::Status failWithRollback(WindowState &state, ModeSnapshot &snapshot, IO::Types::Status failure) noexcept
        {
            IO::Types::Status rollback = restoreModeSnapshot(state, snapshot);
            return rollback.ok() ? std::move(failure) : std::move(rollback);
        }
    } // namespace

    IO::Types::Status placeFullscreenOnMonitor(WindowState &state, HMONITOR monitor, bool preserveZOrder) noexcept
    {
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (monitor == nullptr || GetMonitorInfoW(monitor, &info) == FALSE)
            return statusFromWin32(IO::Types::ErrorCode::NotFound, GetLastError(), "resolve fullscreen monitor");
        UINT flags = SWP_NOACTIVATE | SWP_FRAMECHANGED;
        if (preserveZOrder)
            flags |= SWP_NOZORDER;
        if (SetWindowPos(
                state.platform->handle,
                preserveZOrder ? nullptr : (state.alwaysOnTop ? HWND_TOPMOST : HWND_TOP),
                info.rcMonitor.left,
                info.rcMonitor.top,
                info.rcMonitor.right - info.rcMonitor.left,
                info.rcMonitor.bottom - info.rcMonitor.top,
                flags) == FALSE)
        {
            return statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "place fullscreen window");
        }
        return IO::successStatus();
    }

    IO::Types::Status applyMode(WindowState &state, const Types::ModeRequest &request) noexcept
    {
        if (!state.platform || state.platform->handle == nullptr)
            return IO::makeStatus(IO::Types::ErrorCode::NotOpen);
        try
        {
            WindowData &data = *state.platform;
            if (data.modeTransitionDepth != 0)
                return IO::makeStatus(IO::Types::ErrorCode::ResourceBusy, ERROR_BUSY, "a native window mode transition is already active");
            const Types::Mode previousMode = state.mode;
            if (previousMode == Types::Mode::Windowed && request.mode == Types::Mode::Windowed)
                return IO::successStatus();

            HMONITOR monitor = nullptr;
            Types::Display::InfoResult monitorInfo;
            std::wstring requestedDevice;
            DEVMODEW requestedNativeMode{};
            bool hasRequestedNativeMode = false;
            if (request.mode != Types::Mode::Windowed)
            {
                monitor = targetMonitor(state, request.monitor);
                monitorInfo = monitorFromNative(monitor);
                if (!monitorInfo.status.ok())
                    return monitorInfo.status;
                requestedDevice = monitorDeviceName(monitorInfo.monitor.id);
                if (requestedDevice.empty())
                    return IO::makeStatus(IO::Types::ErrorCode::NotFound);
                if (request.mode == Types::Mode::ExclusiveFullscreen && request.displayMode)
                {
                    IO::Types::Status validation = findNativeMode(requestedDevice, *request.displayMode, requestedNativeMode);
                    if (!validation.ok())
                        return validation;
                    hasRequestedNativeMode = true;
                }

                if (previousMode == request.mode && state.fullscreen.monitor == monitorInfo.monitor.id && !state.fullscreen.suspended)
                {
                    if (request.mode == Types::Mode::ExclusiveFullscreen &&
                        ((request.displayMode && state.fullscreen.exactDisplayMode && state.fullscreen.displayMode == request.displayMode) ||
                         (!request.displayMode && !state.fullscreen.exactDisplayMode)))
                    {
                        return IO::successStatus();
                    }
                }
            }

            RECT previousRect{};
            if (GetWindowRect(data.handle, &previousRect) == FALSE)
                return statusFromWin32(IO::Types::ErrorCode::StatFailed, GetLastError(), "snapshot window mode");

            if (previousMode == Types::Mode::Windowed && request.mode != Types::Mode::Windowed && !saveWindowedPlacement(state))
            {
                return statusFromWin32(IO::Types::ErrorCode::StatFailed, GetLastError(), "GetWindowPlacement");
            }
            ModeSnapshot snapshot = captureModeSnapshot(state, previousRect);
            ModeTransitionScope transition(data);

            if (request.mode == Types::Mode::Windowed)
            {
                IO::Types::Status status = leaveExclusive(state);
                if (!status.ok())
                    return status;
                state.mode = Types::Mode::Windowed;
                state.fullscreen = {};
                status = applyStyle(state);
                if (!status.ok())
                    return failWithRollback(state, snapshot, std::move(status));
                if (data.hasWindowedPlacement && SetWindowPlacement(data.handle, &data.windowedPlacement) == FALSE)
                    return failWithRollback(
                        state,
                        snapshot,
                        statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "SetWindowPlacement"));
                reportModeChange(state, previousMode);
                return refreshCachedGeometry(state);
            }

            IO::Types::Status status = leaveExclusive(state);
            if (!status.ok())
                return status;

            if (request.mode == Types::Mode::ExclusiveFullscreen)
            {
                DEVMODEW current{};
                current.dmSize = sizeof(current);
                if (EnumDisplaySettingsExW(requestedDevice.c_str(), ENUM_CURRENT_SETTINGS, &current, 0) == FALSE)
                    return failWithRollback(
                        state,
                        snapshot,
                        statusFromWin32(IO::Types::ErrorCode::StatFailed, GetLastError(), "query desktop display mode"));

                DEVMODEW desired{};
                Types::Display::Mode active{};
                const bool exact = hasRequestedNativeMode;
                if (hasRequestedNativeMode)
                {
                    desired = requestedNativeMode;
                    active = *request.displayMode;
                }
                else
                {
                    desired = current;
                    active = {
                        .resolution = {current.dmPelsWidth, current.dmPelsHeight},
                        .refreshRateMillihertz = current.dmDisplayFrequency > 1 ? current.dmDisplayFrequency * 1000U : 0,
                        .bitsPerPixel = static_cast<std::uint16_t>(current.dmBitsPerPel),
                        .interlaced = (current.dmDisplayFlags & DM_INTERLACED) != 0};
                }

                data.exclusiveDevice = std::move(requestedDevice);
                const LONG validationResult = ChangeDisplaySettingsExW(data.exclusiveDevice.c_str(), &desired, nullptr, CDS_TEST, nullptr);
                if (validationResult != DISP_CHANGE_SUCCESSFUL)
                {
                    return failWithRollback(state, snapshot, statusFromDisplayChange(validationResult, "validate exclusive fullscreen display mode"));
                }
                const LONG displayResult = ChangeDisplaySettingsExW(data.exclusiveDevice.c_str(), &desired, nullptr, CDS_FULLSCREEN, nullptr);
                if (displayResult != DISP_CHANGE_SUCCESSFUL)
                    return failWithRollback(state, snapshot, statusFromDisplayChange(displayResult, "enter exclusive fullscreen"));
                data.savedDisplayMode = current;
                data.activeNativeDisplayMode = desired;
                data.hasSavedDisplayMode = true;
                data.exclusiveSuspended = false;
                data.activeDisplayMode = active;
                data.exactDisplayMode = exact;
                state.mode = request.mode;
                state.fullscreen = {monitorInfo.monitor.id, active, exact, false};
            }
            else
            {
                state.mode = Types::Mode::BorderlessFullscreen;
                state.fullscreen = {monitorInfo.monitor.id, std::nullopt, false, false};
            }

            status = applyStyle(state);
            if (status.ok() && Detail::consumeFailure(TestHooks::FailurePoint::FullscreenPartial))
                status = IO::makeStatus(IO::Types::ErrorCode::NativeFailure);
            if (status.ok())
                status = placeFullscreenOnMonitor(state, monitor);
            if (!status.ok())
                return failWithRollback(state, snapshot, std::move(status));
            reportModeChange(state, previousMode);
            return refreshCachedGeometry(state);
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

    IO::Types::Status setMode(WindowState &state, const Types::ModeRequest &request) noexcept
    {
        IO::Types::Status transition = applyMode(state, request);

        DWORD foregroundProcessId = 0;
        const HWND foreground = GetForegroundWindow();
        if (foreground != nullptr)
            static_cast<void>(GetWindowThreadProcessId(foreground, &foregroundProcessId));
        IO::Types::Status activation = foregroundProcessId == GetCurrentProcessId() ? resumeExclusive(state) : suspendExclusive(state);

        return transition.ok() ? std::move(activation) : std::move(transition);
    }

    IO::Types::Status recoverAfterDisplayChange(WindowState &state, bool forceRemovedMonitor) noexcept
    {
        if (!state.platform || state.platform->handle == nullptr)
            return IO::makeStatus(IO::Types::ErrorCode::NotOpen);

        const Types::Mode previousMode = state.mode;
        const Types::Display::MonitorId previousMonitor = state.monitor;
        const Types::ScreenPosition previousPosition = state.clientPosition;
        const Types::LogicalSize previousClient = state.clientSize;
        const Types::PixelSize previousFramebuffer = state.framebufferSize;
        const Types::ContentScale previousScale = state.contentScale;
        const Types::Dpi previousDpi = state.dpi;

        const bool fullscreenMonitorConnected = nativeMonitor(state.fullscreen.monitor) != nullptr;
        if (state.mode == Types::Mode::Windowed || (!forceRemovedMonitor && fullscreenMonitorConnected))
        {
            routeEvent(state, Types::Events::DisplayConfigurationChanged{});
            updateCurrentMonitor(state);
            return IO::successStatus();
        }

        HMONITOR primary = MonitorFromPoint(POINT{}, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO primaryInfo{};
        primaryInfo.cbSize = sizeof(primaryInfo);
        Types::Display::InfoResult portablePrimary = monitorFromNative(primary);
        if (primary == nullptr || GetMonitorInfoW(primary, &primaryInfo) == FALSE || !portablePrimary.status.ok())
        {
            state.fullscreen = {};
            state.mode = Types::Mode::Windowed;
            routeEvent(state, Types::Events::DisplayConfigurationChanged{});
            routeEvent(state, Types::Events::ModeChanged{previousMode, state.mode});
            return portablePrimary.status.ok()
                       ? statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "resolve primary monitor after removal")
                       : std::move(portablePrimary.status);
        }

        WindowData &data = *state.platform;
        if (data.modeTransitionDepth != 0)
        {
            routeEvent(state, Types::Events::DisplayConfigurationChanged{});
            return IO::successStatus();
        }
        ModeTransitionScope transition(data);
        RECT desired = data.hasWindowedPlacement ? data.windowedPlacement.rcNormalPosition
                                                 : RECT{
                                                       state.frameRect.position.x,
                                                       state.frameRect.position.y,
                                                       state.frameRect.position.x + static_cast<LONG>(state.frameRect.size.width),
                                                       state.frameRect.position.y + static_cast<LONG>(state.frameRect.size.height)};
        const LONG workWidth = std::max<LONG>(1, primaryInfo.rcWork.right - primaryInfo.rcWork.left);
        const LONG workHeight = std::max<LONG>(1, primaryInfo.rcWork.bottom - primaryInfo.rcWork.top);
        const LONG width = std::clamp<LONG>(desired.right - desired.left, 1, workWidth);
        const LONG height = std::clamp<LONG>(desired.bottom - desired.top, 1, workHeight);
        const LONG x = primaryInfo.rcWork.left + (workWidth - width) / 2;
        const LONG y = primaryInfo.rcWork.top + (workHeight - height) / 2;

        const bool previousSuppression = state.suppressEvents;
        state.suppressEvents = true;
        IO::Types::Status firstFailure = leaveExclusive(state);
        // A disconnected target can reject restoration because it no longer exists. The
        // topology change has already removed that mode, so the failed restore is not an
        // actionable pump error. Clear stale ownership and continue recovery.
        if (!fullscreenMonitorConnected && !firstFailure.ok())
            firstFailure = IO::successStatus();
        data.hasSavedDisplayMode = false;
        data.exclusiveSuspended = false;
        data.exclusiveDevice.clear();
        data.activeNativeDisplayMode = {};
        data.activeDisplayMode = {};
        data.exactDisplayMode = false;
        state.mode = Types::Mode::Windowed;
        state.fullscreen = {};
        state.presentation = Types::PresentationState::Normal;

        IO::Types::Status status = applyStyle(state);
        if (!status.ok() && firstFailure.ok())
            firstFailure = status;
        if (SetWindowPos(data.handle, state.alwaysOnTop ? HWND_TOPMOST : HWND_TOP, x, y, width, height, SWP_NOACTIVATE | SWP_FRAMECHANGED) == FALSE)
        {
            status = statusFromWin32(IO::Types::ErrorCode::NativeFailure, GetLastError(), "place recovered window on primary monitor");
            if (firstFailure.ok())
                firstFailure = status;
        }
        status = refreshCachedGeometry(state);
        if (!status.ok() && firstFailure.ok())
            firstFailure = status;
        state.monitor = portablePrimary.monitor.id;
        state.suppressEvents = previousSuppression;

        routeEvent(state, Types::Events::DisplayConfigurationChanged{});
        routeEvent(state, Types::Events::ModeChanged{previousMode, state.mode});
        if (previousMonitor != state.monitor)
            routeEvent(state, Types::Events::MonitorChanged{previousMonitor, state.monitor});
        if (previousPosition != state.clientPosition)
            routeEvent(state, Types::Events::ClientPositionChanged{state.clientPosition});
        if (previousClient != state.clientSize)
            routeEvent(state, Types::Events::ClientSizeChanged{state.clientSize});
        if (previousFramebuffer != state.framebufferSize)
            routeEvent(state, Types::Events::FramebufferSizeChanged{state.framebufferSize});
        if (previousScale != state.contentScale || previousDpi != state.dpi)
        {
            routeEvent(state, Types::Events::ContentScaleChanged{previousScale, state.contentScale, previousDpi, state.dpi, state.framebufferSize});
        }
        return firstFailure;
    }
} // namespace GameWIP::Window::Detail::Platform

#if WINDOW_INTERNAL_TEST_HOOKS
namespace GameWIP::Window::TestHooks
{
    bool exactNativeDisplayModeMatches(
        const Types::Display::Mode &requested,
        std::uint32_t width,
        std::uint32_t height,
        std::uint32_t frequencyHertz,
        std::uint16_t bitsPerPixel,
        bool isInterlaced) noexcept
    {
        DEVMODEW native{};
        native.dmSize = sizeof(native);
        native.dmPelsWidth = width;
        native.dmPelsHeight = height;
        native.dmDisplayFrequency = frequencyHertz;
        native.dmBitsPerPel = bitsPerPixel;
        native.dmDisplayFlags = isInterlaced ? DM_INTERLACED : 0;
        return Detail::Platform::displayModeMatches(native, requested);
    }
} // namespace GameWIP::Window::TestHooks
#endif
