/// @file desktop_manual_support.inl
/// @brief Shared helpers for opt-in Desktop manual validation.

#include <vector>

[[nodiscard]] std::wstring manualDiagnosticWideText(std::string_view text)
{
    if (text.empty())
    {
        return {};
    }
    const auto measurement = GameWIP::Unicode::Utf8::measureToUtf16(text);

    if (measurement.outcome != GameWIP::Unicode::Types::MeasureOutcome::Measured)
    {
        return L"?";
    }
    std::vector<char16_t> converted(measurement.requiredCodeUnits);
    const auto conversion = GameWIP::Unicode::Utf8::convertToUtf16(text, converted);

    if (conversion.outcome != GameWIP::Unicode::Types::ConversionOutcome::Converted)
    {
        return L"?";
    }
    std::wstring wide(conversion.codeUnitsWritten, L'\0');

    for (std::size_t index = 0; index < conversion.codeUnitsWritten; ++index)
    {
        wide[index] = static_cast<wchar_t>(converted[index]);
    }

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
    {
        return result;
    }
    const auto native = Desktop::Native::Win32::getHandle(window);

    if (!native.status.ok() || native->window == nullptr)
    {
        return result;
    }

    RECT client{};
    POINT clientTopLeft{};
    POINT clientBottomRight{};
    const HMONITOR monitor = MonitorFromWindow(native->window, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);

    if (GetWindowRect(native->window, &result.frame) == FALSE || GetClientRect(native->window, &client) == FALSE ||
        ClientToScreen(native->window, &clientTopLeft) == FALSE)
    {
        return result;
    }
    clientBottomRight = {client.right, client.bottom};

    if (ClientToScreen(native->window, &clientBottomRight) == FALSE || monitor == nullptr || GetMonitorInfoW(monitor, &monitorInfo) == FALSE)
    {
        return result;
    }

    result.client = {clientTopLeft.x, clientTopLeft.y, clientBottomRight.x, clientBottomRight.y};
    result.monitor = monitorInfo.rcMonitor;
    result.style = static_cast<DWORD>(GetWindowLongPtrW(native->window, GWL_STYLE));
    result.extendedStyle = static_cast<DWORD>(GetWindowLongPtrW(native->window, GWL_EXSTYLE));
    result.dpi = GetDpiForWindow(native->window);
    result.popupStyle = (result.style & WS_POPUP) != 0 && (result.style & (WS_CAPTION | WS_THICKFRAME)) == 0;
    result.visibleStyle = (result.style & WS_VISIBLE) != 0;
    result.fullscreenBounds = EqualRect(&result.frame, &result.monitor) != FALSE && EqualRect(&result.client, &result.monitor) != FALSE;
    result.taskbarEligible = (result.extendedStyle & WS_EX_APPWINDOW) != 0 && (result.extendedStyle & WS_EX_TOOLWINDOW) == 0 && result.visibleStyle &&
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
    const Desktop::Types::LogicalSize logical = window.clientSize();
    const Desktop::Types::PixelSize framebuffer = window.framebufferSize();
    const Desktop::Types::ScreenPosition position = window.clientPosition();
    const Desktop::Types::ContentScale scale = window.contentScale();
    const Desktop::Types::Dpi dpi = window.effectiveDpi();
    const Desktop::Types::FullscreenState fullscreen = window.fullscreenState();
    const ManualNativeWindowState native = manualNativeWindowState(window);
    const std::optional<Desktop::Types::Display::Mode> requestedMode = std::visit(
        [](const auto &value) -> std::optional<Desktop::Types::Display::Mode>
        {
            if constexpr (requires { value.displayMode; })
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
            if constexpr (std::is_same_v<Request, Desktop::Types::WindowedModeRequest>)
            {
                return 0;
            }
            else if constexpr (std::is_same_v<Request, Desktop::Types::BorderlessFullscreenRequest>)
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
                    if constexpr (requires { value.targetMonitor; })
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
    recordManualModeTransition(context, window, std::format("{}:before", label), request);
    IO::Types::Status status = window.setWindowMode(request);
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
        {
            return;
        }
        RECT workArea{};

        if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0) == FALSE)
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
        if (handle_ == nullptr)
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

        if (text == lastText_)
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

inline constexpr std::string_view kManualInteropText =
    "GameWIP strict Unicode: caf\xC3\xA9 | \xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E | \xF0\x9F\x98\x80";

[[nodiscard]] Desktop::Types::DataTransfer::Image manualInteropImage()
{
    Desktop::Types::DataTransfer::Image image{{128, 128}, std::vector<std::byte>(std::size_t{128} * 128 * 4)};

    for (std::size_t y = 0; y < 128; ++y)
    {
        for (std::size_t x = 0; x < 128; ++x)
        {
            const std::size_t offset = (y * 128 + x) * 4;
            const bool left = x < 64;
            const bool top = y < 64;
            image.rgba8[offset] = std::byte{static_cast<unsigned char>((top && left) || (!top && !left) ? 255 : 0)};
            image.rgba8[offset + 1] = std::byte{static_cast<unsigned char>(!left ? 255 : 0)};
            image.rgba8[offset + 2] = std::byte{static_cast<unsigned char>(!top ? 255 : 0)};
            image.rgba8[offset + 3] = std::byte{255};
        }
    }

    return image;
}

[[nodiscard]] IO::Types::Status writeManualInteropBitmap(const std::filesystem::path &path, const Desktop::Types::DataTransfer::Image &image)
{
    BITMAPFILEHEADER file{};
    file.bfType = 0x4D42;
    file.bfOffBits = sizeof(file) + sizeof(BITMAPINFOHEADER);
    file.bfSize = file.bfOffBits + static_cast<DWORD>(image.rgba8.size());
    BITMAPINFOHEADER bitmap{};
    bitmap.biSize = sizeof(bitmap);
    bitmap.biWidth = static_cast<LONG>(image.size.width);
    bitmap.biHeight = -static_cast<LONG>(image.size.height);
    bitmap.biPlanes = 1;
    bitmap.biBitCount = 32;
    bitmap.biCompression = BI_RGB;
    std::vector<std::byte> bytes(file.bfSize);
    std::ranges::copy(std::as_bytes(std::span{&file, 1}), bytes.begin());
    std::ranges::copy(std::as_bytes(std::span{&bitmap, 1}), bytes.begin() + sizeof(file));

    for (std::size_t offset = 0; offset < image.rgba8.size(); offset += 4)
    {
        const std::size_t destination = file.bfOffBits + offset;
        bytes[destination] = image.rgba8[offset + 2];
        bytes[destination + 1] = image.rgba8[offset + 1];
        bytes[destination + 2] = image.rgba8[offset];
        bytes[destination + 3] = image.rgba8[offset + 3];
    }

    return GameWIP::FileSystem::writeAllBytes(path, bytes).status;
}

/// @brief Shows channel order, row order, and alpha against a checkerboard in the target.
void paintManualImagePreview(const Desktop::Window &window, const Desktop::Types::DataTransfer::Image &image)
{
    if (image.size.width == 0 || image.size.height == 0 || image.rgba8.empty())
    {
        return;
    }
    const auto native = Desktop::Native::Win32::getHandle(window);

    if (!native.status.ok())
    {
        return;
    }
    constexpr std::size_t extent = 128;
    std::array<std::byte, extent * extent * 4> preview{};

    for (std::size_t y = 0; y < extent; ++y)
    {
        for (std::size_t x = 0; x < extent; ++x)
        {
            const std::size_t source = ((y * image.size.height / extent) * image.size.width + x * image.size.width / extent) * 4;

            if (source + 3 >= image.rgba8.size())
            {
                return;
            }
            const unsigned int alpha = std::to_integer<unsigned int>(image.rgba8[source + 3]);
            const unsigned int background = ((x / 8 + y / 8) % 2) == 0 ? 192 : 240;

            for (std::size_t channel = 0; channel < 3; ++channel)
            {
                const auto color = std::to_integer<unsigned int>(image.rgba8[source + 2 - channel]);
                preview[(y * extent + x) * 4 + channel] = std::byte{static_cast<unsigned char>((color * alpha + background * (255 - alpha)) / 255)};
            }
        }
    }
    BITMAPINFO bitmap{};
    bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap.bmiHeader.biWidth = 128;
    bitmap.bmiHeader.biHeight = -128;
    bitmap.bmiHeader.biPlanes = 1;
    bitmap.bmiHeader.biBitCount = 32;
    const HDC dc = GetDC(native->window);

    if (dc == nullptr)
    {
        return;
    }
    StretchDIBits(dc, 18, 170, 128, 128, 0, 0, 128, 128, preview.data(), &bitmap, DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(native->window, dc);
}

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
    {
        return;
    }
    const auto native = Desktop::Native::Win32::getHandle(window);

    if (!native.status.ok() || native->window == nullptr)
    {
        return;
    }

    RECT client{};

    if (GetClientRect(native->window, &client) == FALSE)
    {
        return;
    }
    HDC device = GetDC(native->window);

    if (device == nullptr)
    {
        return;
    }

    const UINT dpi = GetDpiForWindow(native->window);
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
        {
            SelectObject(device, previousFont);
        }
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
    {
        SelectObject(device, previousPen);
    }

    if (edgePen != nullptr)
    {
        DeleteObject(edgePen);
    }

    ReleaseDC(native->window, device);
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
struct ManualObservationCounts
{
    std::size_t passed = 0;
    std::size_t failed = 0;
    std::size_t skipped = 0;
};
ManualObservationCounts manualObservationCounts;

void recordManualAnswer(
    TestSupport::Context &context,
    std::string_view name,
    TestSupport::Types::Reporting::ManualAnswer answer,
    std::string_view skipReason = {})
{
    switch (answer)
    {
    case TestSupport::Types::Reporting::ManualAnswer::Yes:
        ++manualObservationCounts.passed;
        context.pass(name);
        return;
    case TestSupport::Types::Reporting::ManualAnswer::No:
        ++manualObservationCounts.failed;
        context.fail(name, "manual check rejected by user");
        return;
    case TestSupport::Types::Reporting::ManualAnswer::Skipped:
        ++manualObservationCounts.skipped;
        context.skip(name, skipReason.empty() ? "no skip reason supplied" : skipReason);
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
    if (!manualCaseSelected(name))
    {
        return TestSupport::Types::Reporting::ManualAnswer::Skipped;
    }
    ++manualCasesSelected;

    if (manualStatusWindow != nullptr)
    {
        manualStatusWindow->setScenario(name, question);

        if (!observe)
        {
            manualStatusWindow->setObservation({});
        }
        paintManualValidationSurface(window, surfaceLayout);
        manualStatusWindow->refresh(window);
    }
    std::atomic<bool> answered = false;
    TestSupport::Types::Reporting::ManualAnswer answer = TestSupport::Types::Reporting::ManualAnswer::Skipped;
    std::string skipReason;
    std::jthread promptThread(
        [&]
        {
            answer = TestSupport::promptManualCheck(question, skipReason);
            answered.store(true, std::memory_order_release);
            static_cast<void>(window.wakeEventWait());
        });

    IO::Types::Status pumpFailure;

    while (!answered.load(std::memory_order_acquire))
    {
        const Desktop::Types::Events::PumpResult pump = Desktop::Events::wait(std::chrono::milliseconds{50});

        if (!pump.status.ok() && pumpFailure.ok())
        {
            pumpFailure = pump.status;
        }
        paintManualValidationSurface(window, surfaceLayout);

        if (observe)
        {
            observe();
        }

        if (manualStatusWindow != nullptr)
        {
            manualStatusWindow->refresh(window);
        }
    }
    promptThread.join();
    const Desktop::Types::Events::PumpResult finalPump = Desktop::Events::poll();

    if (!finalPump.status.ok() && pumpFailure.ok())
    {
        pumpFailure = finalPump.status;
    }
    paintManualValidationSurface(window, surfaceLayout);

    if (observe)
    {
        observe();
    }

    if (manualStatusWindow != nullptr)
    {
        manualStatusWindow->refresh(window);
    }

    if (!pumpFailure.ok())
    {
        ++manualObservationCounts.failed;
        context.fail(name, pumpFailure.message);
        return TestSupport::Types::Reporting::ManualAnswer::No;
    }
    recordManualAnswer(context, name, answer, skipReason);
    return answer;
}

/// @brief Reports a failed setup operation and returns whether the manual scenario may continue.
[[nodiscard]] bool requireManualStatus(TestSupport::Context &context, std::string_view name, const IO::Types::Status &status)
{
    if (status.ok())
    {
        return true;
    }

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

/// @brief Keeps owner-thread native messages flowing for a bounded manual preparation interval.
void pumpManualPreparation(std::chrono::milliseconds duration)
{
    const auto deadline = std::chrono::steady_clock::now() + duration;

    while (std::chrono::steady_clock::now() < deadline)
    {
        static_cast<void>(Desktop::Events::wait(std::chrono::milliseconds{50}));
    }
}

/// @brief Opens a consistently sized visible Window for one manual scenario.
[[nodiscard]] bool openManualWindow(
    TestSupport::Context &context,
    Desktop::Window &window,
    std::string_view title,
    Desktop::Types::Description description = {})
{
    description.title = title;
    description.windowed.clientSize = {960, 540};
    description.activation.visible = true;
    description.activation.requestFocus = true;

    if (!requireManualStatus(context, "manual Window setup", window.open(description)))
    {
        return false;
    }

    if (!requireManualStatus(context, "manual Window presentation", window.show()))
    {
        return false;
    }
    const Desktop::Types::Events::PumpResult pump = Desktop::Events::poll();

    if (!requireManualStatus(context, "manual Window event pump", pump.status))
    {
        return false;
    }

    if (!window.isOpen() || !window.visible())
    {
        context.fail("manual Window presentation", "Window did not remain open and visible after the presentation pump");
        return false;
    }
    paintManualValidationSurface(window);

    if (manualStatusWindow != nullptr)
    {
        manualStatusWindow->refresh(window);
    }

    pumpManualPreparation(std::chrono::milliseconds{150});

    paintManualValidationSurface(window);

    if (manualStatusWindow != nullptr)
    {
        manualStatusWindow->refresh(window);
    }

    return true;
}

/// @brief Returns whether an opt-in suite should run and records the unattended skip otherwise.
[[nodiscard]] bool beginManualSuite(TestSupport::Context &context, const GameWIP::Test::DesktopTestOptions &options, std::string_view name)
{
    if (options.enableManualTests)
    {
        return true;
    }
    context.skip(name, "disabled by DesktopTestOptions");
    return false;
}
