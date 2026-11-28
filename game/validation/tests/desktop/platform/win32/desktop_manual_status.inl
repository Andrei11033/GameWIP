/// @file desktop_manual_status.inl
/// @brief Manual-scenario status window and selection state.

/// ------------------------------------------------------------
/// Companion window and validation surface
/// ------------------------------------------------------------

/// @brief Read-only native companion Window that exposes live state during manual scenarios.
class ManualStatusWindow final
{
public:
    explicit ManualStatusWindow(bool enabled)
    {
        if(!enabled)
        {
            return;
        }

        RECT workArea{};

        if(SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0) == FALSE)
        {
            workArea = {0, 0, 1920, 1080};
        }
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

        if(handle_ != nullptr)
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
        if(handle_ != nullptr)
        {
            DestroyWindow(handle_);
        }
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
        if(handle_ == nullptr)
        {
            return;
        }

        // Capture portable state first, then append native state as an independent cross-check.
        const Desktop::Types::LogicalSize logical = window.clientSize();
        const Desktop::Types::PixelSize framebuffer = window.framebufferSize();
        const Desktop::Types::ScreenPosition position = window.clientPosition();
        const Desktop::Types::ContentScale scale = window.contentScale();
        const Desktop::Types::Dpi dpi = window.effectiveDpi();
        const Desktop::Types::Controls controls = window.controls();
        const Desktop::Types::FullscreenState fullscreen = window.fullscreenState();
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
            static_cast<int>(window.windowMode()),
            static_cast<int>(window.presentationState()),
            static_cast<int>(window.decorationMode()),
            window.opacity(),
            fullscreen.monitor ? fullscreen.monitor->value : std::uint64_t{0},
            fullscreen.displayModeMatchedExactly,
            fullscreen.suspended,
            static_cast<int>(window.cursorMode()),
            static_cast<int>(window.cursorShape()),
            static_cast<int>(window.pointerInputMode()),
            controls.closable,
            controls.minimizable,
            controls.maximizable,
            window.resizable(),
            window.ownerId() ? window.ownerId()->value : std::uint64_t{0},
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

        if(text == lastText_)
        {
            return;
        }
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
std::optional<std::string_view> manualCaseSelection;
std::size_t manualCasesSelected = 0;

[[nodiscard]] bool manualCaseSelected(std::string_view name) noexcept
{
    return !manualCaseSelection || name.contains(*manualCaseSelection);
}
