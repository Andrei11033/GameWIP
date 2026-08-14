/// @file window_test.cpp
/// @brief Deterministic Window checks and opt-in visible manual validation.

#include "validation/tests/window/window_test.h"

#include "test_support/test_support.h"
#include "window/renderer_bridge.h"
#include "window/native/win32.h"
#include "window/window.h"

#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#ifndef WINDOW_INTERNAL_TEST_HOOKS
#define WINDOW_INTERNAL_TEST_HOOKS 0
#endif

#if WINDOW_INTERNAL_TEST_HOOKS
#include "window/internal/window_test_hooks.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <format>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "validation/tests/window/window_api_migration.h"

namespace
{
    namespace IO = GameWIP::IO;
    namespace TestSupport = GameWIP::TestSupport;
    namespace Window = GameWIP::Window;
    using ErrorCode = IO::Types::ErrorCode;

    inline constexpr wchar_t kManualTestAppUserModelId[] = L"GameWIP.Validation.WindowManualTests";

    static_assert(!std::is_move_constructible_v<Window::Window>);
    static_assert(!std::is_move_assignable_v<Window::Window>);
    static_assert(!std::is_copy_constructible_v<Window::Window>);
    static_assert(!std::is_copy_assignable_v<Window::Window>);
    static_assert(noexcept(Window::pollEvents()));
    static_assert(noexcept(Window::waitEvents()));
    static_assert(noexcept(std::declval<Window::Window &>().close()));
    static_assert(noexcept(Window::Renderer::getDisplayColorInfo({})));
    static_assert(noexcept(Window::Renderer::getWindowDisplayColorInfo(std::declval<const Window::Window &>())));
    static_assert(std::is_same_v<decltype(Window::Types::FilesDroppedEvent{}.paths)::value_type, GameWIP::FileSystem::Types::Path>);

    /// @brief Converts diagnostic UTF-8 to native text for the manual companion Window.
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

    /// @brief Captures actual HWND state so cached-state bugs remain visible to manual validation.
    [[nodiscard]] ManualNativeWindowState manualNativeWindowState(const Window::Window &window)
    {
        ManualNativeWindowState result;
        if (!window.isOpen())
            return result;
        const Window::Native::Win32::HandleResult native = Window::Native::Win32::getHandle(window);
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
        if (ClientToScreen(native.handle.window, &clientBottomRight) == FALSE || monitor == nullptr ||
            GetMonitorInfoW(monitor, &monitorInfo) == FALSE)
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
        result.taskbarEligible = (result.extendedStyle & WS_EX_APPWINDOW) != 0 && (result.extendedStyle & WS_EX_TOOLWINDOW) == 0 &&
                                 result.visibleStyle && IsWindowVisible(native.handle.window) != FALSE &&
                                 GetWindow(native.handle.window, GW_OWNER) == nullptr;
        result.valid = true;
        return result;
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

        void refresh(const Window::Window &window)
        {
            if (handle_ == nullptr)
                return;

            const Window::Types::LogicalSize logical = window.clientSize();
            const Window::Types::PixelSize framebuffer = window.framebufferSize();
            const Window::Types::ScreenPosition position = window.clientPosition();
            const Window::Types::ContentScale scale = window.contentScale();
            const Window::Types::Dpi dpi = window.effectiveDpi();
            const Window::Types::WindowControls controls = window.windowControls();
            const Window::Types::FullscreenInfo fullscreen = window.fullscreenInfo();
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
                "processAppId=GameWIP.Validation.WindowManualTests\r\n\r\n"
                "The diagnostics refresh while the console prompt is waiting.",
                scenario_,
                expected_,
                observation_.empty() ? "No event-specific observation yet." : observation_,
                window.isOpen(),
                window.isVisible(),
                window.isFocused(),
                window.isMinimized(),
                window.isMaximized(),
                window.closeRequested(),
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
                window.isResizable(),
                window.ownerId().value,
                window.acceptsFileDrops(),
                window.isAlwaysOnTop(),
                window.isUserInteractionEnabled(),
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
    void paintManualValidationSurface(const Window::Window &window, ManualSurfaceLayout layout = ManualSurfaceLayout::Standard)
    {
        if (!window.isOpen())
            return;
        const Window::Native::Win32::HandleResult native = Window::Native::Win32::getHandle(window);
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

    template <typename Payload> [[nodiscard]] bool consumeEventOfType(Window::Window &window)
    {
        bool found = false;
        Window::Types::Event event;
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
        Window::Window &window,
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
            const Window::Types::EventPumpResult pump = Window::waitEvents(std::chrono::milliseconds{50});
            if (!pump.status.ok() && pumpFailure.ok())
                pumpFailure = pump.status;
            if (observe)
                observe();
            paintManualValidationSurface(window, surfaceLayout);
            if (manualStatusWindow != nullptr)
                manualStatusWindow->refresh(window);
        }
        promptThread.join();
        const Window::Types::EventPumpResult finalPump = Window::pollEvents();
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
        Window::Window &window,
        std::string_view title,
        Window::Types::Description description = {})
    {
        description.title = title;
        description.clientSize = {960, 540};
        description.visible = true;
        description.requestFocus = true;
        return requireManualStatus(context, "manual Window setup", window.open(description));
    }

    /// @brief Returns whether an opt-in suite should run and records the unattended skip otherwise.
    [[nodiscard]] bool beginManualSuite(TestSupport::Context &context, const GameWIP::Test::WindowTestOptions &options, std::string_view name)
    {
        if (options.enableManualTests)
            return true;
        context.skip(name, "disabled by WindowTestOptions");
        return false;
    }

    /// @brief Keeps owner-thread native messages flowing for a bounded manual preparation interval.
    void pumpManualPreparation(std::chrono::milliseconds duration)
    {
        const auto deadline = std::chrono::steady_clock::now() + duration;
        while (std::chrono::steady_clock::now() < deadline)
            static_cast<void>(Window::waitEvents(std::chrono::milliseconds{50}));
    }

    /// @brief Exercises the core real visible-window lifecycle required before submission.
    void testManualVisibleLifecycle(TestSupport::Context &context, const GameWIP::Test::WindowTestOptions &options)
    {
        if (!beginManualSuite(context, options, "Window visible lifecycle"))
            return;

        context.manual(
            "Window manual tests require a normal interactive Windows desktop. Answer yes, no, or skip at each prompt. "
            "Restore any changed desktop state before the suite ends.");

        Window::Types::Description description;
        description.title = "GameWIP Window manual validation";
        description.clientSize = {960, 540};
        description.visible = true;
        description.requestFocus = true;

        Window::Window window;
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
        recordManualCheck(
            context,
            window,
            "visible Window minimize",
            "Did the Window minimize correctly? Restore it from the taskbar before answering.");
        static_cast<void>(context.expectTrue("manual Window restore after minimize succeeds", window.restore().ok()));
        static_cast<void>(window.requestFocus());

        recordManualCheck(
            context,
            window,
            "system close request",
            "Click the Window close button once. Does the Window remain alive while recording a close request?");
        static_cast<void>(context.expectTrue("system close request becomes sticky", window.closeRequested()));
        if (window.closeRequested())
            static_cast<void>(context.expectTrue("system close request clears", window.clearCloseRequest().ok()));
        static_cast<void>(context.expectTrue("cleared close request leaves Window open", window.isOpen()));

        recordManualCheck(
            context,
            window,
            "second system close request",
            "Click the Window close button again. Does the Window again remain alive pending explicit close?");
        static_cast<void>(context.expectTrue("second system close request becomes sticky", window.closeRequested()));
        static_cast<void>(context.expectTrue("manual Window explicit close succeeds", window.close().ok()));
        static_cast<void>(context.expectFalse("manual Window is closed", window.isOpen()));

        Window::Window reopened;
        description.visible = false;
        description.requestFocus = false;
        static_cast<void>(context.expectTrue("manual Window reopens after explicit close", reopened.open(description).ok()));
        static_cast<void>(context.expectTrue("reopened manual Window closes cleanly", reopened.close().ok()));
    }

    /// @brief Exercises visible multi-window ownership, routing, activation, and native relationship behavior.
    void testManualMultipleWindows(TestSupport::Context &context, const GameWIP::Test::WindowTestOptions &options)
    {
        if (!beginManualSuite(context, options, "Window multiple-window scenarios"))
            return;

        Window::Window owner;
        if (!openManualWindow(context, owner, "GameWIP manual owner Window"))
            return;

        Window::Types::Description childDescription;
        childDescription.owner = owner.id();
        Window::Window child;
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
        recordManualCheck(
            context,
            owner,
            "remaining Window after peer close",
            "Does the owner remain fully operational after the other Window closes?");

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
    void testManualCustomChrome(TestSupport::Context &context, const GameWIP::Test::WindowTestOptions &options)
    {
        if (!beginManualSuite(context, options, "Window custom chrome"))
            return;

        const Window::Types::Capabilities capabilities = Window::getCapabilities().capabilities;
        if (!capabilities.supports(Window::Types::Capability::CustomChrome))
        {
            context.skip("Window custom chrome", "backend does not advertise CustomChrome");
            return;
        }

        Window::Types::Description description;
        description.decoration = Window::Types::DecorationMode::Custom;
        Window::Window window;
        if (!openManualWindow(context, window, "GameWIP custom chrome validation", description))
            return;

        const std::array draggable{Window::Types::LogicalRect{{0, 0}, {760, 48}}};
        Window::Types::CustomChromeLayout layout;
        layout.draggableRegions = draggable;
        layout.systemMenuRegion = Window::Types::LogicalRect{{0, 0}, {48, 48}};
        layout.minimizeButtonRegion = Window::Types::LogicalRect{{800, 0}, {48, 48}};
        layout.maximizeButtonRegion = Window::Types::LogicalRect{{848, 0}, {48, 48}};
        layout.closeButtonRegion = Window::Types::LogicalRect{{896, 0}, {64, 48}};
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

        const std::array replacement{Window::Types::LogicalRect{{0, 48}, {640, 40}}};
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
    void testManualLayeredAndPointer(TestSupport::Context &context, const GameWIP::Test::WindowTestOptions &options)
    {
        if (!beginManualSuite(context, options, "Window layered and pointer behavior"))
            return;

        const Window::Types::Capabilities capabilities = Window::getCapabilities().capabilities;
        Window::Window window;
        if (!openManualWindow(context, window, "GameWIP opacity and pointer validation"))
            return;

        if (capabilities.supports(Window::Types::Capability::Opacity))
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

        if (capabilities.supports(Window::Types::Capability::PointerClickThrough))
        {
            context.manual("Place another interactive application beneath the validation Window before continuing.");
            static_cast<void>(context.expectTrue(
                "whole-Window click-through applies",
                window.setPointerInputLayout({.mode = Window::Types::PointerInputMode::ClickThrough}).ok()));
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

        const std::array region{Window::Types::LogicalRect{{20, 20}, {120, 80}}};
        for (const Window::Types::PointerInputMode mode :
             {Window::Types::PointerInputMode::AcceptRegions, Window::Types::PointerInputMode::IgnoreRegions})
        {
            Window::Types::PointerInputLayout regionLayout{.mode = mode, .regions = region};
            const IO::Types::Status status = window.setPointerInputLayout(regionLayout);
            if (capabilities.supports(Window::Types::Capability::PointerRegions))
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

        Window::Types::Description alphaDescription;
        alphaDescription.transparentFramebuffer = true;
        Window::Window alpha;
        const IO::Types::Status alphaStatus = alpha.open(alphaDescription);
        if (capabilities.supports(Window::Types::Capability::TransparentFramebuffer))
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
    void testManualDpiAndCoordinates(TestSupport::Context &context, const GameWIP::Test::WindowTestOptions &options)
    {
        if (!beginManualSuite(context, options, "Window DPI and coordinates"))
            return;

        Window::Window window;
        Window::Types::Description description;
        description.dpiResizePolicy = Window::Types::DpiResizePolicy::PreserveLogicalClientSize;
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
            window.setDpiResizePolicy(Window::Types::DpiResizePolicy::PreservePhysicalClientSize).ok()));
        recordManualCheck(
            context,
            window,
            "mixed-DPI physical-size policy",
            "Move across the same DPI boundaries. Do framebuffer pixels remain stable while logical size changes?");

        const Window::Types::LogicalSize size = window.clientSize();
        const std::array points{
            Window::Types::LogicalPosition{0, 0},
            Window::Types::LogicalPosition{static_cast<std::int32_t>(size.width - 1), 0},
            Window::Types::LogicalPosition{0, static_cast<std::int32_t>(size.height - 1)},
            Window::Types::LogicalPosition{static_cast<std::int32_t>(size.width - 1), static_cast<std::int32_t>(size.height - 1)}};
        bool conversionsRoundTrip = true;
        for (const Window::Types::LogicalPosition point : points)
        {
            const Window::Types::ScreenPositionResult screen = window.clientToScreen(point);
            if (!screen.status.ok())
            {
                conversionsRoundTrip = false;
                break;
            }
            const Window::Types::LogicalPositionResult logical = window.screenToClient(screen.position);
            conversionsRoundTrip = conversionsRoundTrip && logical.status.ok() && logical.position == point;
        }
        static_cast<void>(context.expectTrue("client/screen edge conversions round-trip", conversionsRoundTrip));
        recordManualCheck(
            context,
            window,
            "mixed-DPI coordinate rounding",
            "At each available DPI scale, do client/screen edge coordinates follow the expected integral-pixel rounding without visible drift?");

        const Window::Types::MonitorListResult monitors = Window::getMonitors();
        bool monitorRectsValid = monitors.status.ok() && !monitors.monitors.empty();
        for (const Window::Types::MonitorInfo &monitor : monitors.monitors)
        {
            monitorRectsValid = monitorRectsValid && monitor.bounds.size.width > 0 && monitor.bounds.size.height > 0 &&
                                monitor.workArea.size.width > 0 && monitor.workArea.size.height > 0;
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
    void testManualCursor(TestSupport::Context &context, const GameWIP::Test::WindowTestOptions &options)
    {
        if (!beginManualSuite(context, options, "Window cursor behavior"))
            return;

        const Window::Types::Capabilities capabilities = Window::getCapabilities().capabilities;
        Window::Window window;
        if (!openManualWindow(context, window, "GameWIP cursor validation"))
            return;

        constexpr std::array shapes{
            Window::Types::CursorShape::Arrow,
            Window::Types::CursorShape::Text,
            Window::Types::CursorShape::Crosshair,
            Window::Types::CursorShape::Hand,
            Window::Types::CursorShape::Help,
            Window::Types::CursorShape::Wait,
            Window::Types::CursorShape::Progress,
            Window::Types::CursorShape::Move,
            Window::Types::CursorShape::ResizeAll,
            Window::Types::CursorShape::ResizeHorizontal,
            Window::Types::CursorShape::ResizeVertical,
            Window::Types::CursorShape::ResizeDiagonalNorthWestSouthEast,
            Window::Types::CursorShape::ResizeDiagonalNorthEastSouthWest,
            Window::Types::CursorShape::NotAllowed};
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
            const Window::Types::LogicalSize size = window.clientSize();
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

        for (const auto [mode, capability, name] : std::array{
                 std::tuple{Window::Types::CursorMode::Hidden, Window::Types::Capability::Count, std::string_view{"hidden"}},
                 std::tuple{Window::Types::CursorMode::Confined, Window::Types::Capability::CursorConfinement, std::string_view{"confined"}},
                 std::tuple{
                     Window::Types::CursorMode::HiddenConfined,
                     Window::Types::Capability::CursorConfinement,
                     std::string_view{"hidden-confined"}},
                 std::tuple{Window::Types::CursorMode::Relative, Window::Types::Capability::RelativeCursor, std::string_view{"relative"}}})
        {
            if (capability != Window::Types::Capability::Count && !capabilities.supports(capability))
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
            static_cast<void>(window.setCursorMode(Window::Types::CursorMode::Normal));
        }

        if (capabilities.supports(Window::Types::Capability::CursorWarping))
        {
            const Window::Types::LogicalSize size = window.clientSize();
            for (const Window::Types::LogicalPosition point :
                 {Window::Types::LogicalPosition{0, 0},
                  Window::Types::LogicalPosition{static_cast<std::int32_t>(size.width - 1), static_cast<std::int32_t>(size.height - 1)}})
            {
                static_cast<void>(context.expectTrue("cursor warp succeeds", window.setCursorPosition(point).ok()));
                const Window::Types::LogicalPositionResult actual = window.cursorPosition();
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
        static_cast<void>(window.setCursorMode(Window::Types::CursorMode::Normal));
        static_cast<void>(window.close());
    }

    /// @brief Exercises file-drop events and native shell-facing Window controls.
    void testManualFilesAndShell(TestSupport::Context &context, const GameWIP::Test::WindowTestOptions &options)
    {
        if (!beginManualSuite(context, options, "Window files and shell behavior"))
            return;

        const Window::Types::Capabilities capabilities = Window::getCapabilities().capabilities;
        Window::Types::Description description;
        description.fileDropEnabled = capabilities.supports(Window::Types::Capability::FileDrop);
        Window::Window window;
        if (!openManualWindow(context, window, "GameWIP file and shell validation", description))
            return;

        if (capabilities.supports(Window::Types::Capability::FileDrop))
        {
            const auto runDrop = [&](std::string_view name, std::string_view instruction, std::size_t minimumPaths)
            {
                window.clearEvents();
                std::size_t groupedEvents = 0;
                std::size_t pathCount = 0;
                const auto observeDrops = [&]
                {
                    Window::Types::Event event;
                    while (window.popEvent(event))
                    {
                        if (const auto *drop = event.getIf<Window::Types::FilesDroppedEvent>())
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
                    context.expectFalse("disabled file drops queue no event", consumeEventOfType<Window::Types::FilesDroppedEvent>(window)));
        }
        else
        {
            context.skip("Window file drops", "backend does not advertise FileDrop");
        }

        if (capabilities.supports(Window::Types::Capability::WindowIcon))
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
            const std::array images{Window::Types::IconImageView{{16, 16}, smallPixels}, Window::Types::IconImageView{{32, 32}, largePixels}};
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
                Window::Types::WindowControls controls = window.windowControls();
                controls.maximizable = maximizable;
                const IO::Types::Status controlsStatus = window.setWindowControls(controls);
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
        Window::Types::WindowControls maximizableControls = window.windowControls();
        maximizableControls.maximizable = true;
        static_cast<void>(window.setWindowControls(maximizableControls));
        static_cast<void>(context.expectTrue("maximizable-then-nonresizable transition is rejected", !window.setResizable(false).ok()));
        static_cast<void>(window.setWindowControls({.closable = true, .minimizable = true, .maximizable = false}));
        static_cast<void>(window.setResizable(false));
        maximizableControls = window.windowControls();
        maximizableControls.maximizable = true;
        static_cast<void>(
            context.expectTrue("nonresizable-then-maximizable transition is rejected", !window.setWindowControls(maximizableControls).ok()));
        static_cast<void>(window.setResizable(true));
        static_cast<void>(window.setWindowControls({}));
        Window::Types::WindowControls independentControls = window.windowControls();
        independentControls.closable = false;
        independentControls.minimizable = false;
        static_cast<void>(
            context.expectTrue("close and minimize controls disable independently", window.setWindowControls(independentControls).ok()));
        static_cast<void>(context.expectFalse("close control cached disabled", window.windowControls().closable));
        static_cast<void>(context.expectFalse("minimize control cached disabled", window.windowControls().minimizable));
        recordManualCheck(
            context,
            window,
            "standard control combinations",
            "The diagnostics must show close=false, minimize=false, maximize=true, resizable=true. Are only the close and minimize controls disabled "
            "in the native frame?");
        static_cast<void>(window.setWindowControls({}));

        Window::Types::Description ownedDescription;
        ownedDescription.owner = window.id();
        Window::Window owned;
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

    /// @brief Exercises fullscreen transitions, monitor movement, and live topology recovery.
    void testManualFullscreenAndTopology(TestSupport::Context &context, const GameWIP::Test::WindowTestOptions &options)
    {
        if (!beginManualSuite(context, options, "Window fullscreen and display topology"))
            return;

        const Window::Types::Capabilities capabilities = Window::getCapabilities().capabilities;
        const Window::Types::MonitorListResult monitors = Window::getMonitors();
        if (!monitors.status.ok() || monitors.monitors.empty())
        {
            context.fail("fullscreen monitor enumeration", monitors.status.message);
            return;
        }

        Window::Window window;
        if (!openManualWindow(context, window, "GameWIP fullscreen validation"))
            return;
        const Window::Types::ScreenPosition savedPosition = window.clientPosition();
        const Window::Types::LogicalSize savedSize = window.clientSize();

        for (std::size_t index = 0; index < monitors.monitors.size(); ++index)
        {
            const Window::Types::MonitorInfo &monitor = monitors.monitors[index];
            Window::Types::ModeRequest borderless;
            borderless.mode = Window::Types::WindowMode::BorderlessFullscreen;
            borderless.monitor = monitor.id;
            if (requireManualStatus(context, "borderless fullscreen enters", window.setMode(borderless)))
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
                        "must show popupStyle=true and "
                        "fullscreenBounds=true. Press the Windows key: a separate GameWIP taskbar button must be present, or use Alt+Tab to verify "
                        "the Window is listed. On monitor {}/{} ({}), does it exactly cover the display and remain switchable without changing its "
                        "display mode?",
                        index + 1,
                        monitors.monitors.size(),
                        monitor.name));
                static_cast<void>(requireManualStatus(context, "borderless fullscreen leaves", window.setMode({})));
            }
            else
            {
                context.skip("borderless fullscreen monitor", "borderless-mode setup failed; see preceding status");
            }
        }
        static_cast<void>(context.expectEq("windowed size restores after borderless", savedSize, window.clientSize()));
        recordManualCheck(
            context,
            window,
            "windowed placement restoration",
            std::format("After fullscreen transitions, did the Window restore its saved placement near ({}, {})?", savedPosition.x, savedPosition.y));

        if (capabilities.supports(Window::Types::Capability::ExclusiveFullscreen))
        {
            const Window::Types::MonitorId monitor = window.currentMonitor();
            const Window::Types::DisplayModeResult currentMode = Window::getCurrentDisplayMode(monitor);
            const Window::Types::DisplayModeListResult availableModes = Window::getDisplayModes(monitor);
            if (currentMode.status.ok() && availableModes.status.ok() && !availableModes.displayModes.empty())
            {
                const auto selected = std::ranges::min_element(
                    availableModes.displayModes,
                    {},
                    [&](const Window::Types::DisplayMode &mode)
                    {
                        const std::uint64_t resolutionPenalty = mode.resolution == currentMode.displayMode.resolution ? 0 : std::uint64_t{1} << 48;
                        const std::uint64_t depthPenalty = mode.bitsPerPixel == currentMode.displayMode.bitsPerPixel ? 0 : std::uint64_t{1} << 40;
                        const std::uint64_t refreshDifference = mode.refreshRateMillihertz > currentMode.displayMode.refreshRateMillihertz
                                                                    ? mode.refreshRateMillihertz - currentMode.displayMode.refreshRateMillihertz
                                                                    : currentMode.displayMode.refreshRateMillihertz - mode.refreshRateMillihertz;
                        return resolutionPenalty + depthPenalty + refreshDifference;
                    });
                Window::Types::ModeRequest exclusive;
                exclusive.mode = Window::Types::WindowMode::ExclusiveFullscreen;
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
                const IO::Types::Status enterStatus = window.setMode(exclusive);
                if (requireManualStatus(context, "exclusive fullscreen enters", enterStatus))
                {
                    const ManualNativeWindowState native = manualNativeWindowState(window);
                    static_cast<void>(context.expectTrue("exclusive native HWND query succeeds", native.valid));
                    static_cast<void>(context.expectTrue("exclusive native popup style applies", native.popupStyle));
                    static_cast<void>(context.expectTrue("exclusive native bounds match active monitor", native.fullscreenBounds));
                    static_cast<void>(context.expectTrue("exclusive Window remains taskbar eligible", native.taskbarEligible));
                    bool sawExclusiveActive = window.isFocused() && !window.fullscreenInfo().suspended;
                    bool sawExclusiveSuspended = window.fullscreenInfo().suspended;
                    recordManualCheck(
                        context,
                        window,
                        "exclusive fullscreen activation cycle",
                        "Alt+Tab to the blue validation surface, back to the terminal, to the validation Window once more, and finally back to the "
                        "terminal to answer. While focused it must cover the display; while back at the terminal, suspended=true and a "
                        "windowed-sized "
                        "surface are expected. The test records both states automatically. Does that activation cycle behave correctly?",
                        [&]
                        {
                            const Window::Types::FullscreenInfo liveFullscreen = window.fullscreenInfo();
                            sawExclusiveActive = sawExclusiveActive || (window.isFocused() && !liveFullscreen.suspended);
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
                    const IO::Types::Status leaveStatus = window.setMode({});
                    if (requireManualStatus(context, "exclusive fullscreen leaves", leaveStatus))
                    {
                        recordManualCheck(
                            context,
                            window,
                            "exclusive display restoration",
                            "The diagnostics must show mode=0 and the original geometry. Was the original desktop display mode restored exactly?");
                    }
                }
                else
                {
                    context.skip("exclusive fullscreen activation cycle", "exclusive-mode setup failed; see preceding status");
                    context.skip("exclusive display restoration", "exclusive-mode setup failed; no display transition occurred");
                }

                Window::Types::ModeRequest unsupported = exclusive;
                unsupported.displayMode->resolution = {1, 1};
                const Window::Types::WindowMode previousMode = window.mode();
                const IO::Types::Status unsupportedStatus = window.setMode(unsupported);
                static_cast<void>(context.expectTrue("unsupported exact mode is rejected", !unsupportedStatus.ok()));
                static_cast<void>(context.expectEq("unsupported exact mode preserves Window mode", previousMode, window.mode()));

                const IO::Types::Status recoveryEnter = window.setMode(exclusive);
                if (recoveryEnter.ok())
                {
                    const TestSupport::Types::Reporting::ManualAnswer recovery = recordManualCheck(
                        context,
                        window,
                        "active exclusive target disconnect",
                        "Only if safe, disconnect/disable this exclusive-fullscreen monitor. Is desktop mode restored and the Window recovered "
                        "visibly on the surviving primary? Otherwise skip.");
                    if (recovery == TestSupport::Types::Reporting::ManualAnswer::Yes)
                    {
                        static_cast<void>(
                            context.expectEq("exclusive disconnect recovers windowed mode", Window::Types::WindowMode::Windowed, window.mode()));
                        static_cast<void>(
                            context.expectFalse("exclusive disconnect clears fullscreen monitor", window.fullscreenInfo().monitor.valid()));
                    }
                    if (window.mode() != Window::Types::WindowMode::Windowed)
                        static_cast<void>(window.setMode({}));
                }
                else
                {
                    context.skip("active exclusive target disconnect", "exclusive-mode setup failed; no active target to disconnect");
                }
            }
            else
            {
                context.skip("exclusive fullscreen", "no enumerated exact display mode is available for the current monitor");
            }
        }
        else
        {
            context.skip("exclusive fullscreen", "backend does not advertise ExclusiveFullscreen");
        }

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
            "If practical, connect/disconnect or enable/disable a non-active monitor. Does re-enumeration succeed and does the stale MonitorId fail "
            "safely? Skip if impractical.");

        Window::Types::ModeRequest activeBorderless;
        activeBorderless.mode = Window::Types::WindowMode::BorderlessFullscreen;
        activeBorderless.monitor = window.currentMonitor();
        if (window.setMode(activeBorderless).ok())
        {
            const TestSupport::Types::Reporting::ManualAnswer recovery = recordManualCheck(
                context,
                window,
                "active borderless target disconnect",
                "Only if safe, disconnect/disable this fullscreen monitor. Does the Window recover visibly in windowed mode on the surviving "
                "primary? Otherwise skip.");
            if (recovery == TestSupport::Types::Reporting::ManualAnswer::Yes)
            {
                static_cast<void>(
                    context.expectEq("borderless disconnect recovers windowed mode", Window::Types::WindowMode::Windowed, window.mode()));
                static_cast<void>(context.expectFalse("borderless disconnect clears fullscreen monitor", window.fullscreenInfo().monitor.valid()));
            }
            if (window.mode() != Window::Types::WindowMode::Windowed)
                static_cast<void>(window.setMode({}));
        }

        context.pass("fullscreen recovery event ordering and failure-state cleanup are covered deterministically");
        static_cast<void>(window.close());
    }

    /// @brief Exercises current SDR/HDR facts and user-driven advanced-color transitions.
    void testManualHdrAndAdvancedColor(TestSupport::Context &context, const GameWIP::Test::WindowTestOptions &options)
    {
        if (!beginManualSuite(context, options, "Window HDR and advanced color"))
            return;

        const Window::Types::MonitorListResult monitors = Window::getMonitors();
        if (!monitors.status.ok() || monitors.monitors.empty())
        {
            context.fail("HDR monitor enumeration", monitors.status.message);
            return;
        }

        Window::Window window;
        if (!openManualWindow(context, window, "GameWIP HDR validation"))
            return;

        for (const Window::Types::MonitorInfo &monitor : monitors.monitors)
        {
            const Window::Types::DisplayColorInfoResult direct = Window::Renderer::getDisplayColorInfo(monitor.id);
            static_cast<void>(context.expectTrue("monitor display-color query succeeds", direct.status.ok()));
            if (!direct.status.ok())
                continue;
            const auto &info = direct.info;
            static_cast<void>(context.expectEq("display-color query retains monitor identity", monitor.id, info.monitor));
            static_cast<void>(context.expectTrue("HDR enabled implies HDR supported", !info.hdrEnabled || info.hdrSupported));
            static_cast<void>(context.expectTrue("SDR-only display is not marked HDR enabled", info.hdrSupported || !info.hdrEnabled));
            static_cast<void>(context.expectTrue(
                "HDR-disabled display is not classified as HDR",
                info.hdrEnabled || info.activeColorSpace != Window::Types::DisplayColorSpace::Hdr10Pq));
            static_cast<void>(context.expectTrue(
                "HDR classification is truthful",
                !info.hdrEnabled || info.activeColorSpace == Window::Types::DisplayColorSpace::Hdr10Pq ||
                    info.activeColorSpace == Window::Types::DisplayColorSpace::Unknown));
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

        const Window::Types::DisplayColorInfoResult windowInfo = Window::Renderer::getWindowDisplayColorInfo(window);
        static_cast<void>(context.expectTrue("Window display-color query succeeds", windowInfo.status.ok()));
        if (windowInfo.status.ok())
            static_cast<void>(
                context.expectEq("Window display-color monitor matches current monitor", window.currentMonitor(), windowInfo.info.monitor));

        const TestSupport::Types::Reporting::ManualAnswer toggle = recordManualCheck(
            context,
            window,
            "HDR toggle in place",
            "If this display supports HDR, toggle HDR in Windows, return here, and verify the Window remains stable. Skip on SDR-only hardware.");
        if (toggle == TestSupport::Types::Reporting::ManualAnswer::Yes)
        {
            static_cast<void>(context.expectTrue("HDR toggle remains queryable", Window::Renderer::getWindowDisplayColorInfo(window).status.ok()));
            static_cast<void>(context.expectTrue(
                "HDR toggle delivers display configuration event",
                consumeEventOfType<Window::Types::DisplayConfigurationChangedEvent>(window)));
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
    void testManualModernWindowsCapabilities(TestSupport::Context &context, const GameWIP::Test::WindowTestOptions &options)
    {
        if (!beginManualSuite(context, options, "Window modern Windows capabilities"))
            return;

        const Window::Types::Capabilities capabilities = Window::getCapabilities().capabilities;
        Window::Window window;
        if (!openManualWindow(context, window, "GameWIP modern capability validation"))
            return;

        constexpr std::array effects{
            Window::Types::BackdropEffect::Automatic,
            Window::Types::BackdropEffect::MainWindow,
            Window::Types::BackdropEffect::TransientWindow,
            Window::Types::BackdropEffect::TabbedWindow};
        if (capabilities.supports(Window::Types::Capability::SystemBackdrop))
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
                static_cast<void>(context.expectTrue("system backdrop clears", window.setBackdropEffect(Window::Types::BackdropEffect::None).ok()));
            }
        }
        else
        {
            for (const Window::Types::BackdropEffect effect : effects)
            {
                static_cast<void>(
                    context.expectEq("unsupported backdrop effect is rejected", ErrorCode::Unsupported, window.setBackdropEffect(effect).code));
            }
            context.skip("system backdrop presentation", "runtime does not advertise SystemBackdrop");
        }
        static_cast<void>(window.close());

        Window::Types::Description alphaDescription;
        alphaDescription.transparentFramebuffer = true;
        Window::Window alpha;
        const IO::Types::Status alphaStatus = alpha.open(alphaDescription);
        if (capabilities.supports(Window::Types::Capability::TransparentFramebuffer))
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

    void testPassiveValuesAndClosedState(TestSupport::Context &context)
    {
        static_cast<void>(context.expectFalse("default window id is invalid", Window::Types::WindowId{}.valid()));
        static_cast<void>(context.expectTrue("nonzero window id is valid", Window::Types::WindowId{4}.valid()));
        static_cast<void>(context.expectFalse("default monitor id is invalid", Window::Types::MonitorId{}.valid()));
        static_cast<void>(context.expectTrue(
            "geometry values compare structurally",
            Window::Types::LogicalRect{{-4, 8}, {10, 12}} == Window::Types::LogicalRect{{-4, 8}, {10, 12}}));
        static_cast<void>(context.expectEq(
            "hit-mask words round each row independently",
            std::size_t{4},
            Window::Renderer::requiredPointerHitMaskWords({33, 2})));
        static_cast<void>(
            context.expectEq("empty hit-mask extent is invalid", std::size_t{0}, Window::Renderer::requiredPointerHitMaskWords({0, 8})));
#if INTERNAL_WINDOW_TEST_HOOKS
        static_cast<void>(context.expectEq(
            "60000/1001 refresh rounds to millihertz",
            std::uint32_t{59'940},
            Window::TestHooks::refreshRateMillihertz(60'000, 1'001)));
        static_cast<void>(
            context.expectEq("unknown rational refresh remains zero", std::uint32_t{0}, Window::TestHooks::refreshRateMillihertz(60'000, 0)));
        static_cast<void>(context.expectEq(
            "rational refresh conversion saturates",
            std::numeric_limits<std::uint32_t>::max(),
            Window::TestHooks::refreshRateMillihertz(std::numeric_limits<std::uint32_t>::max(), 1)));
        const Window::Types::DisplayMode exactMode{{1920, 1080}, 60'000, 32, false};
        static_cast<void>(context.expectTrue(
            "exclusive comparator accepts an exact native mode",
            Window::TestHooks::exactNativeDisplayModeMatches(exactMode, 1920, 1080, 60, 32, false)));
        Window::Types::DisplayMode fractionalMode = exactMode;
        fractionalMode.refreshRateMillihertz = 59'940;
        static_cast<void>(context.expectFalse(
            "exclusive comparator rejects a nearest integer-Hz mode",
            Window::TestHooks::exactNativeDisplayModeMatches(fractionalMode, 1920, 1080, 60, 32, false)));
        const Window::TestHooks::DpiTransitionResult preserveLogical =
            Window::TestHooks::calculateDpiTransition({800, 600}, {800, 600}, 144, Window::Types::DpiResizePolicy::PreserveLogicalClientSize);
        static_cast<void>(
            context.expectEq("logical-size DPI policy preserves logical extent", Window::Types::LogicalSize{800, 600}, preserveLogical.logicalSize));
        static_cast<void>(
            context.expectEq("logical-size DPI policy scales framebuffer", Window::Types::PixelSize{1200, 900}, preserveLogical.framebufferSize));
        const Window::TestHooks::DpiTransitionResult preservePhysical =
            Window::TestHooks::calculateDpiTransition({800, 600}, {800, 600}, 144, Window::Types::DpiResizePolicy::PreservePhysicalClientSize);
        static_cast<void>(context.expectEq(
            "physical-size DPI policy recalculates logical extent",
            Window::Types::LogicalSize{533, 400},
            preservePhysical.logicalSize));
        static_cast<void>(
            context.expectEq("physical-size DPI policy preserves framebuffer", Window::Types::PixelSize{800, 600}, preservePhysical.framebufferSize));
#endif

        const Window::Types::CapabilitiesResult capabilities = Window::getCapabilities();
        static_cast<void>(context.expectTrue("capability query succeeds", capabilities.status.ok()));
        static_cast<void>(
            context.expectTrue("Win32 supports multiple windows", capabilities.capabilities.supports(Window::Types::Capability::MultipleWindows)));
        static_cast<void>(context.expectFalse("Count is not a capability", capabilities.capabilities.supports(Window::Types::Capability::Count)));

        const Window::Types::EventPumpResult idlePoll = Window::pollEvents();
        static_cast<void>(context.expectTrue("poll with no windows is a successful no-op", idlePoll.status.ok()));
        const Window::Types::EventPumpResult badWait = Window::waitEvents(std::chrono::milliseconds{-2});
        static_cast<void>(context.expectEq("timeout below forever sentinel is invalid", ErrorCode::InvalidArgument, badWait.status.code));

        Window::Window closed;
        static_cast<void>(context.expectFalse("default Window is closed", closed.isOpen()));
        static_cast<void>(context.expectEq("default lifetime is Closed", Window::Types::LifetimeState::Closed, closed.lifetimeState()));
        static_cast<void>(context.expectEq("closed id is invalid", Window::Types::WindowId{}, closed.id()));
        static_cast<void>(context.expectEq("closed title is empty", std::string_view{}, closed.title()));
        static_cast<void>(context.expectEq("closed operation reports NotOpen", ErrorCode::NotOpen, closed.setTitle("unused").code));
        static_cast<void>(context.expectTrue("repeated close on closed Window succeeds", closed.close().ok()));
    }

    void testDescriptionValidation(TestSupport::Context &context)
    {
        const auto expectInvalid = [&context](std::string_view name, const Window::Types::Description &description)
        {
            Window::Window window;
            const IO::Types::Status status = window.open(description);
            static_cast<void>(context.expectEq(name, ErrorCode::InvalidArgument, status.code));
            static_cast<void>(context.expectFalse("invalid description leaves Window closed", window.isOpen()));
        };

        Window::Types::Description description;
        description.clientSize = {};
        expectInvalid("zero client size is invalid", description);

        description = {};
        description.title.assign("bad\0title", 9);
        expectInvalid("embedded title NUL is invalid", description);

        description = {};
        description.title.assign("\xC0\xAF", 2);
        expectInvalid("overlong UTF-8 is invalid", description);

        description = {};
        description.opacity = 1.1F;
        expectInvalid("opacity above one is invalid", description);

        description = {};
        description.sizeLimits.minimum = Window::Types::LogicalSize{900, 700};
        description.sizeLimits.maximum = Window::Types::LogicalSize{800, 600};
        expectInvalid("inverted size limits are invalid", description);

        description = {};
        description.aspectRatio = Window::Types::AspectRatio{16, 0};
        expectInvalid("zero aspect denominator is invalid", description);

        description = {};
        description.mode.displayMode = Window::Types::DisplayMode{{1920, 1080}, 60'000, 32, false};
        expectInvalid("display mode outside exclusive mode is invalid", description);

        description = {};
        description.pointerInputMode = Window::Types::PointerInputMode::AcceptRegions;
        expectInvalid("initial region mode without layout is invalid", description);

        description = {};
        description.cursorMode = static_cast<Window::Types::CursorMode>(99);
        expectInvalid("unknown enum is invalid", description);

        description = {};
        description.dpiResizePolicy = static_cast<Window::Types::DpiResizePolicy>(99);
        expectInvalid("unknown DPI resize policy is invalid", description);

        description = {};
        description.resizable = false;
        description.controls.maximizable = true;
        expectInvalid("maximize requires resize at creation", description);

        description = {};
        description.placement.monitor = {std::numeric_limits<std::uint64_t>::max()};
        expectInvalid("unknown placement monitor is invalid", description);

        description = {};
        description.mode.monitor = {std::numeric_limits<std::uint64_t>::max()};
        expectInvalid("unknown mode monitor is invalid", description);

        description = {};
        description.owner = {std::numeric_limits<std::uint64_t>::max()};
        expectInvalid("unknown owner is invalid", description);

        description = {};
        description.requestFocus = true;
        expectInvalid("hidden initial focus request is invalid", description);

        description = {};
        description.presentation = Window::Types::PresentationState::Minimized;
        expectInvalid("hidden non-normal presentation is invalid", description);

        description = {};
        description.visible = true;
        description.requestFocus = true;
        description.focusable = false;
        expectInvalid("focus request on non-focusable Window is invalid", description);

        description = {};
        description.clientSize.width = std::numeric_limits<std::uint32_t>::max();
        expectInvalid("client extent outside native signed range is invalid", description);

        Window::Window window;
        static_cast<void>(context.expectEq("zero internal queue capacity is invalid", ErrorCode::InvalidArgument, window.open({}, 0).code));
        std::span<Window::Types::Event> empty;
        static_cast<void>(context.expectEq("empty external queue is invalid", ErrorCode::InvalidArgument, window.open({}, empty).code));
    }

#if INTERNAL_WINDOW_TEST_HOOKS
    void testFixedEventQueue(TestSupport::Context &context)
    {
        std::array<Window::Types::Event, 3> storage;
        Window::Window window;
        static_cast<void>(context.expectTrue("portable queue hook opens", Window::TestHooks::openPortable(window, storage).ok()));
        static_cast<void>(
            context.expectEq("hook queue reports external storage", Window::Types::EventStorageKind::External, window.eventQueueInfo().storage));
        static_cast<void>(context.expectEq("native open rejects existing hook state", ErrorCode::AlreadyOpen, window.open({}).code));

        static_cast<void>(Window::TestHooks::enqueue(window, Window::Types::MovedEvent{{1, 2}}));
        static_cast<void>(Window::TestHooks::enqueue(window, Window::Types::MovedEvent{{3, 4}}));
        static_cast<void>(context.expectEq("compatible movement coalesces", std::size_t{1}, window.eventQueueInfo().pendingEvents));

        static_cast<void>(Window::TestHooks::enqueue(window, Window::Types::FocusChangedEvent{true}));
        static_cast<void>(Window::TestHooks::enqueue(window, Window::Types::MovedEvent{{5, 6}}));
        static_cast<void>(context.expectEq("noncoalescible event is a barrier", std::size_t{3}, window.eventQueueInfo().pendingEvents));

        static_cast<void>(Window::TestHooks::enqueue(window, Window::Types::RedrawRequestedEvent{}));
        static_cast<void>(context.expectEq("full queue evicts oldest coalescible event", std::uint64_t{1}, window.eventQueueInfo().droppedEvents));

        Window::Types::Event first;
        Window::Types::Event second;
        Window::Types::Event third;
        static_cast<void>(context.expectTrue("first retained event pops", window.popEvent(first)));
        static_cast<void>(context.expectTrue("focus barrier remains", first.getIf<Window::Types::FocusChangedEvent>() != nullptr));
        static_cast<void>(context.expectTrue("second retained event pops", window.popEvent(second)));
        const auto *moved = second.getIf<Window::Types::MovedEvent>();
        static_cast<void>(context.expectTrue("post-barrier movement remains", moved != nullptr));
        if (moved != nullptr)
            static_cast<void>(context.expectEq("coalesced movement keeps latest payload", Window::Types::ScreenPosition{5, 6}, moved->position));
        static_cast<void>(context.expectTrue("third retained event pops", window.popEvent(third)));
        static_cast<void>(context.expectTrue("new noncoalescible event remains", third.getIf<Window::Types::RedrawRequestedEvent>() != nullptr));
        static_cast<void>(
            context.expectTrue("retained sequences are increasing", first.sequence < second.sequence && second.sequence < third.sequence));

        window.clearDroppedEventCount();
        static_cast<void>(context.expectEq("drop count clears", std::uint64_t{0}, window.eventQueueInfo().droppedEvents));
        static_cast<void>(context.expectTrue("hook queue closes", window.close().ok()));

        std::array<Window::Types::Event, 1> payloadStorage;
        Window::Window payloadWindow;
        static_cast<void>(Window::TestHooks::openPortable(payloadWindow, payloadStorage));
        Window::Types::FilesDroppedEvent dropped;
        dropped.paths.emplace_back("retained-until-close.txt");
        static_cast<void>(Window::TestHooks::enqueue(payloadWindow, std::move(dropped)));
        static_cast<void>(payloadWindow.close());
        static_cast<void>(context.expectTrue(
            "close releases payloads from borrowed event slots",
            payloadStorage[0].getIf<Window::Types::FilesDroppedEvent>() == nullptr));
    }

    void testStickyClose(TestSupport::Context &context)
    {
        std::array<Window::Types::Event, 4> storage;
        Window::Window source;
        static_cast<void>(Window::TestHooks::openPortable(source, storage));
        static_cast<void>(Window::TestHooks::requestClose(source, Window::Types::CloseRequestSource::User));
        static_cast<void>(Window::TestHooks::requestClose(source, Window::Types::CloseRequestSource::System));
        static_cast<void>(context.expectTrue("close request is sticky", source.closeRequested()));
        static_cast<void>(context.expectEq("repeated close request emits once", std::size_t{1}, source.eventQueueInfo().pendingEvents));

        Window::Types::Event event;
        static_cast<void>(context.expectTrue("queue remains readable", source.popEvent(event)));
        const auto *close = event.getIf<Window::Types::CloseRequestedEvent>();
        static_cast<void>(context.expectTrue("typed close payload remains", close != nullptr));
        if (close != nullptr)
            static_cast<void>(context.expectEq("first close source wins", Window::Types::CloseRequestSource::User, close->source));
        static_cast<void>(source.close());
    }

    void testFailureInjection(TestSupport::Context &context)
    {
        using FailurePoint = Window::TestHooks::FailurePoint;
        Window::TestHooks::resetFailures();

        Window::Types::Description description;
        description.title = "Window failure validation";
        description.clientSize = {280, 180};
        description.visible = false;

        const auto expectFailedOpen = [&context, &description](std::string_view name, FailurePoint point, ErrorCode expected)
        {
            Window::Window candidate;
            Window::TestHooks::failNext(point);
            const IO::Types::Status status = candidate.open(description, 8);
            static_cast<void>(context.expectEq(name, expected, status.code));
            static_cast<void>(context.expectFalse("failed open rolls back native ownership", candidate.isOpen()));
            Window::TestHooks::resetFailures();
        };

        expectFailedOpen("allocation failure is translated", FailurePoint::Allocation, ErrorCode::OutOfMemory);
        expectFailedOpen("dispatcher failure is translated", FailurePoint::Dispatcher, ErrorCode::OpenFailed);
        expectFailedOpen("native creation failure is translated", FailurePoint::NativeCreation, ErrorCode::OpenFailed);
        expectFailedOpen("partial native open rolls back", FailurePoint::PartialOpen, ErrorCode::NativeFailure);

        Window::Window window;
        static_cast<void>(context.expectTrue("open succeeds after injected rollback", window.open(description, 16).ok()));
        if (!window.isOpen())
            return;

        const std::string originalTitle(window.title());
        Window::TestHooks::failNext(FailurePoint::TitleConversion);
        static_cast<void>(context.expectEq("title conversion failure is translated", ErrorCode::EncodingFailed, window.setTitle("changed").code));
        static_cast<void>(context.expectEq("failed title update preserves cache", std::string_view{originalTitle}, window.title()));

        const std::array pointerRegions{Window::Types::LogicalRect{{0, 0}, {16, 16}}};
        Window::TestHooks::failNext(FailurePoint::RegionCopy);
        const Window::Types::CustomChromeLayout chromeLayout{.draggableRegions = pointerRegions};
        static_cast<void>(
            context.expectEq("region copy failure is translated", ErrorCode::OutOfMemory, window.setCustomChromeLayout(chromeLayout).code));

        const std::array<std::byte, 4> pixel{std::byte{0x40}, std::byte{0x80}, std::byte{0xC0}, std::byte{0xFF}};
        const std::array iconImages{Window::Types::IconImageView{{1, 1}, pixel}};
        Window::TestHooks::failNext(FailurePoint::IconConversion);
        static_cast<void>(context.expectEq("icon conversion failure is translated", ErrorCode::NativeFailure, window.setIcon(iconImages).code));
        static_cast<void>(context.expectTrue("icon operation recovers after failure", window.setIcon(iconImages).ok()));
        static_cast<void>(window.clearIcon());

        Window::TestHooks::failNext(FailurePoint::Cursor);
        static_cast<void>(context.expectEq(
            "cursor failure is translated",
            ErrorCode::NativeFailure,
            window.setCursorMode(Window::Types::CursorMode::Confined).code));
        static_cast<void>(context.expectEq("failed cursor update rolls back cache", Window::Types::CursorMode::Normal, window.cursorMode()));

        Window::TestHooks::failNext(FailurePoint::MonitorQuery);
        static_cast<void>(context.expectEq("monitor query failure is translated", ErrorCode::StatFailed, Window::getPrimaryMonitor().status.code));
        Window::TestHooks::failNext(FailurePoint::DisplayEnumeration);
        static_cast<void>(context.expectEq("display enumeration failure is translated", ErrorCode::StatFailed, Window::getMonitors().status.code));

        const Window::Types::MonitorId monitor = window.currentMonitor();
        Window::TestHooks::failNext(FailurePoint::FullscreenPartial);
        static_cast<void>(context.expectEq(
            "partial fullscreen failure is translated",
            ErrorCode::NativeFailure,
            window.setMode({.mode = Window::Types::WindowMode::BorderlessFullscreen, .monitor = monitor}).code));
        static_cast<void>(context.expectEq("partial fullscreen failure restores mode", Window::Types::WindowMode::Windowed, window.mode()));

        static_cast<void>(context.expectTrue(
            "borderless mode opens restoration boundary",
            window.setMode({.mode = Window::Types::WindowMode::BorderlessFullscreen, .monitor = monitor}).ok()));
        Window::TestHooks::failNext(FailurePoint::DisplayRestoration);
        static_cast<void>(context.expectEq("display restoration failure is translated", ErrorCode::NativeFailure, window.setMode({}).code));
        static_cast<void>(
            context.expectEq("failed display restoration preserves previous mode", Window::Types::WindowMode::BorderlessFullscreen, window.mode()));
        static_cast<void>(context.expectTrue("display restoration retry succeeds", window.setMode({}).ok()));

        Window::TestHooks::failNext(FailurePoint::EventPump);
        static_cast<void>(context.expectEq("event pump failure is translated", ErrorCode::NativeFailure, Window::pollEvents().status.code));
        static_cast<void>(context.expectTrue("event pump recovers after failure", Window::pollEvents().status.ok()));

        Window::TestHooks::failNext(FailurePoint::Close);
        static_cast<void>(context.expectEq("close failure is translated", ErrorCode::CloseFailed, window.close().code));
        static_cast<void>(context.expectTrue("failed close preserves ownership", window.isOpen()));
        static_cast<void>(context.expectTrue("close retry succeeds", window.close().ok()));
        Window::TestHooks::resetFailures();
    }

    void testThreadingContracts(TestSupport::Context &context)
    {
        Window::Types::Description description;
        description.title = "Window threading validation";
        description.clientSize = {240, 160};
        description.visible = false;

        Window::Window window;
        static_cast<void>(context.expectTrue("threading fixture opens", window.open(description, 8).ok()));
        if (!window.isOpen())
            return;

        ErrorCode mutationCode = ErrorCode::Success;
        ErrorCode closeCode = ErrorCode::Success;
        ErrorCode wakeCode = ErrorCode::Unknown;
        ErrorCode nativeHandleCode = ErrorCode::Success;
        std::thread worker(
            [&window, &mutationCode, &closeCode, &wakeCode, &nativeHandleCode]
            {
                mutationCode = window.setTitle("wrong-thread mutation").code;
                closeCode = window.close().code;
                wakeCode = window.wakeEventWait().code;
                nativeHandleCode = Window::Native::Win32::getHandle(window).status.code;
            });
        worker.join();

        static_cast<void>(context.expectEq("wrong-thread mutation is rejected", ErrorCode::ResourceBusy, mutationCode));
        static_cast<void>(context.expectEq("wrong-thread close is rejected", ErrorCode::ResourceBusy, closeCode));
        static_cast<void>(context.expectEq("wake is intentionally cross-thread safe", ErrorCode::Success, wakeCode));
        static_cast<void>(context.expectEq("wrong-thread native handle is rejected", ErrorCode::ResourceBusy, nativeHandleCode));
        static_cast<void>(context.expectEq("wrong-thread mutation preserves title", std::string_view{description.title}, window.title()));
        static_cast<void>(context.expectTrue("wrong-thread close preserves ownership", window.isOpen()));

        const Window::Types::EventPumpResult wakeResult = Window::waitEvents(std::chrono::milliseconds{100});
        static_cast<void>(context.expectTrue("owner pump receives cross-thread wake", wakeResult.status.ok() && !wakeResult.timedOut));
        static_cast<void>(
            context.expectEq("reentrant event pump is rejected", ErrorCode::ResourceBusy, Window::TestHooks::pumpReentrantly().status.code));
        static_cast<void>(context.expectTrue("threading fixture closes", window.close().ok()));
    }

    void testExceptionalLifetime(TestSupport::Context &context)
    {
        Window::Types::Description description;
        description.title = "Window exceptional lifetime validation";
        description.clientSize = {240, 160};
        description.visible = false;

        Window::Window unexpected;
        static_cast<void>(context.expectTrue("unexpected-destruction fixture opens", unexpected.open(description, 1).ok()));
        if (unexpected.isOpen())
        {
            static_cast<void>(Window::TestHooks::enqueue(unexpected, Window::Types::RedrawRequestedEvent{}));
            static_cast<void>(
                context.expectTrue("test hook destroys native HWND unexpectedly", Window::TestHooks::destroyNativeWindow(unexpected).ok()));
            static_cast<void>(context.expectFalse("unexpectedly destroyed HWND is not open", unexpected.isOpen()));
            static_cast<void>(context.expectEq(
                "unexpected destruction enters pending finalize",
                Window::Types::LifetimeState::NativeDestroyedPendingFinalize,
                unexpected.lifetimeState()));
            static_cast<void>(
                context.expectEq("mutation while pending finalize reports NotOpen", ErrorCode::NotOpen, unexpected.setTitle("unused").code));
            static_cast<void>(context.expectEq("reopen before finalization is rejected", ErrorCode::AlreadyOpen, unexpected.open(description).code));
            static_cast<void>(context.expectEq(
                "native handle is unavailable while pending finalize",
                ErrorCode::NotOpen,
                Window::Native::Win32::getHandle(unexpected).status.code));
            Window::Types::Event event;
            static_cast<void>(context.expectTrue("pending finalize retains a terminal event", unexpected.popEvent(event)));
            static_cast<void>(context.expectTrue("terminal event is typed ClosedEvent", event.getIf<Window::Types::ClosedEvent>() != nullptr));
            static_cast<void>(context.expectTrue("controlled finalization succeeds", unexpected.close().ok()));
            static_cast<void>(
                context.expectEq("controlled finalization reaches Closed", Window::Types::LifetimeState::Closed, unexpected.lifetimeState()));
            static_cast<void>(context.expectTrue("Window reopens after finalization", unexpected.open(description, 4).ok()));
            static_cast<void>(context.expectTrue("reopened Window closes normally", unexpected.close().ok()));
        }

        auto deferred = std::make_unique<Window::Window>();
        static_cast<void>(context.expectTrue("deferred-destruction fixture opens", deferred->open(description, 4).ok()));
        HWND deferredHandle = nullptr;
        if (deferred->isOpen())
            deferredHandle = Window::Native::Win32::getHandle(*deferred).handle.window;
        std::thread destroyer(
            [owned = std::move(deferred)]() mutable
            {
                owned.reset();
            });
        destroyer.join();
        static_cast<void>(context.expectTrue("wrong-thread destructor leaves cleanup queued", IsWindow(deferredHandle) != FALSE));
        static_cast<void>(context.expectTrue("owner pump drains deferred cleanup", Window::pollEvents().status.ok()));
        static_cast<void>(context.expectFalse("deferred owner cleanup destroys HWND", IsWindow(deferredHandle) != FALSE));

        std::unique_ptr<Window::Window> survivingObject;
        HWND ownerExitHandle = nullptr;
        std::thread ownerThread(
            [&]
            {
                auto owned = std::make_unique<Window::Window>();
                if (owned->open(description, 4).ok())
                    ownerExitHandle = Window::Native::Win32::getHandle(*owned).handle.window;
                survivingObject = std::move(owned);
            });
        ownerThread.join();
        static_cast<void>(context.expectTrue("owner-exit fixture created a native HWND", ownerExitHandle != nullptr));
        static_cast<void>(context.expectFalse("thread-local dispatcher destroys HWND on owner exit", IsWindow(ownerExitHandle) != FALSE));
        if (survivingObject)
        {
            static_cast<void>(context.expectEq(
                "portable object observes Closed after owner exit",
                Window::Types::LifetimeState::Closed,
                survivingObject->lifetimeState()));
            survivingObject.reset();
        }
    }

    void testPointerHitMask(TestSupport::Context &context)
    {
        namespace Feedback = Window::Renderer;
        using MaskWord = Feedback::PointerHitMaskWord;
        constexpr std::size_t bitsPerWord = std::numeric_limits<MaskWord>::digits;

        Window::Types::Description description;
        description.title = "Window pointer-mask validation";
        description.clientSize = {241, 161};
        description.visible = false;

        Window::Window window;
        static_cast<void>(context.expectTrue("pointer-mask fixture opens", window.open(description, 8).ok()));
        if (!window.isOpen())
            return;

        static_cast<void>(context.expectEq(
            "unadvertised native HitMask bridge is unsupported",
            ErrorCode::Unsupported,
            Feedback::beginPointerHitMaskUpdate(window).status.code));
        Window::TestHooks::enablePointerHitMaskBridge(window);

        Window::Types::PixelSize size = window.framebufferSize();
        if (size.width % bitsPerWord == 0)
        {
            static_cast<void>(window.setClientSize({description.clientSize.width + 1, description.clientSize.height}));
            size = window.framebufferSize();
        }
        const std::size_t wordsPerRow =
            static_cast<std::size_t>(size.width) / bitsPerWord + (size.width % bitsPerWord != 0 ? 1U : 0U);
        const std::size_t wordCount = Feedback::requiredPointerHitMaskWords(size);
        std::vector<MaskWord> words(wordCount);
        words.front() = MaskWord{1};

        const std::size_t lastX = static_cast<std::size_t>(size.width) - 1U;
        const std::size_t lastWord =
            (static_cast<std::size_t>(size.height) - 1U) * wordsPerRow + lastX / bitsPerWord;
        const unsigned int lastBit = static_cast<unsigned int>(lastX % bitsPerWord);
        words[lastWord] |= MaskWord{1} << lastBit;

        const Window::Types::LogicalPosition rowSample{0, static_cast<std::int32_t>(window.clientSize().height / 2U)};
        const std::uint64_t sampledY =
            static_cast<std::uint64_t>(rowSample.y) * size.height / window.clientSize().height;
        words[static_cast<std::size_t>(sampledY) * wordsPerRow] |= MaskWord{1};

        const Feedback::PointerHitMaskTargetResult first = Feedback::beginPointerHitMaskUpdate(window);
        static_cast<void>(context.expectTrue("Window creates a nonzero mask generation", first.status.ok() && first.target.generation != 0));
        static_cast<void>(context.expectEq("target snapshots framebuffer size", size, first.target.framebufferSize));
        static_cast<void>(context.expectEq("target reports exact word count", wordCount, first.target.requiredWordCount));
        Window::TestHooks::failNext(Window::TestHooks::FailurePoint::Allocation);
        static_cast<void>(context.expectEq(
            "first mask allocation failure is reported",
            ErrorCode::OutOfMemory,
            Feedback::publishPointerHitMask(window, first.target.generation, words).code));
        static_cast<void>(
            context.expectEq("failed first publication keeps no mask", std::size_t{0}, Window::TestHooks::pointerHitMaskWordCount(window)));

        static_cast<void>(
            context.expectTrue("first mask publication succeeds", Feedback::publishPointerHitMask(window, first.target.generation, words).ok()));
        static_cast<void>(context.expectTrue("active mask is reported", Feedback::hasPointerHitMask(window)));
        static_cast<void>(context.expectEq("mask stores exact word count", wordCount, Window::TestHooks::pointerHitMaskWordCount(window)));
        static_cast<void>(context.expectEq(
            "mask stores first physical pixel",
            MaskWord{1},
            Window::TestHooks::pointerHitMaskWord(window, 0) & MaskWord{1}));
        static_cast<void>(context.expectTrue(
            "mask stores last physical pixel",
            (Window::TestHooks::pointerHitMaskWord(window, lastWord) & (MaskWord{1} << lastBit)) != 0));
        static_cast<void>(context.expectTrue(
            "first logical position samples the first interactive pixel",
            Window::TestHooks::pointerHitMaskAccepts(window, {0, 0})));
        static_cast<void>(context.expectTrue(
            "row-local mask lookup samples an independently padded row",
            Window::TestHooks::pointerHitMaskAccepts(window, rowSample)));
        static_cast<void>(
            context.expectTrue("out-of-range sampling uses interactive fallback", Window::TestHooks::pointerHitMaskAccepts(window, {-1, -1})));

        const void *storage = Window::TestHooks::pointerHitMaskStorage(window);
        words.front() = MaskWord{2};
        const Feedback::PointerHitMaskTargetResult stale = Feedback::beginPointerHitMaskUpdate(window);
        const Feedback::PointerHitMaskTargetResult newer = Feedback::beginPointerHitMaskUpdate(window);
        static_cast<void>(context.expectTrue(
            "Window generations increase monotonically",
            stale.status.ok() && newer.status.ok() && newer.target.generation > stale.target.generation));
        static_cast<void>(context.expectEq(
            "superseded outstanding update is interrupted",
            ErrorCode::Interrupted,
            Feedback::publishPointerHitMask(window, stale.target.generation, words).code));
        static_cast<void>(
            context.expectTrue("newer same-size mask succeeds", Feedback::publishPointerHitMask(window, newer.target.generation, words).ok()));
        static_cast<void>(context.expectEq("same-size publication reuses storage", storage, Window::TestHooks::pointerHitMaskStorage(window)));
        static_cast<void>(
            context.expectEq("newer publication updates revision", newer.target.generation, Window::TestHooks::pointerHitMaskGeneration(window)));
        static_cast<void>(context.expectEq(
            "stale publication is rejected",
            ErrorCode::Interrupted,
            Feedback::publishPointerHitMask(window, newer.target.generation, words).code));
        static_cast<void>(context.expectEq(
            "stale publication preserves active data",
            MaskWord{2},
            Window::TestHooks::pointerHitMaskWord(window, 0)));

        const unsigned int validBitsInLastWord = static_cast<unsigned int>(size.width % bitsPerWord);
        if (validBitsInLastWord != 0)
        {
            std::vector<MaskWord> invalidPadding = words;
            invalidPadding[wordsPerRow - 1U] |= MaskWord{1} << validBitsInLastWord;
            const Feedback::PointerHitMaskTargetResult padding = Feedback::beginPointerHitMaskUpdate(window);
            static_cast<void>(context.expectEq(
                "set row-padding bits are rejected",
                ErrorCode::InvalidArgument,
                Feedback::publishPointerHitMask(window, padding.target.generation, invalidPadding).code));
            static_cast<void>(context.expectEq(
                "invalid row-padding bits preserve revision",
                newer.target.generation,
                Window::TestHooks::pointerHitMaskGeneration(window)));
        }

        const Window::Types::ScreenPosition originalPosition = window.clientPosition();
        static_cast<void>(window.setClientPosition({originalPosition.x + 1, originalPosition.y + 1}));
        static_cast<void>(context.expectEq(
            "movement preserves active mask revision",
            newer.target.generation,
            Window::TestHooks::pointerHitMaskGeneration(window)));
        const Feedback::PointerHitMaskTargetResult beforeResize = Feedback::beginPointerHitMaskUpdate(window);
        static_cast<void>(window.setClientSize({window.clientSize().width + 1, window.clientSize().height}));
        static_cast<void>(
            context.expectEq("framebuffer resize invalidates active mask", std::uint64_t{0}, Window::TestHooks::pointerHitMaskGeneration(window)));
        static_cast<void>(context.expectFalse("resize removes active-mask state", Feedback::hasPointerHitMask(window)));
        static_cast<void>(context.expectEq(
            "pre-resize update is interrupted",
            ErrorCode::Interrupted,
            Feedback::publishPointerHitMask(window, beforeResize.target.generation, words).code));
        static_cast<void>(context.expectTrue("mask clear is idempotent", Feedback::clearPointerHitMask(window).ok()));

        const Feedback::PointerHitMaskTargetResult beforeClose = Feedback::beginPointerHitMaskUpdate(window);
        static_cast<void>(context.expectTrue("pointer-mask fixture closes", window.close().ok()));
        static_cast<void>(context.expectTrue("pointer-mask fixture reopens", window.open(description, 8).ok()));
        if (window.isOpen())
        {
            Window::TestHooks::enablePointerHitMaskBridge(window);
            static_cast<void>(context.expectEq(
                "pre-close generation stays stale after reopen",
                ErrorCode::Interrupted,
                Feedback::publishPointerHitMask(window, beforeClose.target.generation, words).code));
            Window::TestHooks::setPointerHitMaskGeneration(window, std::numeric_limits<std::uint64_t>::max() - 1U);
            const Feedback::PointerHitMaskTargetResult lastGeneration = Feedback::beginPointerHitMaskUpdate(window);
            static_cast<void>(context.expectEq(
                "last representable generation is deterministic",
                std::numeric_limits<std::uint64_t>::max(),
                lastGeneration.target.generation));
            static_cast<void>(context.expectEq(
                "generation overflow never wraps",
                ErrorCode::ResourceBusy,
                Feedback::beginPointerHitMaskUpdate(window).status.code));
            static_cast<void>(window.close());
        }
    }
#endif

    void testNativeEventTranslation(TestSupport::Context &context)
    {
        Window::Types::Description description;
        description.title = "Window native event validation";
        description.clientSize = {260, 170};
        description.visible = false;
        description.fileDropEnabled = true;

        Window::Window window;
        static_cast<void>(context.expectTrue("native event fixture opens", window.open(description, 64).ok()));
        if (!window.isOpen())
            return;

        const Window::Native::Win32::HandleResult handle = Window::Native::Win32::getHandle(window);
        static_cast<void>(context.expectTrue("native event fixture exposes HWND", handle.status.ok() && handle.handle.window != nullptr));
        if (!handle.status.ok() || handle.handle.window == nullptr)
        {
            static_cast<void>(window.close());
            return;
        }

        window.clearEvents();
        static_cast<void>(window.setClientSize({300, 210}));
        static_cast<void>(context.expectTrue(
            "native resize translates to ClientSizeChangedEvent",
            consumeEventOfType<Window::Types::ClientSizeChangedEvent>(window)));

        window.clearEvents();
        static_cast<void>(window.setMode({.mode = Window::Types::WindowMode::BorderlessFullscreen, .monitor = window.currentMonitor()}));
        static_cast<void>(
            context.expectTrue("fullscreen transition translates to ModeChangedEvent", consumeEventOfType<Window::Types::ModeChangedEvent>(window)));
#if INTERNAL_WINDOW_TEST_HOOKS
        window.clearEvents();
        static_cast<void>(
            context.expectTrue("synthetic monitor removal recovery succeeds", Window::TestHooks::simulateFullscreenMonitorRemoval(window).ok()));
        static_cast<void>(context.expectEq("monitor removal recovers Windowed mode", Window::Types::WindowMode::Windowed, window.mode()));
        static_cast<void>(context.expectFalse("monitor removal clears fullscreen monitor", window.fullscreenInfo().monitor.valid()));
        Window::Types::Event recoveryEvent;
        static_cast<void>(context.expectTrue("recovery queues display event first", window.popEvent(recoveryEvent)));
        static_cast<void>(context.expectTrue(
            "first recovery event is display configuration",
            recoveryEvent.getIf<Window::Types::DisplayConfigurationChangedEvent>() != nullptr));
        static_cast<void>(context.expectTrue("recovery queues mode event second", window.popEvent(recoveryEvent)));
        static_cast<void>(
            context.expectTrue("second recovery event is mode change", recoveryEvent.getIf<Window::Types::ModeChangedEvent>() != nullptr));
        window.clearEvents();
        static_cast<void>(window.setMode({.mode = Window::Types::WindowMode::BorderlessFullscreen, .monitor = window.currentMonitor()}));
#endif
        window.clearEvents();
        static_cast<void>(window.setMode({}));
        static_cast<void>(
            context.expectTrue("windowed restoration translates to ModeChangedEvent", consumeEventOfType<Window::Types::ModeChangedEvent>(window)));

        static_cast<void>(SendMessageW(handle.handle.window, WM_SHOWWINDOW, FALSE, 0));
        window.clearEvents();
        static_cast<void>(SendMessageW(handle.handle.window, WM_SHOWWINDOW, TRUE, 0));
        static_cast<void>(context.expectTrue("WM_SHOWWINDOW updates visibility cache", window.isVisible()));
        static_cast<void>(context.expectTrue(
            "WM_SHOWWINDOW translates to VisibilityChangedEvent",
            consumeEventOfType<Window::Types::VisibilityChangedEvent>(window)));
        window.clearEvents();
        static_cast<void>(SendMessageW(handle.handle.window, WM_SHOWWINDOW, FALSE, 0));
        static_cast<void>(context.expectFalse("synthetic hide restores visibility cache", window.isVisible()));
        static_cast<void>(consumeEventOfType<Window::Types::VisibilityChangedEvent>(window));

        window.clearEvents();
        static_cast<void>(SendMessageW(handle.handle.window, WM_SIZE, SIZE_MINIMIZED, MAKELPARAM(300, 210)));
        static_cast<void>(context.expectTrue("WM_SIZE minimize updates presentation cache", window.isMinimized()));
        static_cast<void>(context.expectTrue(
            "WM_SIZE translates to PresentationStateChangedEvent",
            consumeEventOfType<Window::Types::PresentationStateChangedEvent>(window)));
        window.clearEvents();
        static_cast<void>(SendMessageW(handle.handle.window, WM_SIZE, SIZE_RESTORED, MAKELPARAM(300, 210)));
        static_cast<void>(context.expectFalse("synthetic restore resets minimized cache", window.isMinimized()));
        static_cast<void>(consumeEventOfType<Window::Types::PresentationStateChangedEvent>(window));

        static_cast<void>(SendMessageW(handle.handle.window, WM_KILLFOCUS, 0, 0));
        window.clearEvents();
        static_cast<void>(SendMessageW(handle.handle.window, WM_SETFOCUS, 0, 0));
        static_cast<void>(context.expectTrue("WM_SETFOCUS updates cache", window.isFocused()));
        static_cast<void>(
            context.expectTrue("WM_SETFOCUS translates to FocusChangedEvent", consumeEventOfType<Window::Types::FocusChangedEvent>(window)));
        window.clearEvents();
        static_cast<void>(SendMessageW(handle.handle.window, WM_KILLFOCUS, 0, 0));
        static_cast<void>(context.expectFalse("WM_KILLFOCUS updates cache", window.isFocused()));
        static_cast<void>(
            context.expectTrue("WM_KILLFOCUS translates to FocusChangedEvent", consumeEventOfType<Window::Types::FocusChangedEvent>(window)));

        window.clearEvents();
        static_cast<void>(SendMessageW(handle.handle.window, WM_MOUSEMOVE, 0, MAKELPARAM(2, 2)));
        static_cast<void>(context.expectTrue("WM_MOUSEMOVE updates cursor-presence cache", window.isCursorInside()));
        static_cast<void>(context.expectTrue(
            "WM_MOUSEMOVE translates to CursorPresenceChangedEvent",
            consumeEventOfType<Window::Types::CursorPresenceChangedEvent>(window)));
        window.clearEvents();
        static_cast<void>(SendMessageW(handle.handle.window, WM_MOUSELEAVE, 0, 0));
        static_cast<void>(context.expectFalse("WM_MOUSELEAVE updates cursor-presence cache", window.isCursorInside()));
        static_cast<void>(context.expectTrue(
            "WM_MOUSELEAVE translates to CursorPresenceChangedEvent",
            consumeEventOfType<Window::Types::CursorPresenceChangedEvent>(window)));

        window.clearEvents();
        static_cast<void>(SendMessageW(handle.handle.window, WM_DISPLAYCHANGE, 32, MAKELPARAM(1920, 1080)));
        static_cast<void>(context.expectTrue(
            "WM_DISPLAYCHANGE translates to DisplayConfigurationChangedEvent",
            consumeEventOfType<Window::Types::DisplayConfigurationChangedEvent>(window)));

        window.clearEvents();
        const std::wstring droppedPath = L"C:/GameWIP/window-drop-test.txt";
        const std::size_t dropBytes = sizeof(DROPFILES) + (droppedPath.size() + 2) * sizeof(wchar_t);
        HGLOBAL dropMemory = GlobalAlloc(GHND, dropBytes);
        static_cast<void>(context.expectTrue("synthetic drop payload allocates", dropMemory != nullptr));
        if (dropMemory != nullptr)
        {
            auto *drop = static_cast<DROPFILES *>(GlobalLock(dropMemory));
            static_cast<void>(context.expectTrue("synthetic drop payload locks", drop != nullptr));
            if (drop != nullptr)
            {
                drop->pFiles = sizeof(DROPFILES);
                drop->pt = {4, 6};
                drop->fNC = FALSE;
                drop->fWide = TRUE;
                auto *path = reinterpret_cast<wchar_t *>(reinterpret_cast<std::byte *>(drop) + sizeof(DROPFILES));
                std::copy(droppedPath.begin(), droppedPath.end(), path);
                path[droppedPath.size()] = wchar_t{};
                path[droppedPath.size() + 1] = wchar_t{};
                static_cast<void>(GlobalUnlock(dropMemory));
                static_cast<void>(SendMessageW(handle.handle.window, WM_DROPFILES, reinterpret_cast<WPARAM>(dropMemory), 0));
                dropMemory = nullptr;
                static_cast<void>(
                    context.expectTrue("WM_DROPFILES translates to FilesDroppedEvent", consumeEventOfType<Window::Types::FilesDroppedEvent>(window)));
            }
            if (dropMemory != nullptr)
                static_cast<void>(GlobalFree(dropMemory));
        }

        window.clearEvents();
        static_cast<void>(SendMessageW(handle.handle.window, WM_PAINT, 0, 0));
        static_cast<void>(
            context.expectTrue("WM_PAINT translates to RedrawRequestedEvent", consumeEventOfType<Window::Types::RedrawRequestedEvent>(window)));

        window.clearEvents();
        static_cast<void>(SendMessageW(handle.handle.window, WM_CLOSE, 0, 0));
        static_cast<void>(context.expectTrue("WM_CLOSE sets sticky close intent", window.closeRequested()));
        static_cast<void>(
            context.expectTrue("WM_CLOSE translates to CloseRequestedEvent", consumeEventOfType<Window::Types::CloseRequestedEvent>(window)));
        static_cast<void>(window.clearCloseRequest());
        static_cast<void>(context.expectTrue("native event fixture closes", window.close().ok()));
    }

    void testRendererOcclusionFeedback(TestSupport::Context &context)
    {
        namespace Feedback = Window::Renderer;
        using Capability = Window::Types::Capability;

        Window::Window closed;
        static_cast<void>(
            context.expectEq("closed Window rejects provider attachment", ErrorCode::NotOpen, Feedback::attachOcclusionProvider(closed).code));
        static_cast<void>(
            context.expectEq("closed Window rejects occlusion report", ErrorCode::NotOpen, Feedback::reportOcclusion(closed, true).code));
        static_cast<void>(
            context.expectEq("closed Window rejects provider detach", ErrorCode::NotOpen, Feedback::detachOcclusionProvider(closed).code));

        Window::Types::Description description;
        description.title = "Window renderer feedback validation";
        description.clientSize = {240, 160};
        description.visible = false;

        Window::Window window;
        static_cast<void>(context.expectTrue("renderer feedback fixture opens", window.open(description, 8).ok()));
        if (!window.isOpen())
            return;

        static_cast<void>(context.expectTrue("global occlusion reporting is supported", Window::supports(Capability::OcclusionReporting)));
        static_cast<void>(context.expectTrue("Window reports backend occlusion capability", window.supports(Capability::OcclusionReporting)));
        static_cast<void>(context.expectFalse("Window initially lacks occlusion provider", Feedback::hasOcclusionProvider(window)));
        static_cast<void>(context.expectEq("report before provider is rejected", ErrorCode::NotOpen, Feedback::reportOcclusion(window, true).code));
        static_cast<void>(context.expectTrue("occlusion provider attaches", Feedback::attachOcclusionProvider(window).ok()));
        static_cast<void>(context.expectTrue("attached Window reports its provider", Feedback::hasOcclusionProvider(window)));
        static_cast<void>(context.expectEq("second provider is rejected", ErrorCode::AlreadyOpen, Feedback::attachOcclusionProvider(window).code));

        window.clearEvents();
        static_cast<void>(context.expectTrue("occluded report succeeds", Feedback::reportOcclusion(window, true).ok()));
        static_cast<void>(context.expectTrue("occluded report updates cache", window.isOccluded()));
        Window::Types::Event event;
        static_cast<void>(context.expectTrue("occluded transition queues event", window.popEvent(event)));
        const auto *occludedEvent = event.getIf<Window::Types::OcclusionChangedEvent>();
        static_cast<void>(context.expectTrue("occluded event has typed payload", occludedEvent != nullptr));
        if (occludedEvent != nullptr)
            static_cast<void>(context.expectTrue("occluded event carries true", occludedEvent->occluded));

        static_cast<void>(context.expectTrue("duplicate occlusion report succeeds", Feedback::reportOcclusion(window, true).ok()));
        static_cast<void>(context.expectEq("duplicate report does not queue event", std::size_t{0}, window.eventQueueInfo().pendingEvents));

        ErrorCode wrongThreadCode = ErrorCode::Success;
        std::thread worker(
            [&window, &wrongThreadCode]
            {
                wrongThreadCode = Window::Renderer::reportOcclusion(window, false).code;
            });
        worker.join();
        static_cast<void>(context.expectEq("wrong-thread renderer feedback is rejected", ErrorCode::ResourceBusy, wrongThreadCode));
        static_cast<void>(context.expectTrue("wrong-thread report preserves cache", window.isOccluded()));

        window.clearEvents();
        static_cast<void>(context.expectTrue("provider detaches", Feedback::detachOcclusionProvider(window).ok()));
        static_cast<void>(context.expectTrue("detach preserves backend capability", window.supports(Capability::OcclusionReporting)));
        static_cast<void>(context.expectFalse("detach clears provider state", Feedback::hasOcclusionProvider(window)));
        static_cast<void>(context.expectFalse("detach resets occlusion cache", window.isOccluded()));
        static_cast<void>(context.expectTrue("detach queues final false transition", window.popEvent(event)));
        const auto *visibleEvent = event.getIf<Window::Types::OcclusionChangedEvent>();
        static_cast<void>(context.expectTrue("detach event has typed payload", visibleEvent != nullptr));
        if (visibleEvent != nullptr)
            static_cast<void>(context.expectFalse("detach event carries false", visibleEvent->occluded));
        static_cast<void>(context.expectEq("report after detach is rejected", ErrorCode::NotOpen, Feedback::reportOcclusion(window, false).code));
        static_cast<void>(context.expectTrue("repeated detach succeeds", Feedback::detachOcclusionProvider(window).ok()));

        static_cast<void>(context.expectTrue("provider reattaches", Feedback::attachOcclusionProvider(window).ok()));
        static_cast<void>(context.expectTrue("feedback fixture closes with provider attached", window.close().ok()));

        Window::Window overflow;
        static_cast<void>(context.expectTrue("occlusion overflow fixture opens", overflow.open(description, 1).ok()));
        if (overflow.isOpen())
        {
            static_cast<void>(Feedback::attachOcclusionProvider(overflow));
            static_cast<void>(Feedback::reportOcclusion(overflow, true));
            static_cast<void>(Feedback::reportOcclusion(overflow, false));
            static_cast<void>(context.expectFalse("dropped transition still updates cache", overflow.isOccluded()));
            static_cast<void>(
                context.expectEq("full queue counts dropped occlusion event", std::uint64_t{1}, overflow.eventQueueInfo().droppedEvents));
            static_cast<void>(overflow.close());
        }
    }

    void testDisplayColorInformation(TestSupport::Context &context)
    {
        namespace Feedback = Window::Renderer;

        Window::Window closed;
        static_cast<void>(context.expectEq(
            "closed Window rejects display-color query",
            ErrorCode::NotOpen,
            Feedback::getWindowDisplayColorInfo(closed).status.code));
        static_cast<void>(context.expectEq(
            "invalid monitor rejects display-color query",
            ErrorCode::InvalidArgument,
            Feedback::getDisplayColorInfo({}).status.code));
        static_cast<void>(context.expectEq(
            "disconnected monitor rejects display-color query",
            ErrorCode::NotFound,
            Feedback::getDisplayColorInfo({std::numeric_limits<std::uint64_t>::max()}).status.code));

        const Window::Types::MonitorInfoResult primary = Window::getPrimaryMonitor();
        static_cast<void>(context.expectTrue("display-color primary monitor resolves", primary.status.ok()));
        if (!primary.status.ok())
            return;

        const Window::Types::DisplayColorInfoResult primaryColor = Feedback::getDisplayColorInfo(primary.monitor.id);
        static_cast<void>(context.expectTrue("native display-color query succeeds", primaryColor.status.ok()));
        if (primaryColor.status.ok())
        {
            static_cast<void>(context.expectEq("display-color result retains monitor identity", primary.monitor.id, primaryColor.info.monitor));
            static_cast<void>(context.expectTrue(
                "display-color channel precision is plausible",
                primaryColor.info.bitsPerColorChannel == 0 || primaryColor.info.bitsPerColorChannel <= 64));
            static_cast<void>(context.expectTrue(
                "display-color luminance is nonnegative",
                primaryColor.info.minimumLuminanceNits >= 0.0F && primaryColor.info.maximumLuminanceNits >= 0.0F &&
                    primaryColor.info.maximumFullFrameLuminanceNits >= 0.0F && primaryColor.info.sdrWhiteLevelNits >= 0.0F));
        }

        Window::Types::Description description;
        description.title = "Window display-color validation";
        description.clientSize = {240, 160};
        description.visible = false;
        Window::Window window;
        static_cast<void>(context.expectTrue("display-color Window fixture opens", window.open(description, 8).ok()));
        if (!window.isOpen())
            return;

        const Window::Types::DisplayColorInfoResult windowColor = Feedback::getWindowDisplayColorInfo(window);
        static_cast<void>(context.expectTrue("Window display-color query succeeds", windowColor.status.ok()));
        if (windowColor.status.ok())
            static_cast<void>(context.expectEq("Window color query uses its cached monitor", window.currentMonitor(), windowColor.info.monitor));

        ErrorCode wrongThreadCode = ErrorCode::Success;
        std::thread worker(
            [&window, &wrongThreadCode]
            {
                wrongThreadCode = Feedback::getWindowDisplayColorInfo(window).status.code;
            });
        worker.join();
        static_cast<void>(context.expectEq("wrong-thread Window color query is rejected", ErrorCode::ResourceBusy, wrongThreadCode));

#if INTERNAL_WINDOW_TEST_HOOKS
        Window::TestHooks::failNext(Window::TestHooks::FailurePoint::DisplayColorQuery);
        static_cast<void>(context.expectEq(
            "display-color native failure is translated",
            ErrorCode::StatFailed,
            Feedback::getDisplayColorInfo(window.currentMonitor()).status.code));

        Window::TestHooks::makeNextDisplayColorMetadataUnavailable();
        const Window::Types::DisplayColorInfoResult runtimeUnavailable = Feedback::getDisplayColorInfo(window.currentMonitor());
        static_cast<void>(context.expectTrue("runtime metadata absence still succeeds", runtimeUnavailable.status.ok()));
        static_cast<void>(context.expectEq(
            "runtime metadata absence remains unknown",
            Window::Types::DisplayColorSpace::Unknown,
            runtimeUnavailable.info.activeColorSpace));
        static_cast<void>(
            context.expectEq("runtime metadata absence keeps zero precision", std::uint16_t{0}, runtimeUnavailable.info.bitsPerColorChannel));

        const Window::Types::MonitorId fixtureMonitor{42};
        const Window::Types::DisplayColorInfo unavailable = Window::TestHooks::makeDisplayColorInfo(fixtureMonitor, {});
        static_cast<void>(context.expectEq("unavailable color metadata preserves identity", fixtureMonitor, unavailable.monitor));
        static_cast<void>(
            context.expectEq("unavailable color metadata remains unknown", Window::Types::DisplayColorSpace::Unknown, unavailable.activeColorSpace));
        static_cast<void>(context.expectEq("unavailable color precision remains zero", std::uint16_t{0}, unavailable.bitsPerColorChannel));
        static_cast<void>(context.expectNear("unavailable SDR white remains zero", 0.0, unavailable.sdrWhiteLevelNits, 0.001));

        const Window::Types::DisplayColorInfo hdrDisabled = Window::TestHooks::makeDisplayColorInfo(
            fixtureMonitor,
            {.activeColorSpace = Window::Types::DisplayColorSpace::Srgb,
             .wideColorGamutSupported = true,
             .hdrSupported = true,
             .hdrEnabled = false,
             .bitsPerColorChannel = 10,
             .minimumLuminanceNits = -1.0F,
             .maximumLuminanceNits = 1000.0F,
             .maximumFullFrameLuminanceNits = std::numeric_limits<float>::quiet_NaN(),
             .sdrWhiteLevelMilli80Nits = 2500});
        static_cast<void>(context.expectTrue("HDR-capable disabled fixture retains support", hdrDisabled.hdrSupported));
        static_cast<void>(context.expectFalse("HDR-capable disabled fixture remains disabled", hdrDisabled.hdrEnabled));
        static_cast<void>(context.expectEq("HDR-disabled fixture remains SDR", Window::Types::DisplayColorSpace::Srgb, hdrDisabled.activeColorSpace));
        static_cast<void>(context.expectEq("color precision converts to public width", std::uint16_t{10}, hdrDisabled.bitsPerColorChannel));
        static_cast<void>(context.expectNear("negative luminance becomes unavailable", 0.0, hdrDisabled.minimumLuminanceNits, 0.001));
        static_cast<void>(context.expectNear("peak luminance keeps nit units", 1000.0, hdrDisabled.maximumLuminanceNits, 0.001));
        static_cast<void>(
            context.expectNear("non-finite full-frame luminance becomes unavailable", 0.0, hdrDisabled.maximumFullFrameLuminanceNits, 0.001));
        static_cast<void>(context.expectNear("SDR white converts from 80-nit thousandths", 200.0, hdrDisabled.sdrWhiteLevelNits, 0.001));

        const Window::Types::DisplayColorInfo hdrEnabled = Window::TestHooks::makeDisplayColorInfo(
            fixtureMonitor,
            {.activeColorSpace = Window::Types::DisplayColorSpace::Hdr10Pq,
             .wideColorGamutSupported = true,
             .hdrSupported = true,
             .hdrEnabled = true,
             .bitsPerColorChannel = std::numeric_limits<std::uint32_t>::max()});
        static_cast<void>(context.expectTrue("HDR fixture remains enabled", hdrEnabled.hdrEnabled));
        static_cast<void>(context.expectEq("HDR fixture remains PQ", Window::Types::DisplayColorSpace::Hdr10Pq, hdrEnabled.activeColorSpace));
        static_cast<void>(
            context.expectEq("oversized channel precision saturates", std::numeric_limits<std::uint16_t>::max(), hdrEnabled.bitsPerColorChannel));

        const Window::Types::DisplayColorInfo wideColor = Window::TestHooks::makeDisplayColorInfo(
            fixtureMonitor,
            {.activeColorSpace = Window::Types::DisplayColorSpace::WideColorGamut, .wideColorGamutSupported = true});
        static_cast<void>(
            context.expectEq("advanced-color SDR remains wide-gamut", Window::Types::DisplayColorSpace::WideColorGamut, wideColor.activeColorSpace));
        static_cast<void>(context.expectFalse("wide-gamut SDR is not inferred as HDR", wideColor.hdrEnabled));

        window.clearEvents();
        Window::TestHooks::simulateDisplayColorConfigurationChange();
        static_cast<void>(context.expectTrue("display-color transition pump succeeds", Window::pollEvents().status.ok()));
        static_cast<void>(context.expectTrue(
            "display-color transition reuses display-configuration event",
            consumeEventOfType<Window::Types::DisplayConfigurationChangedEvent>(window)));
#endif

        static_cast<void>(context.expectTrue("display-color Window fixture closes", window.close().ok()));
    }

    void testHiddenNativeWindow(TestSupport::Context &context)
    {
        Window::Types::Description description;
        description.title = "GameWIP Window validation";
        description.clientSize = {320, 200};
        description.visible = false;

        Window::Window owner;
        const IO::Types::Status openStatus = owner.open(description, 32);
        static_cast<void>(context.expectTrue("hidden native Window opens", openStatus.ok()));
        if (!openStatus.ok())
            return;
        static_cast<void>(context.expectTrue("open Window has an id", owner.id().valid()));
        static_cast<void>(context.expectEq("open Window reports Open lifetime", Window::Types::LifetimeState::Open, owner.lifetimeState()));
        static_cast<void>(context.expectEq("title cache matches", std::string_view{description.title}, owner.title()));
        static_cast<void>(
            context.expectEq("internal event storage is reported", Window::Types::EventStorageKind::Internal, owner.eventQueueInfo().storage));

        const Window::Native::Win32::HandleResult handle = Window::Native::Win32::getHandle(owner);
        static_cast<void>(context.expectTrue("native handle adapter succeeds", handle.status.ok()));
        static_cast<void>(context.expectTrue("native HWND is non-null", handle.handle.window != nullptr));

        static_cast<void>(context.expectTrue("UTF-8 title update succeeds", owner.setTitle("Fenêtre GameWIP").ok()));
        static_cast<void>(context.expectTrue("logical client resize succeeds", owner.setClientSize({360, 240}).ok()));
        static_cast<void>(context.expectEq("client-size cache reports applied resize", Window::Types::LogicalSize{360, 240}, owner.clientSize()));
        const Window::Types::LogicalSize beforePolicyChange = owner.clientSize();
        static_cast<void>(context.expectTrue(
            "DPI resize policy changes for future transitions",
            owner.setDpiResizePolicy(Window::Types::DpiResizePolicy::PreservePhysicalClientSize).ok()));
        static_cast<void>(context.expectEq("DPI policy setter does not resize immediately", beforePolicyChange, owner.clientSize()));
        static_cast<void>(context.expectEq(
            "unknown runtime DPI policy is invalid",
            ErrorCode::InvalidArgument,
            owner.setDpiResizePolicy(static_cast<Window::Types::DpiResizePolicy>(99)).code));
        static_cast<void>(context.expectTrue(
            "default DPI policy restores",
            owner.setDpiResizePolicy(Window::Types::DpiResizePolicy::PreserveLogicalClientSize).ok()));

        const Window::Types::LogicalPosition localPoint{12, 18};
        const Window::Types::ScreenPositionResult screenPoint = owner.clientToScreen(localPoint);
        static_cast<void>(context.expectTrue("client-to-screen conversion succeeds", screenPoint.status.ok()));
        if (screenPoint.status.ok())
        {
            const Window::Types::LogicalPositionResult roundTrip = owner.screenToClient(screenPoint.position);
            static_cast<void>(context.expectTrue("screen-to-client conversion succeeds", roundTrip.status.ok()));
            if (roundTrip.status.ok())
                static_cast<void>(context.expectEq("coordinate conversion round trips", localPoint, roundTrip.position));
        }

        static_cast<void>(context.expectTrue(
            "borderless fullscreen transition succeeds",
            owner.setMode({.mode = Window::Types::WindowMode::BorderlessFullscreen, .monitor = owner.currentMonitor()}).ok()));
        static_cast<void>(context.expectEq("borderless mode is cached", Window::Types::WindowMode::BorderlessFullscreen, owner.mode()));
        static_cast<void>(context.expectTrue("windowed placement restores", owner.setMode({}).ok()));
        static_cast<void>(context.expectEq("windowed mode is cached", Window::Types::WindowMode::Windowed, owner.mode()));

        static_cast<void>(
            context.expectTrue("runtime borderless decorations succeed", owner.setDecorationMode(Window::Types::DecorationMode::Borderless).ok()));
        static_cast<void>(context.expectTrue("system decorations restore", owner.setDecorationMode(Window::Types::DecorationMode::System).ok()));
        static_cast<void>(context.expectTrue(
            "click-through pointer policy succeeds",
            owner.setPointerInputLayout({.mode = Window::Types::PointerInputMode::ClickThrough}).ok()));
        const LONG_PTR clickThroughStyle = GetWindowLongPtrW(handle.handle.window, GWL_EXSTYLE);
        static_cast<void>(context.expectTrue(
            "click-through uses documented layered hit-testing styles",
            (clickThroughStyle & WS_EX_LAYERED) != 0 && (clickThroughStyle & WS_EX_TRANSPARENT) != 0));
        static_cast<void>(context.expectTrue("normal pointer policy restores", owner.setPointerInputLayout({}).ok()));
        const LONG_PTR normalPointerStyle = GetWindowLongPtrW(handle.handle.window, GWL_EXSTYLE);
        static_cast<void>(
            context.expectFalse("normal pointer policy removes transparent hit-testing style", (normalPointerStyle & WS_EX_TRANSPARENT) != 0));
        const std::array<Window::Types::LogicalRect, 1> pointerRegions{{{{0, 0}, {10, 10}}}};
        static_cast<void>(context.expectEq(
            "rectangular pointer routing remains Unsupported",
            ErrorCode::Unsupported,
            owner.setPointerInputLayout({.mode = Window::Types::PointerInputMode::AcceptRegions, .regions = pointerRegions}).code));
        static_cast<void>(
            context.expectEq("unsupported region request preserves pointer mode", Window::Types::PointerInputMode::Normal, owner.pointerInputMode()));
        static_cast<void>(context.expectTrue("opacity update succeeds", owner.setOpacity(0.8F).ok()));
        static_cast<void>(context.expectTrue("opacity restores", owner.setOpacity(1.0F).ok()));
        static_cast<void>(context.expectTrue("file-drop enable succeeds", owner.setFileDropEnabled(true).ok()));
        static_cast<void>(
            context.expectTrue("file-drop native style is enabled", (GetWindowLongPtrW(handle.handle.window, GWL_EXSTYLE) & WS_EX_ACCEPTFILES) != 0));
        static_cast<void>(context.expectTrue(
            "style mutation while file drops are enabled succeeds",
            owner.setDecorationMode(Window::Types::DecorationMode::Borderless).ok()));
        static_cast<void>(context.expectTrue(
            "style mutation preserves file-drop native state",
            (GetWindowLongPtrW(handle.handle.window, GWL_EXSTYLE) & WS_EX_ACCEPTFILES) != 0));
        static_cast<void>(context.expectTrue(
            "system decoration restores after file-drop style check",
            owner.setDecorationMode(Window::Types::DecorationMode::System).ok()));
        static_cast<void>(context.expectTrue("file-drop disable succeeds", owner.setFileDropEnabled(false).ok()));
        static_cast<void>(context.expectTrue("interaction disable succeeds", owner.setUserInteractionEnabled(false).ok()));
        static_cast<void>(context.expectTrue(
            "style mutation while interaction is disabled succeeds",
            owner.setDecorationMode(Window::Types::DecorationMode::Borderless).ok()));
        static_cast<void>(context.expectFalse("style mutation preserves native disabled state", IsWindowEnabled(handle.handle.window) != FALSE));
        static_cast<void>(context.expectTrue(
            "system decoration restores while interaction is disabled",
            owner.setDecorationMode(Window::Types::DecorationMode::System).ok()));
        static_cast<void>(
            context.expectFalse("decoration restoration preserves native disabled state", IsWindowEnabled(handle.handle.window) != FALSE));
        static_cast<void>(context.expectTrue("interaction re-enable succeeds", owner.setUserInteractionEnabled(true).ok()));

        static_cast<void>(
            context.expectEq("resize cannot be disabled while maximize remains enabled", ErrorCode::InvalidArgument, owner.setResizable(false).code));
        Window::Types::WindowControls controls = owner.windowControls();
        controls.maximizable = false;
        static_cast<void>(context.expectTrue("maximize is explicitly disabled", owner.setWindowControls(controls).ok()));
        static_cast<void>(context.expectTrue("resize can then be disabled", owner.setResizable(false).ok()));
        controls.maximizable = true;
        static_cast<void>(context.expectEq(
            "maximize cannot be enabled while resize is disabled",
            ErrorCode::InvalidArgument,
            owner.setWindowControls(controls).code));
        controls.maximizable = false;
        controls.closable = false;
        controls.minimizable = false;
        static_cast<void>(context.expectTrue("close and minimize remain independent from resize", owner.setWindowControls(controls).ok()));
        static_cast<void>(context.expectTrue("resize can be re-enabled explicitly", owner.setResizable(true).ok()));
        controls.maximizable = true;
        controls.closable = true;
        controls.minimizable = true;
        static_cast<void>(context.expectTrue("maximize restores after resize", owner.setWindowControls(controls).ok()));

        const Window::Types::Capabilities capabilities = Window::getCapabilities().capabilities;
        static_cast<void>(context.expectFalse(
            "Win32 does not advertise cross-application pointer regions",
            capabilities.supports(Window::Types::Capability::PointerRegions)));
        static_cast<void>(
            context.expectEq("unsupported pointer regions expose zero native limit", std::uint32_t{0}, capabilities.maximumPointerInputRegions));
        if (capabilities.supports(Window::Types::Capability::SystemBackdrop))
        {
            static_cast<void>(context.expectTrue(
                "runtime-supported system backdrop applies",
                owner.setBackdropEffect(Window::Types::BackdropEffect::Automatic).ok()));
            static_cast<void>(context.expectTrue("system backdrop clears", owner.setBackdropEffect(Window::Types::BackdropEffect::None).ok()));
        }
        else
        {
            static_cast<void>(context.expectEq(
                "unsupported system backdrop is rejected",
                ErrorCode::Unsupported,
                owner.setBackdropEffect(Window::Types::BackdropEffect::Automatic).code));
        }

        const std::array<std::byte, 4> redPixel{std::byte{0xFF}, std::byte{0x00}, std::byte{0x00}, std::byte{0xFF}};
        const std::array iconImages{Window::Types::IconImageView{{1, 1}, redPixel}};
        static_cast<void>(context.expectTrue("RGBA icon copy succeeds", owner.setIcon(iconImages).ok()));
        static_cast<void>(context.expectTrue("icon clears", owner.clearIcon().ok()));

        static_cast<void>(context.expectTrue("programmatic close request succeeds", owner.requestClose().ok()));
        static_cast<void>(context.expectTrue("programmatic close request is sticky", owner.closeRequested()));
        static_cast<void>(context.expectTrue("clear close request succeeds", owner.clearCloseRequest().ok()));
        static_cast<void>(context.expectFalse("close request clears", owner.closeRequested()));

        Window::Types::Description childDescription = description;
        childDescription.owner = owner.id();
        Window::Window child;
        static_cast<void>(context.expectTrue("same-thread owned Window opens", child.open(childDescription, 16).ok()));
        if (child.isOpen())
        {
            static_cast<void>(context.expectEq("owner identity is cached", owner.id(), child.ownerId()));
            const HWND childHandle = Window::Native::Win32::getHandle(child).handle.window;
            static_cast<void>(context.expectFalse(
                "owned Window has no independent taskbar style",
                (GetWindowLongPtrW(childHandle, GWL_EXSTYLE) & WS_EX_APPWINDOW) != 0));
            child.clearEvents();
            static_cast<void>(context.expectTrue("owner can be cleared at runtime", child.setOwner({}).ok()));
            static_cast<void>(context.expectTrue(
                "owner removal restores independent taskbar style",
                (GetWindowLongPtrW(childHandle, GWL_EXSTYLE) & WS_EX_APPWINDOW) != 0));
            static_cast<void>(
                context.expectTrue("owner clear translates to OwnerChangedEvent", consumeEventOfType<Window::Types::OwnerChangedEvent>(child)));
            child.clearEvents();
            static_cast<void>(context.expectTrue("owner can be restored at runtime", child.setOwner(owner.id()).ok()));
            static_cast<void>(context.expectFalse(
                "owner restoration removes independent taskbar style",
                (GetWindowLongPtrW(childHandle, GWL_EXSTYLE) & WS_EX_APPWINDOW) != 0));
            static_cast<void>(
                context.expectTrue("owner restore translates to OwnerChangedEvent", consumeEventOfType<Window::Types::OwnerChangedEvent>(child)));
        }
        static_cast<void>(context.expectTrue("owner closes", owner.close().ok()));
        if (child.isOpen())
        {
            static_cast<void>(context.expectFalse("closing owner clears child owner identity", child.ownerId().valid()));
            static_cast<void>(context.expectTrue(
                "closing owner restores child taskbar style",
                (GetWindowLongPtrW(Window::Native::Win32::getHandle(child).handle.window, GWL_EXSTYLE) & WS_EX_APPWINDOW) != 0));
            static_cast<void>(context.expectTrue("child closes after owner", child.close().ok()));
        }
        static_cast<void>(context.expectFalse("closed owner reports closed", owner.isOpen()));
        static_cast<void>(context.expectTrue("repeated native close succeeds", owner.close().ok()));

        Window::Types::Description alphaDescription = description;
        alphaDescription.title = "Transparent framebuffer capability validation";
        alphaDescription.transparentFramebuffer = true;
        Window::Window alphaWindow;
        const IO::Types::Status alphaStatus = alphaWindow.open(alphaDescription, 4);
        if (capabilities.supports(Window::Types::Capability::TransparentFramebuffer))
        {
            static_cast<void>(context.expectTrue("runtime-supported transparent framebuffer opens", alphaStatus.ok()));
            if (alphaWindow.isOpen())
                static_cast<void>(alphaWindow.close());
        }
        else
        {
            static_cast<void>(
                context.expectEq("unsupported transparent framebuffer is rejected before open", ErrorCode::Unsupported, alphaStatus.code));
            static_cast<void>(context.expectFalse("unsupported alpha request creates no HWND", alphaWindow.isOpen()));
        }
    }

    void testMonitors(TestSupport::Context &context)
    {
        const Window::Types::MonitorListResult monitors = Window::getMonitors();
        static_cast<void>(context.expectTrue("monitor enumeration succeeds", monitors.status.ok()));
        static_cast<void>(context.expectFalse("monitor enumeration is nonempty", monitors.monitors.empty()));
        const Window::Types::MonitorInfoResult primary = Window::getPrimaryMonitor();
        static_cast<void>(context.expectTrue("primary monitor query succeeds", primary.status.ok()));
        if (!primary.status.ok())
            return;
        static_cast<void>(context.expectTrue("primary monitor id is valid", primary.monitor.id.valid()));
        static_cast<void>(context.expectTrue("primary monitor is marked primary", primary.monitor.primary));
        static_cast<void>(context.expectTrue("monitor identity resolves", Window::getMonitor(primary.monitor.id).status.ok()));
        const Window::Types::DisplayModeListResult modes = Window::getDisplayModes(primary.monitor.id);
        static_cast<void>(context.expectTrue("display-mode enumeration succeeds", modes.status.ok()));
        static_cast<void>(context.expectFalse("display-mode enumeration is nonempty", modes.displayModes.empty()));
        static_cast<void>(context.expectTrue("current display mode succeeds", Window::getCurrentDisplayMode(primary.monitor.id).status.ok()));
        const Window::Types::DisplayModeResult preferred = Window::getPreferredDisplayMode(primary.monitor.id);
        static_cast<void>(context.expectTrue("DisplayConfig preferred mode succeeds", preferred.status.ok()));
        if (preferred.status.ok())
        {
            static_cast<void>(context.expectTrue(
                "preferred mode has a physical resolution",
                preferred.displayMode.resolution.width != 0 && preferred.displayMode.resolution.height != 0));
        }
        static_cast<void>(context.expectEq("invalid monitor lookup is rejected", ErrorCode::InvalidArgument, Window::getMonitor({}).status.code));
    }
} // namespace

namespace GameWIP::Test
{
    int runWindowTests(int argc, char **argv, const WindowTestOptions &options)
    {
        const HRESULT manualShellIdentityStatus =
            options.enableManualTests ? SetCurrentProcessExplicitAppUserModelID(kManualTestAppUserModelId) : S_OK;
        std::optional<std::string_view> selectedManualSuite;
        constexpr std::string_view manualSuitePrefix = "--window-manual-suite=";
        for (int index = 1; index < argc; ++index)
        {
            if (argv[index] != nullptr && std::string_view(argv[index]).starts_with(manualSuitePrefix))
                selectedManualSuite = std::string_view(argv[index]).substr(manualSuitePrefix.size());
        }
        constexpr std::array manualSuiteNames{
            std::string_view{"lifecycle"},
            std::string_view{"multiple-windows"},
            std::string_view{"custom-chrome"},
            std::string_view{"layered-pointer"},
            std::string_view{"dpi"},
            std::string_view{"cursor"},
            std::string_view{"files-shell"},
            std::string_view{"fullscreen"},
            std::string_view{"hdr"},
            std::string_view{"modern"}};

        TestSupport::Types::Reporting::Options reportOptions;
        reportOptions.writeConsole = true;
        reportOptions.consoleVerbosity =
            options.verboseConsole ? TestSupport::Types::Reporting::ConsoleVerbosity::Full : TestSupport::Types::Reporting::ConsoleVerbosity::Minimal;
        reportOptions.writeReport = options.writeReport;
        reportOptions.appendReport = options.appendReport;
        reportOptions.reportPath = options.reportPath;

        TestSupport::Runner runner(reportOptions);
        ManualStatusWindow statusWindow(options.enableManualTests);
        manualStatusWindow = &statusWindow;
        if (options.enableManualTests)
        {
            runner.runSuite(
                "Window manual shell identity",
                [&](TestSupport::Context &context)
                {
                    static_cast<void>(context.expectTrue("manual test AppUserModelID applies", SUCCEEDED(manualShellIdentityStatus)));
                });
        }
        runner.runSuite("Window passive values and closed state", testPassiveValuesAndClosedState);
        runner.runSuite("Window description validation", testDescriptionValidation);
#if INTERNAL_WINDOW_TEST_HOOKS
        runner.runSuite("Window fixed event queue", testFixedEventQueue);
        runner.runSuite("Window sticky close", testStickyClose);
        runner.runSuite("Window deterministic failure paths", testFailureInjection);
        runner.runSuite("Window threading contracts", testThreadingContracts);
        runner.runSuite("Window exceptional lifetime", testExceptionalLifetime);
        runner.runSuite("Window packed pointer hit mask", testPointerHitMask);
#else
        runner.runSuite(
            "Window fixed event queue",
            [](TestSupport::Context &context)
            {
                context.skip("Window queue hooks", "INTERNAL_WINDOW_TEST_HOOKS is disabled");
            });
#endif
        runner.runSuite("Window hidden native lifecycle", testHiddenNativeWindow);
        runner.runSuite("Window native event translation", testNativeEventTranslation);
        runner.runSuite("Window renderer occlusion feedback", testRendererOcclusionFeedback);
        runner.runSuite("Window display color information", testDisplayColorInformation);
        runner.runSuite("Window monitors and display modes", testMonitors);
        const bool validManualSelection = !selectedManualSuite || std::ranges::find(manualSuiteNames, *selectedManualSuite) != manualSuiteNames.end();
        if (!validManualSelection)
        {
            runner.runSuite(
                "Window manual suite selection",
                [&](TestSupport::Context &context)
                {
                    context.fail("Window manual suite selector", std::format("unknown suite '{}'", *selectedManualSuite));
                });
        }
        const auto runManualSuite = [&](std::string_view displayName, std::string_view selector, auto suite)
        {
            if (!validManualSelection || (selectedManualSuite && *selectedManualSuite != selector))
                return;
            runner.runSuite(
                displayName,
                [&](TestSupport::Context &context)
                {
                    suite(context, options);
                });
        };
        runManualSuite("Window manual visible lifecycle", "lifecycle", testManualVisibleLifecycle);
        runManualSuite("Window manual multiple windows", "multiple-windows", testManualMultipleWindows);
        runManualSuite("Window manual custom chrome", "custom-chrome", testManualCustomChrome);
        runManualSuite("Window manual layered and pointer behavior", "layered-pointer", testManualLayeredAndPointer);
        runManualSuite("Window manual DPI and coordinates", "dpi", testManualDpiAndCoordinates);
        runManualSuite("Window manual cursor behavior", "cursor", testManualCursor);
        runManualSuite("Window manual files and shell behavior", "files-shell", testManualFilesAndShell);
        runManualSuite("Window manual fullscreen and topology", "fullscreen", testManualFullscreenAndTopology);
        runManualSuite("Window manual HDR and advanced color", "hdr", testManualHdrAndAdvancedColor);
        runManualSuite("Window manual modern Windows capabilities", "modern", testManualModernWindowsCapabilities);

        const TestSupport::Types::Reporting::Summary result = runner.result();
        runner.summary(std::format("Window library self-tests passed={} failed={} skipped={}", result.passed, result.failed, result.skipped));
        manualStatusWindow = nullptr;
        return runner.exitCode();
    }
} // namespace GameWIP::Test
