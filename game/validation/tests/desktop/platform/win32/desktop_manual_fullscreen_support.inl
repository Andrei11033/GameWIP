/// @file desktop_manual_fullscreen_support.inl
/// @brief Native Window state diagnostics for manual Desktop validation.

/// ------------------------------------------------------------
/// Fullscreen diagnostics
/// ------------------------------------------------------------

[[nodiscard]] std::wstring manualDiagnosticWideText(std::string_view text)
{
    if(text.empty())
    {
        return {};
    }
    const auto measurement = GameWIP::Unicode::Utf8::measureToUtf16(text);
    if(measurement.outcome != GameWIP::Unicode::Types::MeasureOutcome::Measured)
    {
        return L"?";
    }

    std::vector<char16_t> converted(measurement.requiredCodeUnits);
    const auto conversion = GameWIP::Unicode::Utf8::convertToUtf16(text, converted);
    if(conversion.outcome != GameWIP::Unicode::Types::ConversionOutcome::Converted)
    {
        return L"?";
    }
    std::wstring wide(conversion.codeUnitsWritten, L'\0');

    for(std::size_t index = 0; index < conversion.codeUnitsWritten; ++index)
    {
        wide[index] = static_cast<wchar_t>(converted[index]);
    }

    return wide;
}

/// @brief Native geometry and style observed independently from the portable Window cache.
struct ManualNativeWindowState
{
    // Snapshot status
    bool valid = false;

    // Native geometry
    RECT frame{};
    RECT client{};
    RECT monitor{};

    // Native style and scale
    DWORD style = 0;
    DWORD extendedStyle = 0;
    UINT dpi = 0;

    // Derived window state
    bool popupStyle = false;
    bool visibleStyle = false;
    bool fullscreenBounds = false;
    bool taskbarEligible = false;
};

/// @brief Selects independently repeatable parts of fullscreen and topology validation.
struct ManualFullscreenSections
{
    bool borderless = true;
    bool exclusive = true;
    bool topology = true;
};

/// @brief Captures actual HWND state so cached-state bugs remain visible to manual validation.
[[nodiscard]] ManualNativeWindowState manualNativeWindowState(const Desktop::Window &window)
{
    ManualNativeWindowState result;

    if(!window.isOpen())
    {
        return result;
    }
    const auto native = Desktop::Native::Win32::getHandle(window);
    if(!native.status.ok() || native->window == nullptr)
    {
        return result;
    }

    RECT client{};
    POINT clientTopLeft{};
    POINT clientBottomRight{};
    const HMONITOR monitor = MonitorFromWindow(native->window, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);

    if(GetWindowRect(native->window, &result.frame) == FALSE || GetClientRect(native->window, &client) == FALSE ||
       ClientToScreen(native->window, &clientTopLeft) == FALSE)
    {
        return result;
    }
    clientBottomRight = {client.right, client.bottom};

    if(ClientToScreen(native->window, &clientBottomRight) == FALSE || monitor == nullptr || GetMonitorInfoW(monitor, &monitorInfo) == FALSE)
    {
        return result;
    }
    result.client = {clientTopLeft.x, clientTopLeft.y, clientBottomRight.x, clientBottomRight.y};
    result.monitor = monitorInfo.rcMonitor;
    result.style = static_cast<DWORD>(GetWindowLongPtrW(native->window, GWL_STYLE));
    result.extendedStyle = static_cast<DWORD>(GetWindowLongPtrW(native->window, GWL_EXSTYLE));
    result.dpi = GetDpiForWindow(native->window);
    result.popupStyle = (result.style & static_cast<DWORD>(WS_POPUP)) != 0 &&
                        (result.style & (static_cast<DWORD>(WS_CAPTION) | static_cast<DWORD>(WS_THICKFRAME))) == 0;
    result.visibleStyle = (result.style & static_cast<DWORD>(WS_VISIBLE)) != 0;
    result.fullscreenBounds = EqualRect(&result.frame, &result.monitor) != FALSE && EqualRect(&result.client, &result.monitor) != FALSE;
    result.taskbarEligible = (result.extendedStyle & static_cast<DWORD>(WS_EX_APPWINDOW)) != 0 &&
                             (result.extendedStyle & static_cast<DWORD>(WS_EX_TOOLWINDOW)) == 0 && result.visibleStyle &&
                             IsWindowVisible(native->window) != FALSE && GetWindow(native->window, GW_OWNER) == nullptr;
    result.valid = true;
    return result;
}

/// @brief Persists a before/after fullscreen transition breadcrumb with cached and native geometry.
void recordManualModeTransition(
    TestSupport::Context &context,
    const Desktop::Window &window,
    std::string_view phase,
    const Desktop::Types::WindowModeRequest &request,
    const IO::Types::Status *status = nullptr)
{
    // Capture cached and native state for the transition record.
    const Desktop::Types::LogicalSize logical = window.clientSize();
    const Desktop::Types::PixelSize framebuffer = window.framebufferSize();
    const Desktop::Types::ScreenPosition position = window.clientPosition();
    const Desktop::Types::ContentScale scale = window.contentScale();
    const Desktop::Types::Dpi dpi = window.effectiveDpi();
    const Desktop::Types::FullscreenState fullscreen = window.fullscreenState();
    const ManualNativeWindowState native = manualNativeWindowState(window);

    // Describe the request and any result already available.
    const std::optional<Desktop::Types::Display::Mode> requestedMode = std::visit(
        [](const auto &value) -> std::optional<Desktop::Types::Display::Mode>
        {
            if constexpr(requires { value.displayMode; })
            {
                return value.displayMode;
            }
            else
            {
                return std::nullopt;
            }
        },
        request);
    const std::string requestedDisplayMode = requestedMode ? std::format(
                                                                 "{}x{}@{}mHz/{}bpp/interlaced={}",
                                                                 requestedMode->resolution.width,
                                                                 requestedMode->resolution.height,
                                                                 requestedMode->refreshRateMillihertz,
                                                                 requestedMode->bitsPerPixel,
                                                                 requestedMode->interlaced)
                                                           : std::string{"desktop"};
    const int requestMode = std::visit(
        [](const auto &value) noexcept
        {
            using Request = std::decay_t<decltype(value)>;
            if constexpr(std::is_same_v<Request, Desktop::Types::WindowedModeRequest>)
            {
                return 0;
            }
            else if constexpr(std::is_same_v<Request, Desktop::Types::BorderlessFullscreenRequest>)
            {
                return 1;
            }
            else
            {
                return 2;
            }
        },
        request);
    const std::string result = status == nullptr ? std::string{"pending"}
                                                 : std::format(
                                                       "portableCode={} nativeCode={} message={}",
                                                       static_cast<int>(status->code),
                                                       status->nativeCode,
                                                       status->message.empty() ? "<none>" : status->message);

    // Persist one diagnostic record for crash and regression review.
    context.info(
        std::format(
            "mode-transition phase={} tickMs={} requestMode={} requestMonitorId={} requestDisplay={} result={} "
            "cachedMode={} currentMonitorId={} fullscreenMonitorId={} suspended={} focused={} position=({}, {}) logical={}x{} framebuffer={}x{} "
            "scale=({:.2f}, {:.2f}) dpi=({:.1f}, {:.1f}) nativeValid={} nativeDpi={} frame=({}, {}) {}x{} client=({}, {}) {}x{} "
            "monitor=({}, {}) {}x{} popupStyle={} fullscreenBounds={}",
            phase,
            GetTickCount64(),
            requestMode,
            std::visit(
                [](const auto &value) noexcept
                {
                    if constexpr(requires { value.targetMonitor; })
                    {
                        return value.targetMonitor ? value.targetMonitor->value : std::uint64_t{0};
                    }
                    else
                    {
                        return std::uint64_t{0};
                    }
                },
                request),
            requestedDisplayMode,
            result,
            static_cast<int>(window.windowMode()),
            window.currentMonitor().value,
            fullscreen.monitor ? fullscreen.monitor->value : std::uint64_t{0},
            fullscreen.suspended,
            window.focused(),
            position.x,
            position.y,
            logical.width,
            logical.height,
            framebuffer.width,
            framebuffer.height,
            scale.x,
            scale.y,
            dpi.x,
            dpi.y,
            native.valid,
            native.dpi,
            native.frame.left,
            native.frame.top,
            native.frame.right - native.frame.left,
            native.frame.bottom - native.frame.top,
            native.client.left,
            native.client.top,
            native.client.right - native.client.left,
            native.client.bottom - native.client.top,
            native.monitor.left,
            native.monitor.top,
            native.monitor.right - native.monitor.left,
            native.monitor.bottom - native.monitor.top,
            native.popupStyle,
            native.fullscreenBounds));
}

/// @brief Applies a mode request while retaining crash-resilient transition evidence.
[[nodiscard]] IO::Types::Status setManualModeWithDiagnostics(
    TestSupport::Context &context,
    Desktop::Window &window,
    std::string_view label,
    const Desktop::Types::WindowModeRequest &request)
{
    // Record before and after the native transition.
    recordManualModeTransition(context, window, std::format("{}:before", label), request);
    IO::Types::Status status = window.setWindowMode(request);
    recordManualModeTransition(context, window, std::format("{}:after", label), request, &status);
    return status;
}
