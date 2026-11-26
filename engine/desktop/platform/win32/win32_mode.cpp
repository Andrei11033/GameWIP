/// @file win32_mode.cpp
/// @brief Transactional Win32 windowed and fullscreen mode transitions.

#include "desktop/internal/results.h"
#include "desktop/platform/win32/internal/win32_window_backend.h"
#include "desktop/platform/win32/internal/win32_display_config.h"

#include <algorithm>
#include <new>
#include <optional>
#include <type_traits>

namespace GameWIP::Desktop::Platform
{
    /// ------------------------------------------------------------
    /// Transition state and rollback
    /// ------------------------------------------------------------

    namespace
    {
        /// @brief Marks a mode transition so synchronous native callbacks do not compete for geometry.
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

        /// @brief Resolves an explicit monitor or falls back to the Window's nearest monitor.
        [[nodiscard]] HMONITOR targetMonitor(WindowState &state, Types::Display::MonitorId requested) noexcept
        {
            if(requested.isValid())
            {
                return nativeMonitor(requested);
            }

            if(state.platform && state.platform->handle)
            {
                return MonitorFromWindow(state.platform->handle, MONITOR_DEFAULTTONEAREST);
            }

            return MonitorFromPoint(POINT{}, MONITOR_DEFAULTTOPRIMARY);
        }

        struct ResolvedWindowModeRequest
        {
            Types::WindowMode mode = Types::WindowMode::Windowed;
            std::optional<Types::Display::MonitorId> targetMonitor;
            std::optional<Types::Display::Mode> displayMode;
        };

        [[nodiscard]] ResolvedWindowModeRequest resolveWindowModeRequest(const Types::WindowModeRequest &request) noexcept
        {
            return std::visit(
                [](const auto &value) noexcept
                {
                    using Request = std::decay_t<decltype(value)>;
                    if constexpr(std::is_same_v<Request, Types::WindowedModeRequest>)
                    {
                        return ResolvedWindowModeRequest{};
                    }
                    else if constexpr(std::is_same_v<Request, Types::BorderlessFullscreenRequest>)
                    {
                        return ResolvedWindowModeRequest{Types::WindowMode::BorderlessFullscreen, value.targetMonitor, std::nullopt};
                    }
                    else
                    {
                        return ResolvedWindowModeRequest{Types::WindowMode::ExclusiveFullscreen, value.targetMonitor, value.displayMode};
                    }
                },
                request);
        }

        /// @brief Saves windowed placement and styles before entering fullscreen.
        [[nodiscard]] GameWIP::Status::Types::Status saveWindowedPlacement(WindowState &state) noexcept
        {
            WindowData &data = *state.platform;
            WINDOWPLACEMENT placement{};
            placement.length = sizeof(WINDOWPLACEMENT);

            if(GetWindowPlacement(data.handle, &placement) == FALSE)
            {
                const DWORD nativeCode = GetLastError();
                return statusFromWin32(GameWIP::Status::Types::Code::StatFailed, nativeCode, "GetWindowPlacement");
            }

            LONG_PTR style = 0;
            LONG_PTR extendedStyle = 0;
            GameWIP::Status::Types::Status status = queryWindowLong(data.handle, GWL_STYLE, style, "GetWindowLongPtrW saved windowed style");

            if(!status.ok())
            {
                return status;
            }

            status = queryWindowLong(data.handle, GWL_EXSTYLE, extendedStyle, "GetWindowLongPtrW saved windowed extended style");

            if(!status.ok())
            {
                return status;
            }

            data.windowedPlacement = placement;
            data.windowedStyle = static_cast<DWORD>(style);
            data.windowedExtendedStyle = static_cast<DWORD>(extendedStyle);
            data.hasWindowedPlacement = true;
            return GameWIP::Status::Types::Status{};
        }

        /// @brief Compares a native display mode with the portable mode contract.
        [[nodiscard]] bool displayModeMatches(const DEVMODEW &native, const Types::Display::Mode &mode) noexcept
        {
            const std::uint32_t frequency = native.dmDisplayFrequency > 1 ? native.dmDisplayFrequency * 1000U : 0;
            return native.dmPelsWidth == mode.resolution.width && native.dmPelsHeight == mode.resolution.height &&
                   native.dmBitsPerPel == mode.bitsPerPixel && frequency == mode.refreshRateMillihertz &&
                   ((native.dmDisplayFlags & DM_INTERLACED) != 0) == mode.interlaced;
        }

        /// @brief Finds the exact native display mode requested by the caller.
        [[nodiscard]] GameWIP::Status::Types::Status findNativeMode(std::wstring_view device, const Types::Display::Mode &requested, DEVMODEW &output)
        {
            const std::wstring deviceName(device);

            for(DWORD index = 0;; ++index)
            {
                DEVMODEW candidate{};
                candidate.dmSize = sizeof(candidate);

                if(EnumDisplaySettingsExW(deviceName.c_str(), index, &candidate, EDS_RAWMODE) == FALSE)
                {
                    break;
                }

                if(displayModeMatches(candidate, requested))
                {
                    output = candidate;
                    return GameWIP::Status::Types::Status{};
                }
            }

            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::InvalidArgument);
        }

        /// @brief Publishes a mode transition and refreshes the current monitor identity.
        void reportModeChange(WindowState &state, Types::WindowMode previous) noexcept
        {
            if(previous != state.mode)
            {
                routeEvent(state, Types::Events::WindowModeChanged{previous, state.mode});
            }
            updateCurrentMonitor(state);
        }

        struct ModeSnapshot
        {
            Types::WindowMode mode = Types::WindowMode::Windowed;
            Types::FullscreenState fullscreen;
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

        /// @brief Captures every native and portable value needed for transactional rollback.
        [[nodiscard]] GameWIP::Status::Types::Status captureModeSnapshot(WindowState &state, const RECT &rect, ModeSnapshot &snapshot)
        {
            WindowData &data = *state.platform;
            snapshot = {};
            snapshot.mode = state.mode;
            snapshot.fullscreen = state.fullscreen;
            LONG_PTR style = 0;
            LONG_PTR extendedStyle = 0;
            GameWIP::Status::Types::Status status = queryWindowLong(data.handle, GWL_STYLE, style, "GetWindowLongPtrW mode style");

            if(!status.ok())
            {
                return status;
            }

            status = queryWindowLong(data.handle, GWL_EXSTYLE, extendedStyle, "GetWindowLongPtrW mode extended style");

            if(!status.ok())
            {
                return status;
            }

            snapshot.style = static_cast<DWORD>(style);
            snapshot.extendedStyle = static_cast<DWORD>(extendedStyle);
            snapshot.rect = rect;
            snapshot.exclusiveDevice = data.exclusiveDevice;
            snapshot.savedDisplayMode = data.savedDisplayMode;
            snapshot.activeNativeDisplayMode = data.activeNativeDisplayMode;
            snapshot.activeDisplayMode = data.activeDisplayMode;
            snapshot.hasSavedDisplayMode = data.hasSavedDisplayMode;
            snapshot.exclusiveSuspended = data.exclusiveSuspended;
            snapshot.exactDisplayMode = data.exactDisplayMode;
            return GameWIP::Status::Types::Status{};
        }

        /// @brief Restores a captured mode snapshot after a failed transition.
        [[nodiscard]] GameWIP::Status::Types::Status restoreModeSnapshot(WindowState &state, ModeSnapshot &snapshot) noexcept
        {
            WindowData &data = *state.platform;
            GameWIP::Status::Types::Status rollback = GameWIP::Status::Types::Status{};

            // Restore the display mode before restoring window state.
            if(data.hasSavedDisplayMode && !data.exclusiveSuspended)
            {
                const LONG result = ChangeDisplaySettingsExW(data.exclusiveDevice.c_str(), &data.savedDisplayMode, nullptr, 0, nullptr);

                if(result != DISP_CHANGE_SUCCESSFUL)
                {
                    rollback = statusFromDisplayChange(result, "rollback requested exclusive display mode");
                }
            }

            if(snapshot.hasSavedDisplayMode && !snapshot.exclusiveSuspended)
            {
                const LONG result =
                    ChangeDisplaySettingsExW(snapshot.exclusiveDevice.c_str(), &snapshot.activeNativeDisplayMode, nullptr, CDS_FULLSCREEN, nullptr);

                if(result != DISP_CHANGE_SUCCESSFUL)
                {
                    rollback = statusFromDisplayChange(result, "restore previous exclusive display mode");
                }
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
            const LONG_PTR previousStyle = SetWindowLongPtrW(data.handle, GWL_STYLE, snapshot.style);
            const DWORD styleError = GetLastError();

            if(previousStyle == 0 && styleError != ERROR_SUCCESS)
            {
                rollback = statusFromWin32(GameWIP::Status::Types::Code::NativeFailure, styleError, "restore previous window style");
            }
            SetLastError(ERROR_SUCCESS);
            const LONG_PTR previousExtendedStyle = SetWindowLongPtrW(data.handle, GWL_EXSTYLE, snapshot.extendedStyle);
            const DWORD extendedStyleError = GetLastError();

            if(previousExtendedStyle == 0 && extendedStyleError != ERROR_SUCCESS)
            {
                rollback = statusFromWin32(GameWIP::Status::Types::Code::NativeFailure, extendedStyleError, "restore previous extended style");
            }

            if(SetWindowPos(
                   data.handle,
                   nullptr,
                   snapshot.rect.left,
                   snapshot.rect.top,
                   snapshot.rect.right - snapshot.rect.left,
                   snapshot.rect.bottom - snapshot.rect.top,
                   SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED) == FALSE)
            {
                rollback = statusFromWin32(GameWIP::Status::Types::Code::NativeFailure, GetLastError(), "restore previous window bounds");
            }
            const GameWIP::Status::Types::Status geometry = refreshCachedGeometry(state);

            if(!geometry.ok())
            {
                return geometry;
            }

            return rollback;
        }

        /// @brief Returns the original transition failure unless rollback also failed.
        [[nodiscard]] GameWIP::Status::Types::Status failWithRollback(WindowState &state, ModeSnapshot &snapshot, GameWIP::Status::Types::Status failure) noexcept
        {
            GameWIP::Status::Types::Status rollback = restoreModeSnapshot(state, snapshot);
            return rollback.ok() ? std::move(failure) : std::move(rollback);
        }
    } // namespace

    /// ------------------------------------------------------------
    /// Fullscreen placement and mode transitions
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Status placeFullscreenOnMonitor(WindowState &state, HMONITOR monitor, bool preserveZOrder) noexcept
    {
        if(monitor == nullptr)
        {
            return statusFromWin32(GameWIP::Status::Types::Code::NotFound, ERROR_NOT_FOUND, "resolve fullscreen monitor");
        }

        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);

        if(GetMonitorInfoW(monitor, &info) == FALSE)
        {
            return statusFromWin32(GameWIP::Status::Types::Code::NotFound, GetLastError(), "resolve fullscreen monitor");
        }

        UINT flags = SWP_NOACTIVATE | SWP_FRAMECHANGED;

        if(preserveZOrder)
        {
            flags |= SWP_NOZORDER;
        }

        if(SetWindowPos(
               state.platform->handle,
               preserveZOrder ? nullptr : (state.alwaysOnTop ? HWND_TOPMOST : HWND_TOP),
               info.rcMonitor.left,
               info.rcMonitor.top,
               info.rcMonitor.right - info.rcMonitor.left,
               info.rcMonitor.bottom - info.rcMonitor.top,
               flags) == FALSE)
        {
            return statusFromWin32(GameWIP::Status::Types::Code::NativeFailure, GetLastError(), "place fullscreen window");
        }

        return GameWIP::Status::Types::Status{};
    }

    GameWIP::Status::Types::Status applyWindowMode(WindowState &state, const Types::WindowModeRequest &request) noexcept
    {
        // Validate the Window and reject overlapping transitions.
        if(!state.platform || state.platform->handle == nullptr)
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::NotOpen);
        }

        try
        {
            WindowData &data = *state.platform;

            if(data.modeTransitionDepth != 0)
            {
                return GameWIP::Desktop::Internal::statusFor(
                    GameWIP::Status::Types::Code::ResourceBusy,
                    ERROR_BUSY,
                    GameWIP::Desktop::Internal::NativeCodeDomain::Win32Error,
                    "a native window mode transition is already active");
            }
            const ResolvedWindowModeRequest resolved = resolveWindowModeRequest(request);
            const Types::WindowMode previousMode = state.mode;

            if(previousMode == Types::WindowMode::Windowed && resolved.mode == Types::WindowMode::Windowed)
            {
                return GameWIP::Status::Types::Status{};
            }

            // Resolve the target monitor and validate any requested display mode.
            HMONITOR monitor = nullptr;
            GameWIP::Status::Types::Result<Types::Display::Info> monitorInfo =
                GameWIP::Status::Types::Result<Types::Display::Info>::failure(GameWIP::Status::Types::Status(GameWIP::Status::Types::Code::NotFound));
            std::wstring requestedDevice;
            DEVMODEW requestedNativeMode{};
            bool hasRequestedNativeMode = false;

            if(resolved.mode != Types::WindowMode::Windowed)
            {
                monitor = targetMonitor(state, resolved.targetMonitor.value_or(Types::Display::MonitorId::invalid()));
                monitorInfo = monitorFromNative(monitor);

                if(!monitorInfo.ok())
                {
                    return monitorInfo.status();
                }
                requestedDevice = monitorDeviceName(monitorInfo.value().id);

                if(requestedDevice.empty())
                {
                    return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::NotFound);
                }

                if(resolved.mode == Types::WindowMode::ExclusiveFullscreen && resolved.displayMode)
                {
                    GameWIP::Status::Types::Status validation = findNativeMode(requestedDevice, *resolved.displayMode, requestedNativeMode);

                    if(!validation.ok())
                    {
                        return validation;
                    }
                    hasRequestedNativeMode = true;
                }

                if(previousMode == resolved.mode && state.fullscreen.monitor == monitorInfo.value().id && !state.fullscreen.suspended)
                {
                    if(resolved.mode == Types::WindowMode::ExclusiveFullscreen &&
                       ((resolved.displayMode && state.fullscreen.displayModeMatchedExactly &&
                         state.fullscreen.requestedDisplayMode == resolved.displayMode) ||
                        (!resolved.displayMode && !state.fullscreen.displayModeMatchedExactly)))
                    {
                        return GameWIP::Status::Types::Status{};
                    }
                }
            }

            // Save enough native and portable state to roll back partial changes.
            RECT previousRect{};

            if(GetWindowRect(data.handle, &previousRect) == FALSE)
            {
                return statusFromWin32(GameWIP::Status::Types::Code::StatFailed, GetLastError(), "snapshot window mode");
            }

            if(previousMode == Types::WindowMode::Windowed && resolved.mode != Types::WindowMode::Windowed)
            {
                GameWIP::Status::Types::Status placementStatus = saveWindowedPlacement(state);

                if(!placementStatus.ok())
                {
                    return placementStatus;
                }
            }

            ModeSnapshot snapshot;
            GameWIP::Status::Types::Status snapshotStatus = captureModeSnapshot(state, previousRect, snapshot);

            if(!snapshotStatus.ok())
            {
                return snapshotStatus;
            }
            ModeTransitionScope transition(data);

            // Apply the requested windowed or fullscreen state.
            if(resolved.mode == Types::WindowMode::Windowed)
            {
                GameWIP::Status::Types::Status status = leaveExclusive(state);

                if(!status.ok())
                {
                    return status;
                }
                state.mode = Types::WindowMode::Windowed;
                state.fullscreen = {};
                status = applyStyle(state);

                if(!status.ok())
                {
                    return failWithRollback(state, snapshot, std::move(status));
                }

                if(data.hasWindowedPlacement && SetWindowPlacement(data.handle, &data.windowedPlacement) == FALSE)
                {
                    return failWithRollback(
                        state,
                        snapshot,
                        statusFromWin32(GameWIP::Status::Types::Code::NativeFailure, GetLastError(), "SetWindowPlacement"));
                }
                reportModeChange(state, previousMode);
                return refreshCachedGeometry(state);
            }

            GameWIP::Status::Types::Status status = leaveExclusive(state);

            if(!status.ok())
            {
                return status;
            }

            if(resolved.mode == Types::WindowMode::ExclusiveFullscreen)
            {
                DEVMODEW current{};
                current.dmSize = sizeof(current);

                if(EnumDisplaySettingsExW(requestedDevice.c_str(), ENUM_CURRENT_SETTINGS, &current, 0) == FALSE)
                {
                    return failWithRollback(
                        state,
                        snapshot,
                        statusFromWin32(GameWIP::Status::Types::Code::StatFailed, GetLastError(), "query desktop display mode"));
                }

                DEVMODEW desired{};
                Types::Display::Mode active{};
                const bool exact = hasRequestedNativeMode;

                if(hasRequestedNativeMode)
                {
                    desired = requestedNativeMode;
                    active = *resolved.displayMode;
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

                if(validationResult != DISP_CHANGE_SUCCESSFUL)
                {
                    return failWithRollback(state, snapshot, statusFromDisplayChange(validationResult, "validate exclusive fullscreen display mode"));
                }
                const LONG displayResult = ChangeDisplaySettingsExW(data.exclusiveDevice.c_str(), &desired, nullptr, CDS_FULLSCREEN, nullptr);

                if(displayResult != DISP_CHANGE_SUCCESSFUL)
                {
                    return failWithRollback(state, snapshot, statusFromDisplayChange(displayResult, "enter exclusive fullscreen"));
                }
                data.savedDisplayMode = current;
                data.activeNativeDisplayMode = desired;
                data.hasSavedDisplayMode = true;
                data.exclusiveSuspended = false;
                data.activeDisplayMode = active;
                data.exactDisplayMode = exact;
                state.mode = resolved.mode;
                state.fullscreen = {monitorInfo.value().id, resolved.displayMode, active, exact, false};
            }
            else
            {
                state.mode = Types::WindowMode::BorderlessFullscreen;
                state.fullscreen = {monitorInfo.value().id, std::nullopt, std::nullopt, false, false};
            }

            status = applyStyle(state);

            if(status.ok() && Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::FullscreenPartial))
            {
                status = GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::NativeFailure);
            }

            if(status.ok())
            {
                status = placeFullscreenOnMonitor(state, monitor);
            }

            if(!status.ok())
            {
                return failWithRollback(state, snapshot, std::move(status));
            }
            reportModeChange(state, previousMode);
            return refreshCachedGeometry(state);
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

    GameWIP::Status::Types::Status setWindowMode(WindowState &state, const Types::WindowModeRequest &request) noexcept
    {
        GameWIP::Status::Types::Status transition = applyWindowMode(state, request);

        DWORD foregroundProcessId = 0;
        const HWND foreground = GetForegroundWindow();

        if(foreground != nullptr)
        {
            static_cast<void>(GetWindowThreadProcessId(foreground, &foregroundProcessId));
        }
        GameWIP::Status::Types::Status activation = foregroundProcessId == GetCurrentProcessId() ? resumeExclusive(state) : suspendExclusive(state);

        return transition.ok() ? std::move(activation) : std::move(transition);
    }

    /// ------------------------------------------------------------
    /// Exclusive-mode cleanup
    /// ------------------------------------------------------------

    /// @brief Leaves exclusive mode and restores the saved desktop display mode.
    GameWIP::Status::Types::Status leaveExclusive(WindowState &state) noexcept
    {
        if(state.platform && state.mode != Types::WindowMode::Windowed &&
           Internal::consumeFailure(GameWIP::Desktop::Internal::TestHooks::FailurePoint::DisplayRestoration))
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::NativeFailure);
        }

        if(!state.platform || !state.platform->hasSavedDisplayMode)
        {
            state.fullscreen = {};
            return GameWIP::Status::Types::Status{};
        }
        WindowData &data = *state.platform;

        if(!data.exclusiveSuspended)
        {
            const LONG result = ChangeDisplaySettingsExW(data.exclusiveDevice.c_str(), &data.savedDisplayMode, nullptr, 0, nullptr);

            if(result != DISP_CHANGE_SUCCESSFUL)
            {
                return statusFromDisplayChange(result, "restore desktop display mode");
            }
        }
        data.hasSavedDisplayMode = false;
        data.exclusiveSuspended = false;
        data.exclusiveDevice.clear();
        data.activeDisplayMode = {};
        data.activeNativeDisplayMode = {};
        state.fullscreen = {};
        return GameWIP::Status::Types::Status{};
    }

    /// @brief Temporarily releases exclusive mode while another process is active.
    GameWIP::Status::Types::Status suspendExclusive(WindowState &state) noexcept
    {
        if(!state.platform || state.platform->modeTransitionDepth != 0 || !state.platform->hasSavedDisplayMode || state.platform->exclusiveSuspended)
        {
            return GameWIP::Status::Types::Status{};
        }

        const LONG result = ChangeDisplaySettingsExW(state.platform->exclusiveDevice.c_str(), &state.platform->savedDisplayMode, nullptr, 0, nullptr);

        if(result != DISP_CHANGE_SUCCESSFUL)
        {
            return statusFromDisplayChange(result, "suspend exclusive fullscreen");
        }

        state.platform->exclusiveSuspended = true;
        state.fullscreen.suspended = true;
        return GameWIP::Status::Types::Status{};
    }

    /// @brief Reclaims exclusive mode after this process becomes active again.
    GameWIP::Status::Types::Status resumeExclusive(WindowState &state) noexcept
    {
        if(!state.platform || state.platform->modeTransitionDepth != 0 || !state.platform->hasSavedDisplayMode || !state.platform->exclusiveSuspended)
        {
            return GameWIP::Status::Types::Status{};
        }

        const LONG validationResult =
            ChangeDisplaySettingsExW(state.platform->exclusiveDevice.c_str(), &state.platform->activeNativeDisplayMode, nullptr, CDS_TEST, nullptr);

        if(validationResult != DISP_CHANGE_SUCCESSFUL)
        {
            return statusFromDisplayChange(validationResult, "validate resumed exclusive fullscreen display mode");
        }

        const LONG result = ChangeDisplaySettingsExW(
            state.platform->exclusiveDevice.c_str(),
            &state.platform->activeNativeDisplayMode,
            nullptr,
            CDS_FULLSCREEN,
            nullptr);

        if(result != DISP_CHANGE_SUCCESSFUL)
        {
            return statusFromDisplayChange(result, "resume exclusive fullscreen");
        }

        state.platform->exclusiveSuspended = false;
        state.fullscreen.suspended = false;
        return GameWIP::Status::Types::Status{};
    }

    /// ------------------------------------------------------------
    /// Display-change recovery
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Status recoverAfterDisplayChange(WindowState &state, bool forceRemovedMonitor) noexcept
    {
        // Keep the current geometry and monitor state for change events.
        if(!state.platform || state.platform->handle == nullptr)
        {
            return GameWIP::Desktop::Internal::statusFor(GameWIP::Status::Types::Code::NotOpen);
        }
        const Types::WindowMode previousMode = state.mode;
        const Types::Display::MonitorId previousMonitor = state.monitor;
        const Types::ScreenPosition previousPosition = state.clientPosition;
        const Types::LogicalSize previousClient = state.clientSize;
        const Types::PixelSize previousFramebuffer = state.framebufferSize;
        const Types::ContentScale previousScale = state.contentScale;
        const Types::Dpi previousDpi = state.dpi;

        const bool fullscreenMonitorConnected = state.fullscreen.monitor && nativeMonitor(*state.fullscreen.monitor) != nullptr;

        if(state.mode == Types::WindowMode::Windowed || (!forceRemovedMonitor && fullscreenMonitorConnected))
        {
            routeEvent(state, Types::Events::DisplayConfigurationChanged{});
            updateCurrentMonitor(state);
            return GameWIP::Status::Types::Status{};
        }

        // Resolve the primary monitor as the fallback after losing fullscreen output.
        HMONITOR primary = MonitorFromPoint(POINT{}, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO primaryInfo{};
        primaryInfo.cbSize = sizeof(primaryInfo);
        GameWIP::Status::Types::Result<Types::Display::Info> portablePrimary = monitorFromNative(primary);
        GameWIP::Status::Types::Status monitorFailure = GameWIP::Status::Types::Status{};

        if(!portablePrimary.ok())
        {
            monitorFailure = portablePrimary.status();
        }
        else if(primary == nullptr)
        {
            monitorFailure = statusFromWin32(GameWIP::Status::Types::Code::NativeFailure, ERROR_NOT_FOUND, "resolve primary monitor after removal");
        }
        else if(GetMonitorInfoW(primary, &primaryInfo) == FALSE)
        {
            const DWORD error = GetLastError();
            monitorFailure = statusFromWin32(GameWIP::Status::Types::Code::NativeFailure, error, "resolve primary monitor after removal");
        }

        if(!monitorFailure.ok())
        {
            state.fullscreen = {};
            state.mode = Types::WindowMode::Windowed;
            routeEvent(state, Types::Events::DisplayConfigurationChanged{});
            routeEvent(state, Types::Events::WindowModeChanged{previousMode, state.mode});
            return monitorFailure;
        }

        WindowData &data = *state.platform;

        if(data.modeTransitionDepth != 0)
        {
            routeEvent(state, Types::Events::DisplayConfigurationChanged{});
            return GameWIP::Status::Types::Status{};
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

        // Clear exclusive state and move the Window to a usable primary-monitor area.
        GameWIP::Status::Types::Status firstFailure = leaveExclusive(state);
        // A disconnected target can reject restoration because it no longer exists. The
        // topology change has already removed that mode, so the failed restore is not an
        // actionable pump error. Clear stale ownership and continue recovery.
        if(!fullscreenMonitorConnected && !firstFailure.ok())
        {
            firstFailure = GameWIP::Status::Types::Status{};
        }
        data.hasSavedDisplayMode = false;
        data.exclusiveSuspended = false;
        data.exclusiveDevice.clear();
        data.activeNativeDisplayMode = {};
        data.activeDisplayMode = {};
        data.exactDisplayMode = false;
        state.mode = Types::WindowMode::Windowed;
        state.fullscreen = {};
        state.presentation = Types::PresentationState::Normal;

        if(state.presentationPublication != nullptr)
        {
            state.presentationPublication->publishPresentationState(state.presentation);
        }

        GameWIP::Status::Types::Status status = applyStyle(state);

        if(!status.ok() && firstFailure.ok())
        {
            firstFailure = status;
        }

        if(SetWindowPos(data.handle, state.alwaysOnTop ? HWND_TOPMOST : HWND_TOP, x, y, width, height, SWP_NOACTIVATE | SWP_FRAMECHANGED) == FALSE)
        {
            status = statusFromWin32(GameWIP::Status::Types::Code::NativeFailure, GetLastError(), "place recovered window on primary monitor");

            if(firstFailure.ok())
            {
                firstFailure = status;
            }
        }
        status = refreshCachedGeometry(state);

        if(!status.ok() && firstFailure.ok())
        {
            firstFailure = std::move(status);
        }
        state.monitor = portablePrimary.value().id;

        if(state.presentationPublication != nullptr)
        {
            state.presentationPublication->publishMonitor(state.monitor);
        }
        state.suppressEvents = previousSuppression;

        // Publish only the final recovered state.
        routeEvent(state, Types::Events::DisplayConfigurationChanged{});
        routeEvent(state, Types::Events::WindowModeChanged{previousMode, state.mode});

        if(previousMonitor != state.monitor)
        {
            routeEvent(state, Types::Events::MonitorChanged{previousMonitor, state.monitor});
        }

        if(previousPosition != state.clientPosition)
        {
            routeEvent(state, Types::Events::ClientPositionChanged{state.clientPosition});
        }

        if(previousClient != state.clientSize)
        {
            routeEvent(state, Types::Events::ClientSizeChanged{state.clientSize});
        }

        if(previousFramebuffer != state.framebufferSize)
        {
            routeEvent(state, Types::Events::FramebufferSizeChanged{state.framebufferSize});
        }

        if(previousScale != state.contentScale || previousDpi != state.dpi)
        {
            routeEvent(state, Types::Events::ContentScaleChanged{previousScale, state.contentScale, previousDpi, state.dpi, state.framebufferSize});
        }

        return firstFailure;
    }
} // namespace GameWIP::Desktop::Platform

#if DESKTOP_INTERNAL_TEST_HOOKS
namespace GameWIP::Desktop::Internal::TestHooks
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
        return Platform::displayModeMatches(native, requested);
    }
} // namespace GameWIP::Desktop::Internal::TestHooks
#endif
