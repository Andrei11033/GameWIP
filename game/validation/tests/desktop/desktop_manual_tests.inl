/// @file desktop_manual_tests.inl
/// @brief Optional manual Desktop validation helpers and cases.

[[nodiscard]] std::wstring manualDiagnosticWideText(std::string_view text)
{
    if (text.empty())
        return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0)
        return std::wstring(text.begin(), text.end());
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

/// @brief Native geometry and style observed independently from the portable Window cache.
struct ManualNativeWindowState
{
    bool valid = false;
    RECT frame{};
    RECT client{};
    RECT monitor{};
    DWORD style = 0;
    DWORD extendedStyle = 0;
    UINT dpi = 0;
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
    if (!window.isOpen())
        return result;
    const Desktop::Native::Win32::HandleResult native = Desktop::Native::Win32::getHandle(window);
    if (!native.status.ok() || native.handle.window == nullptr)
        return result;

    RECT client{};
    POINT clientTopLeft{};
    POINT clientBottomRight{};
    const HMONITOR monitor = MonitorFromWindow(native.handle.window, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (GetWindowRect(native.handle.window, &result.frame) == FALSE || GetClientRect(native.handle.window, &client) == FALSE ||
        ClientToScreen(native.handle.window, &clientTopLeft) == FALSE)
    {
        return result;
    }
    clientBottomRight = {client.right, client.bottom};
    if (ClientToScreen(native.handle.window, &clientBottomRight) == FALSE || monitor == nullptr || GetMonitorInfoW(monitor, &monitorInfo) == FALSE)
    {
        return result;
    }

    result.client = {clientTopLeft.x, clientTopLeft.y, clientBottomRight.x, clientBottomRight.y};
    result.monitor = monitorInfo.rcMonitor;
    result.style = static_cast<DWORD>(GetWindowLongPtrW(native.handle.window, GWL_STYLE));
    result.extendedStyle = static_cast<DWORD>(GetWindowLongPtrW(native.handle.window, GWL_EXSTYLE));
    result.dpi = GetDpiForWindow(native.handle.window);
    result.popupStyle = (result.style & WS_POPUP) != 0 && (result.style & (WS_CAPTION | WS_THICKFRAME)) == 0;
    result.visibleStyle = (result.style & WS_VISIBLE) != 0;
    result.fullscreenBounds = EqualRect(&result.frame, &result.monitor) != FALSE && EqualRect(&result.client, &result.monitor) != FALSE;
    result.taskbarEligible = (result.extendedStyle & WS_EX_APPWINDOW) != 0 && (result.extendedStyle & WS_EX_TOOLWINDOW) == 0 && result.visibleStyle &&
                             IsWindowVisible(native.handle.window) != FALSE && GetWindow(native.handle.window, GW_OWNER) == nullptr;
    result.valid = true;
    return result;
}

/// @brief Persists a before/after fullscreen transition breadcrumb with cached and native geometry.
void recordManualModeTransition(
    TestSupport::Context &context,
    const Desktop::Window &window,
    std::string_view phase,
    const Desktop::Types::ModeRequest &request,
    const IO::Types::Status *status = nullptr)
{
    const Desktop::Types::LogicalSize logical = window.clientSize();
    const Desktop::Types::PixelSize framebuffer = window.framebufferSize();
    const Desktop::Types::ScreenPosition position = window.clientPosition();
    const Desktop::Types::ContentScale scale = window.contentScale();
    const Desktop::Types::Dpi dpi = window.effectiveDpi();
    const Desktop::Types::FullscreenInfo fullscreen = window.fullscreenInfo();
    const ManualNativeWindowState native = manualNativeWindowState(window);
    const std::string requestedDisplayMode = request.displayMode ? std::format(
                                                                       "{}x{}@{}mHz/{}bpp/interlaced={}",
                                                                       request.displayMode->resolution.width,
                                                                       request.displayMode->resolution.height,
                                                                       request.displayMode->refreshRateMillihertz,
                                                                       request.displayMode->bitsPerPixel,
                                                                       request.displayMode->interlaced)
                                                                 : std::string{"desktop"};
    const std::string result = status == nullptr ? std::string{"pending"}
                                                 : std::format(
                                                       "portableCode={} nativeCode={} message={}",
                                                       static_cast<int>(status->code),
                                                       status->nativeCode,
                                                       status->message.empty() ? "<none>" : status->message);

    context.info(
        std::format(
            "mode-transition phase={} tickMs={} requestMode={} requestMonitorId={} requestDisplay={} result={} "
            "cachedMode={} currentMonitorId={} fullscreenMonitorId={} suspended={} focused={} position=({}, {}) logical={}x{} framebuffer={}x{} "
            "scale=({:.2f}, {:.2f}) dpi=({:.1f}, {:.1f}) nativeValid={} nativeDpi={} frame=({}, {}) {}x{} client=({}, {}) {}x{} "
            "monitor=({}, {}) {}x{} popupStyle={} fullscreenBounds={}",
            phase,
            GetTickCount64(),
            static_cast<int>(request.mode),
            request.monitor.value,
            requestedDisplayMode,
            result,
            static_cast<int>(window.mode()),
            window.currentMonitor().value,
            fullscreen.monitor.value,
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
    const Desktop::Types::ModeRequest &request)
{
    recordManualModeTransition(context, window, std::format("{}:before", label), request);
    IO::Types::Status status = window.setMode(request);
    recordManualModeTransition(context, window, std::format("{}:after", label), request, &status);
    return status;
}

/// @brief Read-only native companion Window that exposes live state during manual scenarios.
class ManualStatusWindow final
{
public:
    explicit ManualStatusWindow(bool enabled)
    {
        if (!enabled)
            return;
        RECT workArea{};
        if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0) == FALSE)
            workArea = {0, 0, 1920, 1080};
        constexpr int width = 680;
        constexpr int height = 520;
        const int x = std::max<int>(workArea.left, workArea.right - width - 20);
        const int y = workArea.top + 20;
        handle_ = CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            L"EDIT",
            L"GameWIP Window manual-test diagnostics",
            WS_OVERLAPPEDWINDOW | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
            x,
            y,
            width,
            height,
            nullptr,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr);
        if (handle_ != nullptr)
        {
            SendMessageW(handle_, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
            ShowWindow(handle_, SW_SHOWNOACTIVATE);
            SetWindowPos(handle_, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
    }

    ManualStatusWindow(const ManualStatusWindow &) = delete;
    ManualStatusWindow &operator=(const ManualStatusWindow &) = delete;

    ~ManualStatusWindow()
    {
        if (handle_ != nullptr)
            DestroyWindow(handle_);
    }

    void setScenario(std::string_view scenario, std::string_view expected)
    {
        scenario_ = scenario;
        expected_ = expected;
    }

    void setObservation(std::string_view observation)
    {
        observation_ = observation;
    }

    void refresh(const Desktop::Window &window)
    {
        if (handle_ == nullptr)
            return;

        const Desktop::Types::LogicalSize logical = window.clientSize();
        const Desktop::Types::PixelSize framebuffer = window.framebufferSize();
        const Desktop::Types::ScreenPosition position = window.clientPosition();
        const Desktop::Types::ContentScale scale = window.contentScale();
        const Desktop::Types::Dpi dpi = window.effectiveDpi();
        const Desktop::Types::Controls controls = window.controls();
        const Desktop::Types::FullscreenInfo fullscreen = window.fullscreenInfo();
        const ManualNativeWindowState native = manualNativeWindowState(window);
        const std::string text = std::format(
            "SCENARIO\r\n{}\r\n\r\nEXPECTED\r\n{}\r\n\r\nLATEST OBSERVATION\r\n{}\r\n\r\nLIVE WINDOW STATE\r\n"
            "open={} visible={} focused={} minimized={} maximized={} closeRequested={}\r\n"
            "position=({}, {}) logical={}x{} framebuffer={}x{}\r\n"
            "scale=({:.2f}, {:.2f}) dpi=({:.1f}, {:.1f}) monitorId={}\r\n"
            "mode={} (0=windowed, 1=borderless, 2=exclusive) presentation={} decoration={} opacity={:.2f}\r\n"
            "fullscreen: monitorId={} exactMode={} suspended={}\r\n"
            "cursorMode={} cursorShape={} pointerMode={}\r\n"
            "controls: close={} minimize={} maximize={} resizable={}\r\n"
            "ownerId={} dropsEnabled={} topmost={} interaction={} taskbarEligible={}\r\n\r\n"
            "NATIVE HWND STATE (independent of cache)\r\n"
            "valid={} dpi={} frame=({}, {}) {}x{} client=({}, {}) {}x{}\r\n"
            "monitor=({}, {}) {}x{} style=0x{:08X} exStyle=0x{:08X}\r\n"
            "popupStyle={} visibleStyle={} fullscreenBounds={}\r\n"
            "processAppId=GameWIP.Validation.DesktopManualTests\r\n\r\n"
            "The diagnostics refresh while the console prompt is waiting.",
            scenario_,
            expected_,
            observation_.empty() ? "No event-specific observation yet." : observation_,
            window.isOpen(),
            window.visible(),
            window.focused(),
            window.minimized(),
            window.maximized(),
            window.hasCloseRequest(),
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
            window.currentMonitor().value,
            static_cast<int>(window.mode()),
            static_cast<int>(window.presentationState()),
            static_cast<int>(window.decorationMode()),
            window.opacity(),
            fullscreen.monitor.value,
            fullscreen.exactDisplayMode,
            fullscreen.suspended,
            static_cast<int>(window.cursorMode()),
            static_cast<int>(window.cursorShape()),
            static_cast<int>(window.pointerInputMode()),
            controls.closable,
            controls.minimizable,
            controls.maximizable,
            window.resizable(),
            window.ownerId().value,
            window.fileDropEnabled(),
            window.alwaysOnTop(),
            window.userInteractionEnabled(),
            native.taskbarEligible,
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
            native.style,
            native.extendedStyle,
            native.popupStyle,
            native.visibleStyle,
            native.fullscreenBounds);
        if (text == lastText_)
            return;
        lastText_ = text;
        const std::wstring wide = manualDiagnosticWideText(text);
        SetWindowTextW(handle_, wide.c_str());
    }

private:
    HWND handle_ = nullptr;
    std::string scenario_;
    std::string expected_;
    std::string observation_;
    std::string lastText_;
};

ManualStatusWindow *manualStatusWindow = nullptr;

/// @brief Selects renderer-free artwork for a visible manual scenario.
enum class ManualSurfaceLayout : std::uint8_t
{
    Standard,
    CustomChromePrimary,
    CustomChromeReplacement
};

/// @brief Paints a renderer-free test surface across the complete native client area.
/// @details GameWIPTests does not attach a renderer, so resized or fullscreen HWND backing
/// pixels are otherwise undefined and can misleadingly retain only the old windowed area.
void paintManualValidationSurface(const Desktop::Window &window, ManualSurfaceLayout layout = ManualSurfaceLayout::Standard)
{
    if (!window.isOpen())
        return;
    const Desktop::Native::Win32::HandleResult native = Desktop::Native::Win32::getHandle(window);
    if (!native.status.ok() || native.handle.window == nullptr)
        return;

    RECT client{};
    if (GetClientRect(native.handle.window, &client) == FALSE)
        return;
    HDC device = GetDC(native.handle.window);
    if (device == nullptr)
        return;

    const UINT dpi = GetDpiForWindow(native.handle.window);
    const auto logical = [dpi](int value)
    {
        return MulDiv(value, static_cast<int>(dpi), 96);
    };
    const auto clippedRect = [&](int left, int top, int right, int bottom)
    {
        return RECT{
            std::clamp(static_cast<LONG>(logical(left)), client.left, client.right),
            std::clamp(static_cast<LONG>(logical(top)), client.top, client.bottom),
            std::clamp(static_cast<LONG>(logical(right)), client.left, client.right),
            std::clamp(static_cast<LONG>(logical(bottom)), client.top, client.bottom)};
    };
    const auto fill = [&](RECT rect, COLORREF color)
    {
        SetDCBrushColor(device, color);
        FillRect(device, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    };
    const auto drawText = [&](std::wstring_view text, RECT rect, int, COLORREF color, UINT format)
    {
        HGDIOBJ previousFont = SelectObject(device, GetStockObject(DEFAULT_GUI_FONT));
        SetBkMode(device, TRANSPARENT);
        SetTextColor(device, color);
        DrawTextW(device, text.data(), static_cast<int>(text.size()), &rect, format | DT_NOPREFIX);
        if (previousFont != nullptr)
            SelectObject(device, previousFont);
    };

    fill(client, RGB(22, 70, 126));
    if (layout == ManualSurfaceLayout::CustomChromePrimary)
    {
        RECT drag = clippedRect(0, 0, 760, 48);
        RECT gap = clippedRect(760, 0, 800, 48);
        RECT menu = clippedRect(0, 0, 48, 48);
        RECT minimize = clippedRect(800, 0, 848, 48);
        RECT maximize = clippedRect(848, 0, 896, 48);
        RECT close = clippedRect(896, 0, 960, 48);
        fill(drag, RGB(15, 112, 132));
        fill(gap, RGB(55, 65, 78));
        fill(menu, RGB(12, 84, 105));
        fill(minimize, RGB(55, 65, 78));
        fill(maximize, RGB(55, 65, 78));
        fill(close, RGB(182, 45, 55));
        drawText(L"MENU", clippedRect(14, 0, 48, 48), 11, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        drawText(L"DRAG AREA", drag, 20, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        drawText(L"MIN", minimize, 12, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        drawText(L"MAX", maximize, 12, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        drawText(L"CLOSE", close, 11, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        drawText(L"CUSTOM CHROME TEST", clippedRect(80, 150, 880, 190), 24, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        drawText(
            L"Drag the teal title region and use every visible system control.",
            clippedRect(80, 210, 880, 250),
            20,
            RGB(255, 255, 255),
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        drawText(
            L"Hover MAX for Snap Layout; resize from every cyan edge and corner.",
            clippedRect(80, 270, 880, 310),
            20,
            RGB(255, 255, 255),
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    else if (layout == ManualSurfaceLayout::CustomChromeReplacement)
    {
        RECT inactive = clippedRect(0, 0, 960, 48);
        RECT replacement = clippedRect(0, 48, 640, 88);
        fill(inactive, RGB(80, 48, 55));
        fill(replacement, RGB(15, 132, 102));
        drawText(L"OLD TOP STRIP - INACTIVE", inactive, 18, RGB(255, 210, 210), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        drawText(L"REPLACEMENT DRAG STRIP (y=48..87)", replacement, 17, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        drawText(
            L"Only the green replacement strip should move the Window.",
            clippedRect(100, 180, 860, 220),
            24,
            RGB(255, 255, 255),
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        drawText(
            L"The red old strip and every former control region must be inactive.",
            clippedRect(100, 240, 860, 280),
            20,
            RGB(255, 255, 255),
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    else
    {
        const LONG middle = client.top + (client.bottom - client.top) / 2;
        RECT title{client.left, middle - logical(38), client.right, middle - logical(4)};
        RECT subtitle{client.left, middle + logical(4), client.right, middle + logical(38)};
        drawText(L"GameWIP manual validation surface", title, 28, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        drawText(
            L"The blue surface and cyan inset border must cover the complete client area.",
            subtitle,
            22,
            RGB(255, 255, 255),
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    const int inset = std::max(4, MulDiv(10, static_cast<int>(dpi), 96));
    const int penWidth = std::max(2, MulDiv(6, static_cast<int>(dpi), 96));
    HPEN edgePen = CreatePen(PS_SOLID, penWidth, RGB(70, 210, 255));
    HGDIOBJ previousPen = edgePen != nullptr ? SelectObject(device, edgePen) : nullptr;
    HGDIOBJ previousBrush = SelectObject(device, GetStockObject(NULL_BRUSH));
    Rectangle(device, client.left + inset, client.top + inset, client.right - inset, client.bottom - inset);
    SelectObject(device, previousBrush);
    if (previousPen != nullptr)
        SelectObject(device, previousPen);
    if (edgePen != nullptr)
        DeleteObject(edgePen);

    ReleaseDC(native.handle.window, device);
}

template <typename Payload> [[nodiscard]] bool consumeEventOfType(Desktop::Window &window)
{
    bool found = false;
    Desktop::Types::Event event;
    while (window.popEvent(event))
    {
        found = found || event.getIf<Payload>() != nullptr;
    }
    return found;
}

/// @brief Records a manual response as a test pass, failure, or skip.
void recordManualAnswer(TestSupport::Context &context, std::string_view name, TestSupport::Types::Reporting::ManualAnswer answer)
{
    switch (answer)
    {
    case TestSupport::Types::Reporting::ManualAnswer::Yes:
        context.pass(name);
        return;
    case TestSupport::Types::Reporting::ManualAnswer::No:
        context.fail(name, "manual check rejected by user");
        return;
    case TestSupport::Types::Reporting::ManualAnswer::Skipped:
        context.skip(name, "manual check skipped by user");
        return;
    }
}

/// @brief Prompts on a worker while the owner thread keeps native Window messages flowing.
TestSupport::Types::Reporting::ManualAnswer recordManualCheck(
    TestSupport::Context &context,
    Desktop::Window &window,
    std::string_view name,
    std::string_view question,
    const std::function<void()> &observe = {},
    ManualSurfaceLayout surfaceLayout = ManualSurfaceLayout::Standard)
{
    if (manualStatusWindow != nullptr)
    {
        manualStatusWindow->setScenario(name, question);
        if (!observe)
            manualStatusWindow->setObservation({});
        paintManualValidationSurface(window, surfaceLayout);
        manualStatusWindow->refresh(window);
    }
    std::atomic<bool> answered = false;
    TestSupport::Types::Reporting::ManualAnswer answer = TestSupport::Types::Reporting::ManualAnswer::Skipped;
    std::jthread promptThread(
        [&]
        {
            answer = TestSupport::promptManualCheck(question);
            answered.store(true, std::memory_order_release);
            static_cast<void>(window.wakeEventWait());
        });

    IO::Types::Status pumpFailure;
    while (!answered.load(std::memory_order_acquire))
    {
        const Desktop::Types::Events::PumpResult pump = Desktop::Events::wait(std::chrono::milliseconds{50});
        if (!pump.status.ok() && pumpFailure.ok())
            pumpFailure = pump.status;
        if (observe)
            observe();
        paintManualValidationSurface(window, surfaceLayout);
        if (manualStatusWindow != nullptr)
            manualStatusWindow->refresh(window);
    }
    promptThread.join();
    const Desktop::Types::Events::PumpResult finalPump = Desktop::Events::poll();
    if (!finalPump.status.ok() && pumpFailure.ok())
        pumpFailure = finalPump.status;
    if (observe)
        observe();
    paintManualValidationSurface(window, surfaceLayout);
    if (manualStatusWindow != nullptr)
        manualStatusWindow->refresh(window);
    if (!pumpFailure.ok())
    {
        context.fail(name, pumpFailure.message);
        return TestSupport::Types::Reporting::ManualAnswer::No;
    }
    recordManualAnswer(context, name, answer);
    return answer;
}

/// @brief Reports a failed setup operation and returns whether the manual scenario may continue.
[[nodiscard]] bool requireManualStatus(TestSupport::Context &context, std::string_view name, const IO::Types::Status &status)
{
    if (status.ok())
        return true;
    if (manualStatusWindow != nullptr)
    {
        manualStatusWindow->setObservation(
            std::format(
                "Operation '{}' failed: portableCode={} nativeCode={} message={}",
                name,
                static_cast<int>(status.code),
                status.nativeCode,
                status.message));
    }
    context.fail(
        name,
        std::format(
            "portableCode={} nativeCode={} message={}",
            static_cast<int>(status.code),
            status.nativeCode,
            status.message.empty() ? "<none>" : status.message));
    return false;
}

/// @brief Opens a consistently sized visible Window for one manual scenario.
[[nodiscard]] bool openManualWindow(
    TestSupport::Context &context,
    Desktop::Window &window,
    std::string_view title,
    Desktop::Types::Description description = {})
{
    description.title = title;
    description.clientSize = {960, 540};
    description.visible = true;
    description.requestFocus = true;
    return requireManualStatus(context, "manual Window setup", window.open(description));
}

/// @brief Returns whether an opt-in suite should run and records the unattended skip otherwise.
[[nodiscard]] bool beginManualSuite(TestSupport::Context &context, const GameWIP::Test::DesktopTestOptions &options, std::string_view name)
{
    if (options.enableManualTests)
        return true;
    context.skip(name, "disabled by DesktopTestOptions");
    return false;
}

/// @brief Keeps owner-thread native messages flowing for a bounded manual preparation interval.
void pumpManualPreparation(std::chrono::milliseconds duration)
{
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < deadline)
        static_cast<void>(Desktop::Events::wait(std::chrono::milliseconds{50}));
}

/// @brief Exercises the core real visible-window lifecycle required before submission.
void testManualVisibleLifecycle(TestSupport::Context &context, const GameWIP::Test::DesktopTestOptions &options)
{
    if (!beginManualSuite(context, options, "Window visible lifecycle"))
        return;

    context.manual(
        "Window manual tests require a normal interactive Windows desktop. Answer yes, no, or skip at each prompt. "
        "Restore any changed desktop state before the suite ends.");

    Desktop::Types::Description description;
    description.title = "GameWIP Window manual validation";
    description.clientSize = {960, 540};
    description.visible = true;
    description.requestFocus = true;

    Desktop::Window window;
    const IO::Types::Status openStatus = window.open(description);
    if (!openStatus.ok())
    {
        context.fail("manual Window opens", openStatus.message);
        return;
    }
    context.pass("manual Window opens");

    recordManualCheck(
        context,
        window,
        "visible Window create, show, and focus",
        "Is the GameWIP validation Window visible, correctly sized, and focused or requesting attention as allowed by Windows?");
    recordManualCheck(
        context,
        window,
        "visible Window move and resize",
        "Move and resize the Window with the system frame. Do movement and edge/corner resizing behave normally?");

    static_cast<void>(context.expectTrue("manual Window maximize request succeeds", window.maximize().ok()));
    recordManualCheck(context, window, "visible Window maximize", "Did the Window maximize correctly?");
    static_cast<void>(context.expectTrue("manual Window restore request succeeds", window.restore().ok()));
    static_cast<void>(context.expectTrue("manual Window minimize request succeeds", window.minimize().ok()));
    recordManualCheck(context, window, "visible Window minimize", "Did the Window minimize correctly? Restore it from the taskbar before answering.");
    static_cast<void>(context.expectTrue("manual Window restore after minimize succeeds", window.restore().ok()));
    static_cast<void>(window.requestFocus());

    recordManualCheck(
        context,
        window,
        "system close request",
        "Click the Window close button once. Does the Window remain alive while recording a close request?");
    static_cast<void>(context.expectTrue("system close request becomes sticky", window.hasCloseRequest()));
    if (window.hasCloseRequest())
        static_cast<void>(context.expectTrue("system close request clears", window.clearCloseRequest().ok()));
    static_cast<void>(context.expectTrue("cleared close request leaves Window open", window.isOpen()));

    recordManualCheck(
        context,
        window,
        "second system close request",
        "Click the Window close button again. Does the Window again remain alive pending explicit close?");
    static_cast<void>(context.expectTrue("second system close request becomes sticky", window.hasCloseRequest()));
    static_cast<void>(context.expectTrue("manual Window explicit close succeeds", window.close().ok()));
    static_cast<void>(context.expectFalse("manual Window is closed", window.isOpen()));

    Desktop::Window reopened;
    description.visible = false;
    description.requestFocus = false;
    static_cast<void>(context.expectTrue("manual Window reopens after explicit close", reopened.open(description).ok()));
    static_cast<void>(context.expectTrue("reopened manual Window closes cleanly", reopened.close().ok()));
}

/// @brief Exercises visible multi-window ownership, routing, activation, and native relationship behavior.
void testManualMultipleWindows(TestSupport::Context &context, const GameWIP::Test::DesktopTestOptions &options)
{
    if (!beginManualSuite(context, options, "Window multiple-window scenarios"))
        return;

    Desktop::Window owner;
    if (!openManualWindow(context, owner, "GameWIP manual owner Window"))
        return;

    Desktop::Types::Description childDescription;
    childDescription.owner = owner.id();
    Desktop::Window child;
    if (!openManualWindow(context, child, "GameWIP manual owned tool Window", childDescription))
        return;
    const auto paintOwnedWindow = [&]
    {
        paintManualValidationSurface(child);
    };

    recordManualCheck(
        context,
        owner,
        "two independent queues",
        "Move, resize, and focus both Windows. Do both remain responsive and receive behavior independently?",
        paintOwnedWindow);
    recordManualCheck(
        context,
        owner,
        "owned Window native behavior",
        "Activate, minimize, and restore the owner and owned Window. Is their native z-order/minimization relationship stable?",
        paintOwnedWindow);

    static_cast<void>(context.expectTrue("owned Window owner removal succeeds", child.setOwner({}).ok()));
    recordManualCheck(
        context,
        owner,
        "owned Window owner removal",
        "After owner removal, does the tool Window behave as an independent taskbar Window?",
        paintOwnedWindow);
    static_cast<void>(context.expectTrue("owned Window owner restoration succeeds", child.setOwner(owner.id()).ok()));
    recordManualCheck(
        context,
        owner,
        "owned Window owner restoration",
        "After owner restoration, is the independent taskbar entry removed and native ownership restored?",
        paintOwnedWindow);

    static_cast<void>(context.expectTrue("owned Window closes", child.close().ok()));
    recordManualCheck(context, owner, "remaining Window after peer close", "Does the owner remain fully operational after the other Window closes?");

    static_cast<void>(context.expectTrue("manual owner hides", owner.hide().ok()));
    recordManualCheck(
        context,
        owner,
        "show-without-activation preparation",
        "Answer yes, then immediately focus another desktop application; the Window will be shown after a five-second preparation interval.");
    pumpManualPreparation(std::chrono::seconds{5});
    static_cast<void>(context.expectTrue("manual owner shows without activation", owner.show().ok()));
    recordManualCheck(context, owner, "show without activation", "Did show() leave the other application active instead of stealing focus?");
    const IO::Types::Status focusStatus = owner.requestFocus();
    recordManualCheck(
        context,
        owner,
        "explicit focus policy",
        focusStatus.ok() ? "After requestFocus(), did Windows either focus the Window or provide its normal attention indication?"
                         : "Windows rejected requestFocus(). Was the existing foreground application left stable?");
    static_cast<void>(context.expectTrue("manual owner closes", owner.close().ok()));

    context.pass("cross-thread destruction is covered by Window threading contracts");
    context.pass("unexpected native destruction is covered by Window exceptional lifetime");
    context.pass("owner-thread exit cleanup is covered by Window exceptional lifetime");
}

/// @brief Exercises custom non-client hit testing and runtime layout replacement.
void testManualCustomChrome(TestSupport::Context &context, const GameWIP::Test::DesktopTestOptions &options)
{
    if (!beginManualSuite(context, options, "Window custom chrome"))
        return;

    const Desktop::Types::Capabilities capabilities = Desktop::getCapabilities().capabilities;
    if (!capabilities.supports(Desktop::Types::Capability::CustomChrome))
    {
        context.skip("Window custom chrome", "backend does not advertise CustomChrome");
        return;
    }

    Desktop::Types::Description description;
    description.decoration = Desktop::Types::DecorationMode::Custom;
    Desktop::Window window;
    if (!openManualWindow(context, window, "GameWIP custom chrome validation", description))
        return;

    const std::array draggable{Desktop::Types::LogicalRect{{0, 0}, {760, 48}}};
    Desktop::Types::CustomChromeLayout layout;
    layout.draggableRegions = draggable;
    layout.systemMenuRegion = Desktop::Types::LogicalRect{{0, 0}, {48, 48}};
    layout.minimizeButtonRegion = Desktop::Types::LogicalRect{{800, 0}, {48, 48}};
    layout.maximizeButtonRegion = Desktop::Types::LogicalRect{{848, 0}, {48, 48}};
    layout.closeButtonRegion = Desktop::Types::LogicalRect{{896, 0}, {64, 48}};
    if (!requireManualStatus(context, "custom chrome layout applies", window.setCustomChromeLayout(layout)))
        return;

    recordManualCheck(
        context,
        window,
        "custom chrome controls and resize",
        "Using the top 48 logical pixels, test dragging and the system/minimize/maximize/close regions without accepting close. Also test every "
        "resize edge/corner and a snap layout. Do all native behaviors work?",
        {},
        ManualSurfaceLayout::CustomChromePrimary);

    const std::array replacement{Desktop::Types::LogicalRect{{0, 48}, {640, 40}}};
    layout.draggableRegions = replacement;
    layout.systemMenuRegion.reset();
    layout.minimizeButtonRegion.reset();
    layout.maximizeButtonRegion.reset();
    layout.closeButtonRegion.reset();
    static_cast<void>(context.expectTrue("replacement custom chrome layout applies", window.setCustomChromeLayout(layout).ok()));
    recordManualCheck(
        context,
        window,
        "custom chrome replacement",
        "Does only the replacement strip at logical y=48..87 drag, with every old top-strip region inactive immediately?",
        {},
        ManualSurfaceLayout::CustomChromeReplacement);
    recordManualCheck(
        context,
        window,
        "custom chrome DPI scales",
        "If 100%, 125%, 150%, or 200% displays are available, move the Window among them. Do chrome regions remain aligned? Skip if the topology "
        "is unavailable.",
        {},
        ManualSurfaceLayout::CustomChromeReplacement);

    static_cast<void>(context.expectTrue("custom chrome layout clears", window.clearCustomChromeLayout().ok()));
    static_cast<void>(context.expectTrue("custom chrome Window closes", window.close().ok()));
}

/// @brief Exercises opacity, compositor transparency, cross-application routing, and mask-visible behavior.
void testManualLayeredAndPointer(TestSupport::Context &context, const GameWIP::Test::DesktopTestOptions &options)
{
    if (!beginManualSuite(context, options, "Window layered and pointer behavior"))
        return;

    const Desktop::Types::Capabilities capabilities = Desktop::getCapabilities().capabilities;
    Desktop::Window window;
    if (!openManualWindow(context, window, "GameWIP opacity and pointer validation"))
        return;

    if (capabilities.supports(Desktop::Types::Capability::Opacity))
    {
        context.manual("Place another high-contrast application beneath the validation Window as the opacity and input reference.");
        for (const float opacity : {1.0F, 0.5F, 0.0F})
        {
            static_cast<void>(context.expectTrue("opacity transition succeeds", window.setOpacity(opacity).ok()));
            recordManualCheck(
                context,
                window,
                "opacity presentation and input",
                std::format(
                    "The diagnostics show cached opacity {:.1f}. Over the reference application, is the visual blend correct? At 0.0 the "
                    "validation "
                    "Window should be invisible but still block clicks; at 1.0 it should be opaque.",
                    opacity));
        }
        static_cast<void>(window.setOpacity(1.0F));
    }
    else
    {
        context.skip("opacity presentation and input", "backend does not advertise Opacity");
    }

    if (capabilities.supports(Desktop::Types::Capability::PointerClickThrough))
    {
        context.manual("Place another interactive application beneath the validation Window before continuing.");
        static_cast<void>(context.expectTrue(
            "whole-Window click-through applies",
            window.setPointerInputLayout({.mode = Desktop::Types::PointerInputMode::ClickThrough}).ok()));
        recordManualCheck(
            context,
            window,
            "cross-application click-through",
            "The diagnostics show pointerMode=1. Do clicks through both client and system-frame areas reach the different "
            "application underneath?");
        static_cast<void>(context.expectTrue("normal pointer routing restores", window.setPointerInputLayout({}).ok()));
        recordManualCheck(
            context,
            window,
            "normal pointer routing restoration",
            "The diagnostics show pointerMode=0. Do the validation Window title bar, resize border, and client now intercept input again?");
    }
    else
    {
        context.skip("cross-application click-through", "backend does not advertise PointerClickThrough");
    }

    const std::array region{Desktop::Types::LogicalRect{{20, 20}, {120, 80}}};
    for (const Desktop::Types::PointerInputMode mode :
         {Desktop::Types::PointerInputMode::AcceptRegions, Desktop::Types::PointerInputMode::IgnoreRegions})
    {
        Desktop::Types::PointerInputLayout regionLayout{.mode = mode, .regions = region};
        const IO::Types::Status status = window.setPointerInputLayout(regionLayout);
        if (capabilities.supports(Desktop::Types::Capability::PointerRegions))
        {
            static_cast<void>(context.expectTrue("supported pointer-region layout applies", status.ok()));
            recordManualCheck(
                context,
                window,
                "cross-application pointer regions",
                "Does the configured rectangle route input according to the requested region mode against another application?");
        }
        else
        {
            static_cast<void>(context.expectEq("unsupported pointer-region layout is rejected", ErrorCode::Unsupported, status.code));
        }
    }
    static_cast<void>(window.setPointerInputLayout({}));

    context.pass("pointer-mask first/last pixels, clearing, movement, resize invalidation, and stale generations are covered deterministically");
    static_cast<void>(context.expectTrue("layered pointer Window closes", window.close().ok()));

    Desktop::Types::Description alphaDescription;
    alphaDescription.transparentFramebuffer = true;
    Desktop::Window alpha;
    const IO::Types::Status alphaStatus = alpha.open(alphaDescription);
    if (capabilities.supports(Desktop::Types::Capability::TransparentFramebuffer))
    {
        static_cast<void>(context.expectTrue("transparent framebuffer Window opens", alphaStatus.ok()));
        if (alpha.isOpen())
        {
            static_cast<void>(alpha.show());
            context.skip(
                "transparent framebuffer presentation",
                "GameWIPTests has no renderer-provided alpha surface; run this observation in a renderer-backed host");
            static_cast<void>(alpha.close());
        }
    }
    else
    {
        static_cast<void>(context.expectEq("unsupported transparent framebuffer is rejected", ErrorCode::Unsupported, alphaStatus.code));
    }
}

/// @brief Exercises mixed-DPI policies, coordinate conversion, and physical monitor geometry.
void testManualDpiAndCoordinates(TestSupport::Context &context, const GameWIP::Test::DesktopTestOptions &options)
{
    if (!beginManualSuite(context, options, "Window DPI and coordinates"))
        return;

    Desktop::Window window;
    Desktop::Types::Description description;
    description.dpiResizePolicy = Desktop::Types::DpiResizePolicy::PreserveLogicalClientSize;
    if (!openManualWindow(context, window, "GameWIP mixed-DPI validation", description))
        return;

    recordManualCheck(
        context,
        window,
        "mixed-DPI logical-size policy",
        "With differently scaled monitors arranged on either side of primary when available, move the Window across boundaries. Does logical "
        "size remain stable while framebuffer pixels change?");

    static_cast<void>(context.expectTrue(
        "physical-size DPI policy applies",
        window.setDpiResizePolicy(Desktop::Types::DpiResizePolicy::PreservePhysicalClientSize).ok()));
    recordManualCheck(
        context,
        window,
        "mixed-DPI physical-size policy",
        "Move across the same DPI boundaries. Do framebuffer pixels remain stable while logical size changes?");

    const Desktop::Types::LogicalSize size = window.clientSize();
    const std::array points{
        Desktop::Types::LogicalPosition{0, 0},
        Desktop::Types::LogicalPosition{static_cast<std::int32_t>(size.width - 1), 0},
        Desktop::Types::LogicalPosition{0, static_cast<std::int32_t>(size.height - 1)},
        Desktop::Types::LogicalPosition{static_cast<std::int32_t>(size.width - 1), static_cast<std::int32_t>(size.height - 1)}};
    bool conversionsRoundTrip = true;
    for (const Desktop::Types::LogicalPosition point : points)
    {
        const Desktop::Types::ScreenPositionResult screen = window.clientToScreen(point);
        if (!screen.status.ok())
        {
            conversionsRoundTrip = false;
            break;
        }
        const Desktop::Types::LogicalPositionResult logical = window.screenToClient(screen.position);
        conversionsRoundTrip = conversionsRoundTrip && logical.status.ok() && logical.position == point;
    }
    static_cast<void>(context.expectTrue("client/screen edge conversions round-trip", conversionsRoundTrip));
    recordManualCheck(
        context,
        window,
        "mixed-DPI coordinate rounding",
        "At each available DPI scale, do client/screen edge coordinates follow the expected integral-pixel rounding without visible drift?");

    const Desktop::Types::Display::MonitorsResult monitors = Desktop::Display::getMonitors();
    bool monitorRectsValid = monitors.status.ok() && !monitors.monitors.empty();
    for (const Desktop::Types::Display::Info &monitor : monitors.monitors)
    {
        monitorRectsValid = monitorRectsValid && monitor.bounds.size.width > 0 && monitor.bounds.size.height > 0 && monitor.workArea.size.width > 0 &&
                            monitor.workArea.size.height > 0;
    }
    static_cast<void>(context.expectTrue("physical monitor bounds and work areas are valid", monitorRectsValid));
    recordManualCheck(
        context,
        window,
        "physical virtual-screen monitor rectangles",
        "Do reported monitor bounds and work areas match the Windows virtual-screen arrangement, including negative origins, without independent "
        "scaling?");
    static_cast<void>(window.close());
}

/// @brief Exercises standard shapes, focus-sensitive cursor modes, and logical warping.
void testManualCursor(TestSupport::Context &context, const GameWIP::Test::DesktopTestOptions &options)
{
    if (!beginManualSuite(context, options, "Window cursor behavior"))
        return;

    const Desktop::Types::Capabilities capabilities = Desktop::getCapabilities().capabilities;
    Desktop::Window window;
    if (!openManualWindow(context, window, "GameWIP cursor validation"))
        return;

    constexpr std::array shapes{
        Desktop::Types::CursorShape::Arrow,
        Desktop::Types::CursorShape::Text,
        Desktop::Types::CursorShape::Crosshair,
        Desktop::Types::CursorShape::Hand,
        Desktop::Types::CursorShape::Help,
        Desktop::Types::CursorShape::Wait,
        Desktop::Types::CursorShape::Progress,
        Desktop::Types::CursorShape::Move,
        Desktop::Types::CursorShape::ResizeAll,
        Desktop::Types::CursorShape::ResizeHorizontal,
        Desktop::Types::CursorShape::ResizeVertical,
        Desktop::Types::CursorShape::ResizeDiagonalNorthWestSouthEast,
        Desktop::Types::CursorShape::ResizeDiagonalNorthEastSouthWest,
        Desktop::Types::CursorShape::NotAllowed};
    constexpr std::array shapeNames{
        std::string_view{"Arrow"},
        std::string_view{"Text"},
        std::string_view{"Crosshair"},
        std::string_view{"Hand"},
        std::string_view{"Help"},
        std::string_view{"Wait"},
        std::string_view{"Progress"},
        std::string_view{"Move"},
        std::string_view{"ResizeAll"},
        std::string_view{"ResizeHorizontal"},
        std::string_view{"ResizeVertical"},
        std::string_view{"ResizeDiagonalNorthWestSouthEast"},
        std::string_view{"ResizeDiagonalNorthEastSouthWest"},
        std::string_view{"NotAllowed"}};
    for (std::size_t index = 0; index < shapes.size(); ++index)
    {
        static_cast<void>(context.expectTrue("standard cursor shape applies", window.setCursorShape(shapes[index]).ok()));
        const Desktop::Types::LogicalSize size = window.clientSize();
        static_cast<void>(context.expectTrue(
            "cursor moves into the visible validation surface",
            window.setCursorPosition({static_cast<std::int32_t>(size.width / 2), static_cast<std::int32_t>(size.height / 2)}).ok()));
        recordManualCheck(
            context,
            window,
            "standard cursor shape",
            std::format(
                "The diagnostics show cursorShape={}. Does the '{}' system cursor display correctly over the validation client?",
                static_cast<int>(shapes[index]),
                shapeNames[index]));
    }

    for (const auto &[mode, capability, name] : std::array{
             std::tuple{Desktop::Types::CursorMode::Hidden, Desktop::Types::Capability::Count, std::string_view{"hidden"}},
             std::tuple{Desktop::Types::CursorMode::Confined, Desktop::Types::Capability::CursorConfinement, std::string_view{"confined"}},
             std::tuple{
                 Desktop::Types::CursorMode::HiddenConfined,
                 Desktop::Types::Capability::CursorConfinement,
                 std::string_view{"hidden-confined"}},
             std::tuple{Desktop::Types::CursorMode::Relative, Desktop::Types::Capability::RelativeCursor, std::string_view{"relative"}}})
    {
        if (capability != Desktop::Types::Capability::Count && !capabilities.supports(capability))
        {
            context.skip(std::format("{} cursor mode", name), "backend does not advertise the required capability");
            continue;
        }
        static_cast<void>(context.expectTrue("cursor mode applies", window.setCursorMode(mode).ok()));
        recordManualCheck(
            context,
            window,
            std::format("{} cursor mode", name),
            std::format(
                "Exercise {} mode while focused, then alt-tab, minimize, hide, restore, and refocus. Is the system cursor always released and "
                "reacquired correctly?",
                name));
        static_cast<void>(window.setCursorMode(Desktop::Types::CursorMode::Normal));
    }

    if (capabilities.supports(Desktop::Types::Capability::CursorWarping))
    {
        const Desktop::Types::LogicalSize size = window.clientSize();
        for (const Desktop::Types::LogicalPosition point :
             {Desktop::Types::LogicalPosition{0, 0},
              Desktop::Types::LogicalPosition{static_cast<std::int32_t>(size.width - 1), static_cast<std::int32_t>(size.height - 1)}})
        {
            static_cast<void>(context.expectTrue("cursor warp succeeds", window.setCursorPosition(point).ok()));
            const Desktop::Types::LogicalPositionResult actual = window.cursorPosition();
            static_cast<void>(context.expectTrue("cursor warp query succeeds", actual.status.ok()));
            if (actual.status.ok())
                static_cast<void>(context.expectEq("cursor warp reaches requested logical point", point, actual.position));
        }
        recordManualCheck(
            context,
            window,
            "cursor corner warping",
            "Did cursor warping reach both logical client corners correctly at the available DPI scales?");
    }
    else
    {
        context.skip("cursor corner warping", "backend does not advertise CursorWarping");
    }
    static_cast<void>(window.setCursorMode(Desktop::Types::CursorMode::Normal));
    static_cast<void>(window.close());
}

/// @brief Exercises file-drop events and native shell-facing Window controls.
void testManualFilesAndShell(TestSupport::Context &context, const GameWIP::Test::DesktopTestOptions &options)
{
    if (!beginManualSuite(context, options, "Window files and shell behavior"))
        return;

    const Desktop::Types::Capabilities capabilities = Desktop::getCapabilities().capabilities;
    Desktop::Types::Description description;
    description.fileDropEnabled = capabilities.supports(Desktop::Types::Capability::FileDrop);
    Desktop::Window window;
    if (!openManualWindow(context, window, "GameWIP file and shell validation", description))
        return;

    if (capabilities.supports(Desktop::Types::Capability::FileDrop))
    {
        const auto runDrop = [&](std::string_view name, std::string_view instruction, std::size_t minimumPaths)
        {
            window.clearEvents();
            std::size_t groupedEvents = 0;
            std::size_t pathCount = 0;
            const auto observeDrops = [&]
            {
                Desktop::Types::Event event;
                while (window.popEvent(event))
                {
                    if (const auto *drop = event.getIf<Desktop::Types::Events::FilesDropped>())
                    {
                        ++groupedEvents;
                        pathCount += drop->paths.size();
                        std::string pathList;
                        for (const auto &path : drop->paths)
                        {
                            const std::u8string utf8 = path.generic_u8string();
                            if (!pathList.empty())
                                pathList.append("\r\n");
                            pathList.append(reinterpret_cast<const char *>(utf8.data()), utf8.size());
                        }
                        const std::string observation = std::format(
                            "Received FilesDroppedEvent: groupedEvents={} paths={} clientPosition={}\r\n{}",
                            groupedEvents,
                            pathCount,
                            drop->clientPosition.has_value(),
                            pathList);
                        if (manualStatusWindow != nullptr)
                            manualStatusWindow->setObservation(observation);
                        static_cast<void>(window.setTitle(std::format("Drop received: {} path(s)", pathCount)));
                    }
                }
            };
            static_cast<void>(window.setTitle("Waiting for file drop..."));
            if (manualStatusWindow != nullptr)
                manualStatusWindow->setObservation("Waiting for FilesDroppedEvent.");
            const TestSupport::Types::Reporting::ManualAnswer answer = recordManualCheck(context, window, name, instruction, observeDrops);
            if (answer != TestSupport::Types::Reporting::ManualAnswer::Yes)
                return;
            static_cast<void>(context.expectEq("file drop produces one grouped event", std::size_t{1}, groupedEvents));
            static_cast<void>(context.expectTrue("file drop retains expected paths", pathCount >= minimumPaths));
        };

        runDrop("single-file drop", "Drag one ordinary file into the Window, then answer yes if one drop was accepted.", 1);
        runDrop("multiple-file drop", "Drag at least two files together into the Window, then answer yes if one grouped drop was accepted.", 2);
        runDrop("Unicode-path drop", "Drag a file whose path contains non-ASCII characters, then answer yes if it was accepted intact.", 1);
        runDrop("space-containing-path drop", "Drag a file whose path contains spaces, then answer yes if it was accepted intact.", 1);

        static_cast<void>(context.expectTrue("file drops disable", window.setFileDropEnabled(false).ok()));
        window.clearEvents();
        const TestSupport::Types::Reporting::ManualAnswer disabledAnswer =
            recordManualCheck(context, window, "disabled file drops", "Try dropping a file. Was it rejected with no observable drop behavior?");
        if (disabledAnswer == TestSupport::Types::Reporting::ManualAnswer::Yes)
            static_cast<void>(
                context.expectFalse("disabled file drops queue no event", consumeEventOfType<Desktop::Types::Events::FilesDropped>(window)));
    }
    else
    {
        context.skip("Window file drops", "backend does not advertise FileDrop");
    }

    if (capabilities.supports(Desktop::Types::Capability::WindowIcon))
    {
        const auto makeIcon = [](std::uint32_t extent)
        {
            std::vector<std::byte> pixels(static_cast<std::size_t>(extent) * extent * 4u);
            for (std::uint32_t y = 0; y < extent; ++y)
            {
                for (std::uint32_t x = 0; x < extent; ++x)
                {
                    const bool cyan = x < std::max(2u, extent / 5u) || y < std::max(2u, extent / 5u) || x == y || x + y + 1u == extent;
                    const std::size_t offset = (static_cast<std::size_t>(y) * extent + x) * 4u;
                    pixels[offset] = cyan ? std::byte{70} : std::byte{22};
                    pixels[offset + 1u] = cyan ? std::byte{210} : std::byte{70};
                    pixels[offset + 2u] = cyan ? std::byte{255} : std::byte{126};
                    pixels[offset + 3u] = std::byte{255};
                }
            }
            return pixels;
        };
        std::vector<std::byte> smallPixels = makeIcon(16u);
        std::vector<std::byte> largePixels = makeIcon(32u);
        const std::array images{Desktop::Types::IconImageView{{16, 16}, smallPixels}, Desktop::Types::IconImageView{{32, 32}, largePixels}};
        static_cast<void>(context.expectTrue("custom shell icons apply", window.setIcon(images).ok()));
        recordManualCheck(
            context,
            window,
            "custom shell icons",
            "Does the blue/cyan GameWIP validation icon appear clearly at the appropriate small title-bar and larger taskbar shell sizes?");
        static_cast<void>(window.clearIcon());
    }

    static_cast<void>(context.expectTrue("attention request succeeds", window.requestAttention().ok()));
    recordManualCheck(context, window, "attention indication", "Did Windows show its normal taskbar/native attention indication?");

    static_cast<void>(context.expectTrue("focusability disables", window.setFocusable(false).ok()));
    static_cast<void>(context.expectTrue("interaction disables", window.setUserInteractionEnabled(false).ok()));
    recordManualCheck(
        context,
        window,
        "disabled focus and interaction",
        "Is the Window visibly disabled and unable to acquire focus or interaction?");
    static_cast<void>(window.setUserInteractionEnabled(true));
    static_cast<void>(window.setFocusable(true));
    static_cast<void>(context.expectTrue("topmost enables", window.setAlwaysOnTop(true).ok()));
    recordManualCheck(context, window, "topmost policy", "Does the Window remain above ordinary Windows without breaking activation?");
    static_cast<void>(window.setAlwaysOnTop(false));

    for (const bool resizable : {false, true})
    {
        for (const bool maximizable : {false, true})
        {
            static_cast<void>(window.setResizable(true));
            Desktop::Types::Controls controls = window.controls();
            controls.maximizable = maximizable;
            const IO::Types::Status controlsStatus = window.setControls(controls);
            const IO::Types::Status resizeStatus = window.setResizable(resizable);
            if (!resizable && maximizable)
            {
                static_cast<void>(
                    context.expectTrue("invalid nonresizable/maximizable combination is rejected", !controlsStatus.ok() || !resizeStatus.ok()));
            }
            else
            {
                static_cast<void>(context.expectTrue("valid resize/control combination applies", controlsStatus.ok() && resizeStatus.ok()));
            }
        }
    }
    static_cast<void>(window.setResizable(true));
    Desktop::Types::Controls maximizableControls = window.controls();
    maximizableControls.maximizable = true;
    static_cast<void>(window.setControls(maximizableControls));
    static_cast<void>(context.expectTrue("maximizable-then-nonresizable transition is rejected", !window.setResizable(false).ok()));
    static_cast<void>(window.setControls({.closable = true, .minimizable = true, .maximizable = false}));
    static_cast<void>(window.setResizable(false));
    maximizableControls = window.controls();
    maximizableControls.maximizable = true;
    static_cast<void>(context.expectTrue("nonresizable-then-maximizable transition is rejected", !window.setControls(maximizableControls).ok()));
    static_cast<void>(window.setResizable(true));
    static_cast<void>(window.setControls({}));
    Desktop::Types::Controls independentControls = window.controls();
    independentControls.closable = false;
    independentControls.minimizable = false;
    static_cast<void>(context.expectTrue("close and minimize controls disable independently", window.setControls(independentControls).ok()));
    static_cast<void>(context.expectFalse("close control cached disabled", window.controls().closable));
    static_cast<void>(context.expectFalse("minimize control cached disabled", window.controls().minimizable));
    recordManualCheck(
        context,
        window,
        "standard control combinations",
        "The diagnostics must show close=false, minimize=false, maximize=true, resizable=true. Are only the close and minimize controls disabled "
        "in the native frame?");
    static_cast<void>(window.setControls({}));

    Desktop::Types::Description ownedDescription;
    ownedDescription.owner = window.id();
    Desktop::Window owned;
    if (openManualWindow(context, owned, "GameWIP owned taskbar validation", ownedDescription))
    {
        recordManualCheck(
            context,
            owned,
            "owned taskbar default",
            "The diagnostics must show a nonzero ownerId. Does the owned Window have no independent taskbar entry?");
        static_cast<void>(owned.setOwner({}));
        recordManualCheck(
            context,
            owned,
            "owned taskbar removal",
            "The diagnostics must show ownerId=0. After removing ownership, does an independent taskbar entry appear?");
        static_cast<void>(owned.setOwner(window.id()));
        recordManualCheck(
            context,
            owned,
            "owned taskbar restoration",
            "The diagnostics must again show a nonzero ownerId. After restoring ownership, does the independent taskbar entry disappear again?");
        static_cast<void>(owned.close());
    }
    static_cast<void>(window.close());
}

/// @brief Exercises selected fullscreen transitions, monitor movement, and live topology recovery.
void testManualFullscreenAndTopology(
    TestSupport::Context &context,
    const GameWIP::Test::DesktopTestOptions &options,
    ManualFullscreenSections sections)
{
    if (!beginManualSuite(context, options, "Window fullscreen and display topology"))
        return;

    const Desktop::Types::Capabilities capabilities = Desktop::getCapabilities().capabilities;
    const Desktop::Types::Display::MonitorsResult monitors = Desktop::Display::getMonitors();
    if (!monitors.status.ok() || monitors.monitors.empty())
    {
        context.fail("fullscreen monitor enumeration", monitors.status.message);
        return;
    }

    Desktop::Window window;
    if (!openManualWindow(context, window, "GameWIP fullscreen validation"))
        return;
    const Desktop::Types::ScreenPosition savedPosition = window.clientPosition();
    const Desktop::Types::LogicalSize savedSize = window.clientSize();
    const auto disconnectableMonitor = []() -> std::optional<Desktop::Types::Display::MonitorId>
    {
        const Desktop::Types::Display::MonitorsResult connected = Desktop::Display::getMonitors();
        if (!connected.status.ok())
            return std::nullopt;
        const auto secondary = std::ranges::find_if(
            connected.monitors,
            [](const Desktop::Types::Display::Info &monitor)
            {
                return !monitor.primary;
            });
        return secondary == connected.monitors.end() ? std::nullopt : std::optional{secondary->id};
    };

    if (sections.borderless)
    {
        for (std::size_t index = 0; index < monitors.monitors.size(); ++index)
        {
            const Desktop::Types::Display::Info &monitor = monitors.monitors[index];
            Desktop::Types::ModeRequest borderless;
            borderless.mode = Desktop::Types::Mode::BorderlessFullscreen;
            borderless.monitor = monitor.id;
            const IO::Types::Status enterStatus = setManualModeWithDiagnostics(context, window, "borderless-enter", borderless);
            if (requireManualStatus(context, "borderless fullscreen enters", enterStatus))
            {
                const ManualNativeWindowState native = manualNativeWindowState(window);
                static_cast<void>(context.expectTrue("borderless native HWND query succeeds", native.valid));
                static_cast<void>(context.expectTrue("borderless native popup style applies", native.popupStyle));
                static_cast<void>(context.expectTrue("borderless native bounds match monitor", native.fullscreenBounds));
                static_cast<void>(context.expectTrue("borderless Window remains taskbar eligible", native.taskbarEligible));
                recordManualCheck(
                    context,
                    window,
                    "borderless fullscreen monitor",
                    std::format(
                        "The blue validation surface and cyan inset border must touch every display edge behind the diagnostics. The native section "
                        "must show popupStyle=true and fullscreenBounds=true. Press the Windows key: a separate GameWIP taskbar button must be "
                        "present, or use Alt+Tab to verify the Window is listed. On monitor {}/{} ({}), does it exactly cover the display and "
                        "remain switchable without changing its display mode?",
                        index + 1,
                        monitors.monitors.size(),
                        monitor.name));
                const IO::Types::Status leaveStatus = setManualModeWithDiagnostics(context, window, "borderless-leave", {});
                static_cast<void>(requireManualStatus(context, "borderless fullscreen leaves", leaveStatus));
            }
            else
            {
                context.skip("borderless fullscreen monitor", "borderless-mode setup failed; see preceding status");
            }
        }
        const Desktop::Types::LogicalSize restoredSize = window.clientSize();
        if (savedSize == restoredSize)
        {
            context.pass("windowed size restores after borderless");
        }
        else
        {
            context.fail(
                "windowed size restores after borderless",
                std::format("expected {}x{}, got {}x{}", savedSize.width, savedSize.height, restoredSize.width, restoredSize.height));
        }
        recordManualCheck(
            context,
            window,
            "windowed placement restoration",
            std::format("After fullscreen transitions, did the Window restore its saved placement near ({}, {})?", savedPosition.x, savedPosition.y));
    }

    if (sections.exclusive || sections.topology)
    {
        if (capabilities.supports(Desktop::Types::Capability::ExclusiveFullscreen))
        {
            const std::optional<Desktop::Types::Display::MonitorId> topologyMonitor = sections.topology ? disconnectableMonitor() : std::nullopt;
            const Desktop::Types::Display::MonitorId monitor = topologyMonitor.value_or(window.currentMonitor());
            const Desktop::Types::Display::ModeResult currentMode = Desktop::Display::getCurrentMode(monitor);
            const Desktop::Types::Display::ModesResult availableModes = Desktop::Display::getModes(monitor);
            if (currentMode.status.ok() && availableModes.status.ok() && !availableModes.modes.empty())
            {
                const auto selected = std::ranges::min_element(
                    availableModes.modes,
                    {},
                    [&](const Desktop::Types::Display::Mode &mode)
                    {
                        const std::uint64_t resolutionPenalty = mode.resolution == currentMode.mode.resolution ? 0 : std::uint64_t{1} << 48;
                        const std::uint64_t depthPenalty = mode.bitsPerPixel == currentMode.mode.bitsPerPixel ? 0 : std::uint64_t{1} << 40;
                        const std::uint64_t interlacePenalty = mode.interlaced == currentMode.mode.interlaced ? 0 : std::uint64_t{1} << 39;
                        const std::uint64_t refreshDifference = mode.refreshRateMillihertz > currentMode.mode.refreshRateMillihertz
                                                                    ? mode.refreshRateMillihertz - currentMode.mode.refreshRateMillihertz
                                                                    : currentMode.mode.refreshRateMillihertz - mode.refreshRateMillihertz;
                        return resolutionPenalty + depthPenalty + interlacePenalty + refreshDifference;
                    });
                Desktop::Types::ModeRequest exclusive;
                exclusive.mode = Desktop::Types::Mode::ExclusiveFullscreen;
                exclusive.monitor = monitor;
                exclusive.displayMode = *selected;
                if (manualStatusWindow != nullptr)
                {
                    manualStatusWindow->setObservation(
                        std::format(
                            "Requesting enumerated exact mode {}x{} @ {:.3f} Hz, {} bpp, interlaced={}.",
                            selected->resolution.width,
                            selected->resolution.height,
                            static_cast<double>(selected->refreshRateMillihertz) / 1000.0,
                            selected->bitsPerPixel,
                            selected->interlaced));
                }

                if (sections.exclusive)
                {
                    const IO::Types::Status enterStatus = setManualModeWithDiagnostics(context, window, "exclusive-enter", exclusive);
                    if (requireManualStatus(context, "exclusive fullscreen enters", enterStatus))
                    {
                        const ManualNativeWindowState native = manualNativeWindowState(window);
                        static_cast<void>(context.expectTrue("exclusive native HWND query succeeds", native.valid));
                        static_cast<void>(context.expectTrue("exclusive native popup style applies", native.popupStyle));
                        static_cast<void>(context.expectTrue("exclusive native bounds match active monitor", native.fullscreenBounds));
                        static_cast<void>(context.expectTrue("exclusive Window remains taskbar eligible", native.taskbarEligible));
                        bool sawExclusiveActive = window.focused() && !window.fullscreenInfo().suspended;
                        bool sawExclusiveSuspended = window.fullscreenInfo().suspended;
                        recordManualCheck(
                            context,
                            window,
                            "exclusive fullscreen activation cycle",
                            "Alt+Tab to the blue validation surface, back to the terminal, to the validation Window once more, and finally back "
                            "to the terminal to answer. While focused it must cover the display; while back at the terminal, suspended=true and "
                            "a windowed-sized surface are expected. The test records both states automatically. Does that activation cycle "
                            "behave correctly?",
                            [&]
                            {
                                const Desktop::Types::FullscreenInfo liveFullscreen = window.fullscreenInfo();
                                sawExclusiveActive = sawExclusiveActive || (window.focused() && !liveFullscreen.suspended);
                                sawExclusiveSuspended = sawExclusiveSuspended || liveFullscreen.suspended;
                                if (manualStatusWindow != nullptr)
                                {
                                    manualStatusWindow->setObservation(
                                        std::format(
                                            "Activation evidence: activeSeen={} suspendedSeen={} (finish in the terminal to answer).",
                                            sawExclusiveActive,
                                            sawExclusiveSuspended));
                                }
                            });
                        static_cast<void>(context.expectTrue("exclusive activation state is observed", sawExclusiveActive));
                        static_cast<void>(context.expectTrue("exclusive suspension state is observed", sawExclusiveSuspended));
                        const IO::Types::Status leaveStatus = setManualModeWithDiagnostics(context, window, "exclusive-leave", {});
                        if (requireManualStatus(context, "exclusive fullscreen leaves", leaveStatus))
                        {
                            recordManualCheck(
                                context,
                                window,
                                "exclusive display restoration",
                                "The diagnostics must show mode=0 and the original geometry. Was the original desktop display mode restored "
                                "exactly?");
                        }
                    }
                    else
                    {
                        context.skip("exclusive fullscreen activation cycle", "exclusive-mode setup failed; see preceding status");
                        context.skip("exclusive display restoration", "exclusive-mode setup failed; no display transition occurred");
                    }

                    Desktop::Types::ModeRequest unsupported = exclusive;
                    unsupported.displayMode->resolution = {1, 1};
                    const Desktop::Types::Mode previousMode = window.mode();
                    const IO::Types::Status unsupportedStatus = setManualModeWithDiagnostics(context, window, "exclusive-unsupported", unsupported);
                    static_cast<void>(context.expectTrue("unsupported exact mode is rejected", !unsupportedStatus.ok()));
                    static_cast<void>(context.expectEq("unsupported exact mode preserves Window mode", previousMode, window.mode()));
                }

                if (sections.topology)
                {
                    if (!topologyMonitor)
                    {
                        context.skip("active exclusive target disconnect", "no connected non-primary monitor can be physically disconnected");
                    }
                    else
                    {
                        const IO::Types::Status recoveryEnter =
                            setManualModeWithDiagnostics(context, window, "exclusive-disconnect-enter", exclusive);
                        if (recoveryEnter.ok())
                        {
                            const TestSupport::Types::Reporting::ManualAnswer recovery = recordManualCheck(
                                context,
                                window,
                                "active exclusive target disconnect",
                                "Disconnect/disable this non-primary exclusive-fullscreen monitor. Confirm the desktop mode restores and the "
                                "Window recovers visibly on the surviving primary, then reconnect it before answering. Otherwise skip.");
                            if (recovery == TestSupport::Types::Reporting::ManualAnswer::Yes)
                            {
                                static_cast<void>(
                                    context.expectEq("exclusive disconnect recovers windowed mode", Desktop::Types::Mode::Windowed, window.mode()));
                                static_cast<void>(
                                    context.expectFalse("exclusive disconnect clears fullscreen monitor", window.fullscreenInfo().monitor.isValid()));
                            }
                            if (window.mode() != Desktop::Types::Mode::Windowed)
                            {
                                static_cast<void>(
                                    setManualModeWithDiagnostics(context, window, "exclusive-disconnect-cleanup", Desktop::Types::ModeRequest{}));
                            }
                        }
                        else
                        {
                            context.skip("active exclusive target disconnect", "exclusive-mode setup failed; no active target to disconnect");
                        }
                    }
                }
            }
            else
            {
                if (sections.exclusive)
                    context.skip("exclusive fullscreen", "no enumerated exact display mode is available for the current monitor");
                if (sections.topology)
                    context.skip("active exclusive target disconnect", "no enumerated exact display mode is available for the current monitor");
            }
        }
        else
        {
            if (sections.exclusive)
                context.skip("exclusive fullscreen", "backend does not advertise ExclusiveFullscreen");
            if (sections.topology)
                context.skip("active exclusive target disconnect", "backend does not advertise ExclusiveFullscreen");
        }
    }

    if (sections.topology)
    {
        recordManualCheck(
            context,
            window,
            "mixed-monitor fullscreen geometry",
            "Move the Window between monitors with different DPI when available. Do logical geometry, framebuffer extent, DPI/scale, and "
            "current-monitor state follow the destination?");
        recordManualCheck(
            context,
            window,
            "monitor connect and disconnect",
            "If practical, connect/disconnect or enable/disable a non-active monitor. Does re-enumeration succeed and does the stale MonitorId "
            "fail safely? Reconnect it before answering. Skip if impractical.");

        const std::optional<Desktop::Types::Display::MonitorId> topologyMonitor = disconnectableMonitor();
        if (!topologyMonitor)
        {
            context.skip("active borderless target disconnect", "no connected non-primary monitor can be physically disconnected");
        }
        else
        {
            Desktop::Types::ModeRequest activeBorderless;
            activeBorderless.mode = Desktop::Types::Mode::BorderlessFullscreen;
            activeBorderless.monitor = *topologyMonitor;
            const IO::Types::Status borderlessEnter = setManualModeWithDiagnostics(context, window, "borderless-disconnect-enter", activeBorderless);
            if (borderlessEnter.ok())
            {
                const TestSupport::Types::Reporting::ManualAnswer recovery = recordManualCheck(
                    context,
                    window,
                    "active borderless target disconnect",
                    "Disconnect/disable this non-primary fullscreen monitor. Confirm the Window recovers visibly in windowed mode on the "
                    "surviving primary, then reconnect it before answering. Otherwise skip.");
                if (recovery == TestSupport::Types::Reporting::ManualAnswer::Yes)
                {
                    static_cast<void>(
                        context.expectEq("borderless disconnect recovers windowed mode", Desktop::Types::Mode::Windowed, window.mode()));
                    static_cast<void>(
                        context.expectFalse("borderless disconnect clears fullscreen monitor", window.fullscreenInfo().monitor.isValid()));
                }
                if (window.mode() != Desktop::Types::Mode::Windowed)
                {
                    static_cast<void>(setManualModeWithDiagnostics(context, window, "borderless-disconnect-cleanup", Desktop::Types::ModeRequest{}));
                }
            }
        }

        context.pass("fullscreen recovery event ordering and failure-state cleanup are covered deterministically");
    }
    static_cast<void>(window.close());
}

/// @brief Exercises current SDR/HDR facts and user-driven advanced-color transitions.
void testManualHdrAndAdvancedColor(TestSupport::Context &context, const GameWIP::Test::DesktopTestOptions &options)
{
    if (!beginManualSuite(context, options, "Window HDR and advanced color"))
        return;

    const Desktop::Types::Display::MonitorsResult monitors = Desktop::Display::getMonitors();
    if (!monitors.status.ok() || monitors.monitors.empty())
    {
        context.fail("HDR monitor enumeration", monitors.status.message);
        return;
    }

    Desktop::Window window;
    if (!openManualWindow(context, window, "GameWIP HDR validation"))
        return;

    for (const Desktop::Types::Display::Info &monitor : monitors.monitors)
    {
        const Desktop::Types::Display::ColorInfoResult direct = Desktop::Display::getColorInfo(monitor.id);
        static_cast<void>(context.expectTrue("monitor display-color query succeeds", direct.status.ok()));
        if (!direct.status.ok())
            continue;
        const auto &info = direct.info;
        static_cast<void>(context.expectEq("display-color query retains monitor identity", monitor.id, info.monitor));
        static_cast<void>(context.expectTrue("HDR enabled implies HDR supported", !info.hdrEnabled || info.hdrSupported));
        static_cast<void>(context.expectTrue("SDR-only display is not marked HDR enabled", info.hdrSupported || !info.hdrEnabled));
        static_cast<void>(context.expectTrue(
            "HDR-disabled display is not classified as HDR",
            info.hdrEnabled || info.activeColorSpace != Desktop::Types::Display::ColorSpace::Hdr10Pq));
        static_cast<void>(context.expectTrue(
            "HDR classification is truthful",
            !info.hdrEnabled || info.activeColorSpace == Desktop::Types::Display::ColorSpace::Hdr10Pq ||
                info.activeColorSpace == Desktop::Types::Display::ColorSpace::Unknown));
        recordManualCheck(
            context,
            window,
            "display-color facts",
            std::format(
                "For display '{}', do support={}, enabled={}, bits/channel={}, min/peak/full-frame={:.1f}/{:.1f}/{:.1f} nits and SDR "
                "white={:.1f} nits match Windows/driver reports?",
                monitor.name,
                info.hdrSupported,
                info.hdrEnabled,
                info.bitsPerColorChannel,
                info.minimumLuminanceNits,
                info.maximumLuminanceNits,
                info.maximumFullFrameLuminanceNits,
                info.sdrWhiteLevelNits));
    }

    const Desktop::Types::Display::ColorInfoResult windowInfo = Desktop::Display::getColorInfo(window);
    static_cast<void>(context.expectTrue("Window display-color query succeeds", windowInfo.status.ok()));
    if (windowInfo.status.ok())
        static_cast<void>(context.expectEq("Window display-color monitor matches current monitor", window.currentMonitor(), windowInfo.info.monitor));

    const TestSupport::Types::Reporting::ManualAnswer toggle = recordManualCheck(
        context,
        window,
        "HDR toggle in place",
        "If this display supports HDR, toggle HDR in Windows, return here, and verify the Window remains stable. Skip on SDR-only hardware.");
    if (toggle == TestSupport::Types::Reporting::ManualAnswer::Yes)
    {
        static_cast<void>(context.expectTrue("HDR toggle remains queryable", Desktop::Display::getColorInfo(window).status.ok()));
        static_cast<void>(context.expectTrue(
            "HDR toggle delivers display configuration event",
            consumeEventOfType<Desktop::Types::Events::DisplayConfigurationChanged>(window)));
    }

    recordManualCheck(
        context,
        window,
        "SDR/HDR monitor movement",
        "If both SDR and HDR displays are available, move the Window between them. Do MonitorChangedEvent-triggered queries follow the "
        "destination state? Skip otherwise.");
    recordManualCheck(
        context,
        window,
        "HDR display reconnect",
        "If safe, disconnect and reconnect the queried display. Does the stale ID fail, followed by a successful fresh enumeration/query? Skip "
        "otherwise.");
    recordManualCheck(
        context,
        window,
        "Windows 10 advanced-color compatibility",
        "On an available Windows 10 compatibility host, does the legacy query work while unavailable WCG metadata remains unknown? Skip on the "
        "supported Windows 11 host.");
    static_cast<void>(window.close());
}

/// @brief Exercises runtime-gated backdrop effects and renderer-provided framebuffer alpha.
void testManualModernWindowsCapabilities(TestSupport::Context &context, const GameWIP::Test::DesktopTestOptions &options)
{
    if (!beginManualSuite(context, options, "Window modern Windows capabilities"))
        return;

    const Desktop::Types::Capabilities capabilities = Desktop::getCapabilities().capabilities;
    Desktop::Window window;
    if (!openManualWindow(context, window, "GameWIP modern capability validation"))
        return;

    constexpr std::array effects{
        Desktop::Types::BackdropEffect::Automatic,
        Desktop::Types::BackdropEffect::MainWindow,
        Desktop::Types::BackdropEffect::TransientWindow,
        Desktop::Types::BackdropEffect::TabbedWindow};
    if (capabilities.supports(Desktop::Types::Capability::SystemBackdrop))
    {
        for (std::size_t index = 0; index < effects.size(); ++index)
        {
            const IO::Types::Status status = window.setBackdropEffect(effects[index]);
            static_cast<void>(context.expectTrue("supported backdrop effect applies", status.ok()));
            if (status.ok())
            {
                recordManualCheck(
                    context,
                    window,
                    "system backdrop presentation",
                    std::format("Does backdrop effect {}/{} render as the corresponding native Windows treatment?", index + 1, effects.size()));
            }
            static_cast<void>(context.expectTrue("system backdrop clears", window.setBackdropEffect(Desktop::Types::BackdropEffect::None).ok()));
        }
    }
    else
    {
        for (const Desktop::Types::BackdropEffect effect : effects)
        {
            static_cast<void>(
                context.expectEq("unsupported backdrop effect is rejected", ErrorCode::Unsupported, window.setBackdropEffect(effect).code));
        }
        context.skip("system backdrop presentation", "runtime does not advertise SystemBackdrop");
    }
    static_cast<void>(window.close());

    Desktop::Types::Description alphaDescription;
    alphaDescription.transparentFramebuffer = true;
    Desktop::Window alpha;
    const IO::Types::Status alphaStatus = alpha.open(alphaDescription);
    if (capabilities.supports(Desktop::Types::Capability::TransparentFramebuffer))
    {
        static_cast<void>(context.expectTrue("modern alpha Window opens", alphaStatus.ok()));
        if (alpha.isOpen())
        {
            static_cast<void>(alpha.show());
            context.skip(
                "redirection-bitmap framebuffer alpha",
                "GameWIPTests has no renderer-provided premultiplied-alpha surface; run this observation in a renderer-backed host");
            context.skip("opacity and framebuffer-alpha independence", "requires the same renderer-backed alpha host");
            static_cast<void>(alpha.close());
        }
    }
    else
    {
        static_cast<void>(context.expectEq("unsupported modern alpha Window is rejected", ErrorCode::Unsupported, alphaStatus.code));
    }
}
