/// @file desktop_manual_support.inl
/// @brief Shared helpers for opt-in Desktop manual validation.

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

        // Capture portable state first, then append native state as an independent cross-check.
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
    DragDropSource,
    DragDropTarget,
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
    if (layout == ManualSurfaceLayout::DragDropSource)
    {
        fill(client, RGB(25, 104, 72));
        drawText(L"GAMEWIP DRAG SOURCE", clippedRect(20, 100, 540, 155), 26, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        drawText(
            L"Press and hold the requested button here, then drag to the target or foreign application.",
            clippedRect(30, 170, 530, 270),
            18,
            RGB(255, 255, 255),
            DT_CENTER | DT_VCENTER | DT_WORDBREAK);
    }
    else if (layout == ManualSurfaceLayout::DragDropTarget)
    {
        RECT preferred = clippedRect(120, 90, 480, 270);
        fill(preferred, RGB(20, 128, 94));
        drawText(L"GAMEWIP DROP TARGET", clippedRect(20, 20, 540, 70), 24, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        drawText(L"REGION 2 - MOVE PREFERRED", preferred, 18, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_WORDBREAK);
        drawText(
            L"Blue remainder: whole-client Region 1, Copy preferred",
            clippedRect(20, 285, 540, 335),
            16,
            RGB(255, 255, 255),
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    else if (layout == ManualSurfaceLayout::CustomChromePrimary)
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
