/// @file window_lifecycle_tests.inl
/// @brief Window lifecycle, ownership, and property validation cases.

void testPassiveValuesAndClosedState(TestSupport::Context &context)
{
    static_cast<void>(context.expectFalse("default window id is invalid", Window::Types::WindowId{}.isValid()));
    static_cast<void>(context.expectTrue("nonzero window id is valid", Window::Types::WindowId{4}.isValid()));
    static_cast<void>(context.expectFalse("default monitor id is invalid", Window::Types::Display::MonitorId{}.isValid()));
    static_cast<void>(context.expectTrue(
        "geometry values compare structurally",
        Window::Types::LogicalRect{{-4, 8}, {10, 12}} == Window::Types::LogicalRect{{-4, 8}, {10, 12}}));
    static_cast<void>(
        context.expectEq("hit-mask words round each row independently", std::size_t{4}, Window::Renderer::requiredPointerHitMaskWords({33, 2})));
    static_cast<void>(context.expectEq("empty hit-mask extent is invalid", std::size_t{0}, Window::Renderer::requiredPointerHitMaskWords({0, 8})));
#if WINDOW_INTERNAL_TEST_HOOKS
    static_cast<void>(
        context.expectEq("60000/1001 refresh rounds to millihertz", std::uint32_t{59'940}, Window::TestHooks::refreshRateMillihertz(60'000, 1'001)));
    static_cast<void>(
        context.expectEq("unknown rational refresh remains zero", std::uint32_t{0}, Window::TestHooks::refreshRateMillihertz(60'000, 0)));
    static_cast<void>(context.expectEq(
        "rational refresh conversion saturates",
        std::numeric_limits<std::uint32_t>::max(),
        Window::TestHooks::refreshRateMillihertz(std::numeric_limits<std::uint32_t>::max(), 1)));
    const Window::Types::Display::Mode exactMode{{1920, 1080}, 60'000, 32, false};
    static_cast<void>(context.expectTrue(
        "exclusive comparator accepts an exact native mode",
        Window::TestHooks::exactNativeDisplayModeMatches(exactMode, 1920, 1080, 60, 32, false)));
    Window::Types::Display::Mode fractionalMode = exactMode;
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
    static_cast<void>(
        context.expectEq("physical-size DPI policy recalculates logical extent", Window::Types::LogicalSize{533, 400}, preservePhysical.logicalSize));
    static_cast<void>(
        context.expectEq("physical-size DPI policy preserves framebuffer", Window::Types::PixelSize{800, 600}, preservePhysical.framebufferSize));
#endif

    const Window::Types::CapabilitiesResult capabilities = Window::getCapabilities();
    static_cast<void>(context.expectTrue("capability query succeeds", capabilities.status.ok()));
    static_cast<void>(
        context.expectTrue("Win32 supports multiple windows", capabilities.capabilities.supports(Window::Types::Capability::MultipleWindows)));
    static_cast<void>(context.expectFalse("Count is not a capability", capabilities.capabilities.supports(Window::Types::Capability::Count)));

    const Window::Types::Events::PumpResult idlePoll = Window::Events::poll();
    static_cast<void>(context.expectTrue("poll with no windows is a successful no-op", idlePoll.status.ok()));
    const Window::Types::Events::PumpResult badWait = Window::Events::wait(std::chrono::milliseconds{-2});
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
    description.mode.displayMode = Window::Types::Display::Mode{{1920, 1080}, 60'000, 32, false};
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

#if WINDOW_INTERNAL_TEST_HOOKS
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
    static_cast<void>(context.expectEq("region copy failure is translated", ErrorCode::OutOfMemory, window.setCustomChromeLayout(chromeLayout).code));

    const std::array<std::byte, 4> pixel{std::byte{0x40}, std::byte{0x80}, std::byte{0xC0}, std::byte{0xFF}};
    const std::array iconImages{Window::Types::IconImageView{{1, 1}, pixel}};
    Window::TestHooks::failNext(FailurePoint::IconConversion);
    static_cast<void>(context.expectEq("icon conversion failure is translated", ErrorCode::NativeFailure, window.setIcon(iconImages).code));
    static_cast<void>(context.expectTrue("icon operation recovers after failure", window.setIcon(iconImages).ok()));
    static_cast<void>(window.clearIcon());

    Window::TestHooks::failNext(FailurePoint::Cursor);
    static_cast<void>(
        context.expectEq("cursor failure is translated", ErrorCode::NativeFailure, window.setCursorMode(Window::Types::CursorMode::Confined).code));
    static_cast<void>(context.expectEq("failed cursor update rolls back cache", Window::Types::CursorMode::Normal, window.cursorMode()));

    Window::TestHooks::failNext(FailurePoint::MonitorQuery);
    static_cast<void>(
        context.expectEq("monitor query failure is translated", ErrorCode::StatFailed, Window::Display::getPrimaryMonitor().status.code));
    Window::TestHooks::failNext(FailurePoint::DisplayEnumeration);
    static_cast<void>(
        context.expectEq("display enumeration failure is translated", ErrorCode::StatFailed, Window::Display::getMonitors().status.code));

    const Window::Types::Display::MonitorId monitor = window.currentMonitor();
    Window::TestHooks::failNext(FailurePoint::FullscreenPartial);
    static_cast<void>(context.expectEq(
        "partial fullscreen failure is translated",
        ErrorCode::NativeFailure,
        window.setMode({.mode = Window::Types::Mode::BorderlessFullscreen, .monitor = monitor}).code));
    static_cast<void>(context.expectEq("partial fullscreen failure restores mode", Window::Types::Mode::Windowed, window.mode()));

    static_cast<void>(context.expectTrue(
        "borderless mode opens restoration boundary",
        window.setMode({.mode = Window::Types::Mode::BorderlessFullscreen, .monitor = monitor}).ok()));
    Window::TestHooks::failNext(FailurePoint::DisplayRestoration);
    static_cast<void>(context.expectEq("display restoration failure is translated", ErrorCode::NativeFailure, window.setMode({}).code));
    static_cast<void>(
        context.expectEq("failed display restoration preserves previous mode", Window::Types::Mode::BorderlessFullscreen, window.mode()));
    static_cast<void>(context.expectTrue("display restoration retry succeeds", window.setMode({}).ok()));

    Window::TestHooks::failNext(FailurePoint::EventPump);
    static_cast<void>(context.expectEq("event pump failure is translated", ErrorCode::NativeFailure, Window::Events::poll().status.code));
    static_cast<void>(context.expectTrue("event pump recovers after failure", Window::Events::poll().status.ok()));

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

    const Window::Types::Events::PumpResult wakeResult = Window::Events::wait(std::chrono::milliseconds{100});
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
        static_cast<void>(Window::TestHooks::enqueue(unexpected, Window::Types::Events::RedrawRequested{}));
        static_cast<void>(context.expectTrue("test hook destroys native HWND unexpectedly", Window::TestHooks::destroyNativeWindow(unexpected).ok()));
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
        static_cast<void>(
            context.expectTrue("terminal event is typed NativeDestroyed", event.getIf<Window::Types::Events::NativeDestroyed>() != nullptr));
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
    static_cast<void>(context.expectTrue("owner pump drains deferred cleanup", Window::Events::poll().status.ok()));
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

#endif

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
    static_cast<void>(context.expectTrue("open Window has an id", owner.id().isValid()));
    static_cast<void>(context.expectEq("open Window reports Open lifetime", Window::Types::LifetimeState::Open, owner.lifetimeState()));
    static_cast<void>(context.expectEq("title cache matches", std::string_view{description.title}, owner.title()));
    static_cast<void>(
        context.expectEq("internal event storage is reported", Window::Types::Events::StorageKind::Internal, owner.eventQueueInfo().storage));

    const Window::Native::Win32::HandleResult handle = Window::Native::Win32::getHandle(owner);
    static_cast<void>(context.expectTrue("native handle adapter succeeds", handle.status.ok()));
    static_cast<void>(context.expectTrue("native HWND is non-null", handle.handle.window != nullptr));

    static_cast<void>(context.expectTrue("UTF-8 title update succeeds", owner.setTitle("FenÃªtre GameWIP").ok()));
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
    static_cast<void>(
        context.expectTrue("default DPI policy restores", owner.setDpiResizePolicy(Window::Types::DpiResizePolicy::PreserveLogicalClientSize).ok()));

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
        owner.setMode({.mode = Window::Types::Mode::BorderlessFullscreen, .monitor = owner.currentMonitor()}).ok()));
    static_cast<void>(context.expectEq("borderless mode is cached", Window::Types::Mode::BorderlessFullscreen, owner.mode()));
    HMONITOR fullscreenMonitor = MonitorFromWindow(handle.handle.window, MONITOR_DEFAULTTONEAREST);
    MONITORINFO fullscreenMonitorInfo{};
    fullscreenMonitorInfo.cbSize = sizeof(fullscreenMonitorInfo);
    const bool fullscreenMonitorResolved = fullscreenMonitor != nullptr && GetMonitorInfoW(fullscreenMonitor, &fullscreenMonitorInfo) != FALSE;
    static_cast<void>(context.expectTrue("borderless native monitor resolves", fullscreenMonitorResolved));
    if (fullscreenMonitorResolved)
    {
        RECT ignoredWindowedSuggestion{17, 23, 657, 503};
        const UINT currentDpi = GetDpiForWindow(handle.handle.window);
        static_cast<void>(SendMessageW(
            handle.handle.window,
            WM_DPICHANGED,
            MAKEWPARAM(currentDpi, currentDpi),
            reinterpret_cast<LPARAM>(&ignoredWindowedSuggestion)));
        RECT fullscreenRect{};
        const bool fullscreenRectResolved = GetWindowRect(handle.handle.window, &fullscreenRect) != FALSE;
        static_cast<void>(context.expectTrue("borderless bounds remain queryable after DPI notification", fullscreenRectResolved));
        if (fullscreenRectResolved)
        {
            static_cast<void>(context.expectTrue(
                "fullscreen DPI notification preserves exact monitor bounds",
                EqualRect(&fullscreenRect, &fullscreenMonitorInfo.rcMonitor) != FALSE));
        }

        static_cast<void>(context.expectTrue(
            "borderless repair fixture perturbs native bounds",
            SetWindowPos(
                handle.handle.window,
                nullptr,
                fullscreenMonitorInfo.rcMonitor.left + 31,
                fullscreenMonitorInfo.rcMonitor.top + 23,
                640,
                480,
                SWP_NOZORDER | SWP_NOACTIVATE) != FALSE));
        static_cast<void>(context.expectTrue(
            "repeated borderless request succeeds",
            owner.setMode({.mode = Window::Types::Mode::BorderlessFullscreen, .monitor = owner.currentMonitor()}).ok()));
        const bool repairedRectResolved = GetWindowRect(handle.handle.window, &fullscreenRect) != FALSE;
        static_cast<void>(context.expectTrue("repaired borderless bounds remain queryable", repairedRectResolved));
        if (repairedRectResolved)
        {
            static_cast<void>(context.expectTrue(
                "repeated borderless request repairs exact monitor bounds",
                EqualRect(&fullscreenRect, &fullscreenMonitorInfo.rcMonitor) != FALSE));
        }
    }
    static_cast<void>(context.expectTrue("windowed placement restores", owner.setMode({}).ok()));
    static_cast<void>(context.expectEq("windowed mode is cached", Window::Types::Mode::Windowed, owner.mode()));

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
    static_cast<void>(context.expectFalse("decoration restoration preserves native disabled state", IsWindowEnabled(handle.handle.window) != FALSE));
    static_cast<void>(context.expectTrue("interaction re-enable succeeds", owner.setUserInteractionEnabled(true).ok()));

    static_cast<void>(
        context.expectEq("resize cannot be disabled while maximize remains enabled", ErrorCode::InvalidArgument, owner.setResizable(false).code));
    Window::Types::Controls controls = owner.controls();
    controls.maximizable = false;
    static_cast<void>(context.expectTrue("maximize is explicitly disabled", owner.setControls(controls).ok()));
    static_cast<void>(context.expectTrue("resize can then be disabled", owner.setResizable(false).ok()));
    controls.maximizable = true;
    static_cast<void>(
        context.expectEq("maximize cannot be enabled while resize is disabled", ErrorCode::InvalidArgument, owner.setControls(controls).code));
    controls.maximizable = false;
    controls.closable = false;
    controls.minimizable = false;
    static_cast<void>(context.expectTrue("close and minimize remain independent from resize", owner.setControls(controls).ok()));
    static_cast<void>(context.expectTrue("resize can be re-enabled explicitly", owner.setResizable(true).ok()));
    controls.maximizable = true;
    controls.closable = true;
    controls.minimizable = true;
    static_cast<void>(context.expectTrue("maximize restores after resize", owner.setControls(controls).ok()));

    const Window::Types::Capabilities capabilities = Window::getCapabilities().capabilities;
    static_cast<void>(context.expectFalse(
        "Win32 does not advertise cross-application pointer regions",
        capabilities.supports(Window::Types::Capability::PointerRegions)));
    static_cast<void>(
        context.expectEq("unsupported pointer regions expose zero native limit", std::uint32_t{0}, capabilities.maximumPointerInputRegions));
    if (capabilities.supports(Window::Types::Capability::SystemBackdrop))
    {
        static_cast<void>(
            context.expectTrue("runtime-supported system backdrop applies", owner.setBackdropEffect(Window::Types::BackdropEffect::Automatic).ok()));
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
    static_cast<void>(context.expectTrue("programmatic close request is sticky", owner.hasCloseRequest()));
    static_cast<void>(context.expectTrue("clear close request succeeds", owner.clearCloseRequest().ok()));
    static_cast<void>(context.expectFalse("close request clears", owner.hasCloseRequest()));

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
            context.expectTrue("owner clear translates to OwnerChangedEvent", consumeEventOfType<Window::Types::Events::OwnerChanged>(child)));
        child.clearEvents();
        static_cast<void>(context.expectTrue("owner can be restored at runtime", child.setOwner(owner.id()).ok()));
        static_cast<void>(context.expectFalse(
            "owner restoration removes independent taskbar style",
            (GetWindowLongPtrW(childHandle, GWL_EXSTYLE) & WS_EX_APPWINDOW) != 0));
        static_cast<void>(
            context.expectTrue("owner restore translates to OwnerChangedEvent", consumeEventOfType<Window::Types::Events::OwnerChanged>(child)));
    }
    static_cast<void>(context.expectTrue("owner closes", owner.close().ok()));
    if (child.isOpen())
    {
        static_cast<void>(context.expectFalse("closing owner clears child owner identity", child.ownerId().isValid()));
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
        static_cast<void>(context.expectEq("unsupported transparent framebuffer is rejected before open", ErrorCode::Unsupported, alphaStatus.code));
        static_cast<void>(context.expectFalse("unsupported alpha request creates no HWND", alphaWindow.isOpen()));
    }
}
