/// @file desktop_native_window_tests.inl
/// @brief Native window behavior and result precondition tests.

/// ------------------------------------------------------------
/// Native window and result behavior
/// ------------------------------------------------------------

void testHiddenNativeWindow(TestSupport::Context &context)
{
    // Open a hidden Window and verify its initial state.
    Desktop::Types::Description description;
    description.title = "GameWIP Desktop validation";
    description.windowed.clientSize = {320, 200};
    description.activation.visible = false;

    Desktop::Window owner;
    const IO::Types::Status openStatus = owner.open(description, Desktop::Events::owned(32));
    static_cast<void>(context.expectTrue("hidden native Window opens", openStatus.ok()));

    if(!openStatus.ok())
    {
        return;
    }

    static_cast<void>(context.expectTrue("open Window has an id", owner.id().isValid()));
    static_cast<void>(context.expectEq("open Window reports Open lifetime", Desktop::Types::LifetimeState::Open, owner.lifetimeState()));
    static_cast<void>(context.expectEq("title cache matches", std::string_view{description.title}, owner.title()));
    static_cast<void>(
        context.expectEq("internal event storage is reported", Desktop::Types::Events::StorageKind::Internal, owner.eventQueueInfo().storage));

    // Require a native handle before querying Win32 state.
    const auto handle = Desktop::Native::Win32::getHandle(owner);
    if(!context.expectTrue("native handle resolves", handle.status.ok() && handle->window != nullptr))
    {
        static_cast<void>(owner.close());
        return;
    }

    // Check title, sizing, DPI policy, and coordinate conversions.
    static_cast<void>(context.expectTrue("UTF-8 title update succeeds", owner.setTitle("Fenêtre GameWIP").ok()));
    static_cast<void>(context.expectTrue("logical client resize succeeds", owner.setClientSize({360, 240}).ok()));
    static_cast<void>(context.expectEq("client-size cache reports applied resize", Desktop::Types::LogicalSize{360, 240}, owner.clientSize()));
    const Desktop::Types::LogicalSize beforePolicyChange = owner.clientSize();
    static_cast<void>(context.expectTrue(
        "DPI resize policy changes for future transitions",
        owner.setDpiResizePolicy(Desktop::Types::DpiResizePolicy::PreservePhysicalClientSize).ok()));
    static_cast<void>(context.expectEq("DPI policy setter does not resize immediately", beforePolicyChange, owner.clientSize()));
    static_cast<void>(context.expectEq(
        "unknown runtime DPI policy is invalid",
        ErrorCode::InvalidArgument,
        owner.setDpiResizePolicy(static_cast<Desktop::Types::DpiResizePolicy>(99)).code));
    static_cast<void>(
        context.expectTrue("default DPI policy restores", owner.setDpiResizePolicy(Desktop::Types::DpiResizePolicy::PreserveLogicalClientSize).ok()));

    const Desktop::Types::LogicalPosition localPoint{12, 18};
    const auto screenPoint = owner.clientToScreen(localPoint);
    static_cast<void>(context.expectTrue("client-to-screen conversion succeeds", screenPoint.status.ok()));

    if(screenPoint.status.ok())
    {
        const auto roundTrip = owner.screenToClient(*screenPoint);
        static_cast<void>(context.expectTrue("screen-to-client conversion succeeds", roundTrip.status.ok()));

        if(roundTrip.status.ok())
        {
            static_cast<void>(context.expectEq("coordinate conversion round trips", localPoint, *roundTrip));
        }
    }

    // Check fullscreen placement and repair after native bounds drift.
    static_cast<void>(context.expectTrue(
        "borderless fullscreen transition succeeds",
        owner.setWindowMode(Desktop::Types::WindowModes::borderlessFullscreen(owner.currentMonitor())).ok()));
    static_cast<void>(context.expectEq("borderless mode is cached", Desktop::Types::WindowMode::BorderlessFullscreen, owner.windowMode()));
    HMONITOR fullscreenMonitor = MonitorFromWindow(handle->window, MONITOR_DEFAULTTONEAREST);
    MONITORINFO fullscreenMonitorInfo{};
    fullscreenMonitorInfo.cbSize = sizeof(fullscreenMonitorInfo);
    const bool fullscreenMonitorResolved = fullscreenMonitor != nullptr && GetMonitorInfoW(fullscreenMonitor, &fullscreenMonitorInfo) != FALSE;
    static_cast<void>(context.expectTrue("borderless native monitor resolves", fullscreenMonitorResolved));

    if(fullscreenMonitorResolved)
    {
        RECT ignoredWindowedSuggestion{17, 23, 657, 503};
        const UINT currentDpi = GetDpiForWindow(handle->window);
        static_cast<void>(
            SendMessageW(handle->window, WM_DPICHANGED, MAKEWPARAM(currentDpi, currentDpi), reinterpret_cast<LPARAM>(&ignoredWindowedSuggestion)));
        RECT fullscreenRect{};
        const bool fullscreenRectResolved = GetWindowRect(handle->window, &fullscreenRect) != FALSE;
        static_cast<void>(context.expectTrue("borderless bounds remain queryable after DPI notification", fullscreenRectResolved));

        if(fullscreenRectResolved)
        {
            static_cast<void>(context.expectTrue(
                "fullscreen DPI notification preserves exact monitor bounds",
                EqualRect(&fullscreenRect, &fullscreenMonitorInfo.rcMonitor) != FALSE));
        }

        static_cast<void>(context.expectTrue(
            "borderless repair fixture perturbs native bounds",
            SetWindowPos(
                handle->window,
                nullptr,
                fullscreenMonitorInfo.rcMonitor.left + 31,
                fullscreenMonitorInfo.rcMonitor.top + 23,
                640,
                480,
                SWP_NOZORDER | SWP_NOACTIVATE) != FALSE));
        static_cast<void>(context.expectTrue(
            "repeated borderless request succeeds",
            owner.setWindowMode(Desktop::Types::WindowModes::borderlessFullscreen(owner.currentMonitor())).ok()));
        const bool repairedRectResolved = GetWindowRect(handle->window, &fullscreenRect) != FALSE;
        static_cast<void>(context.expectTrue("repaired borderless bounds remain queryable", repairedRectResolved));

        if(repairedRectResolved)
        {
            static_cast<void>(context.expectTrue(
                "repeated borderless request repairs exact monitor bounds",
                EqualRect(&fullscreenRect, &fullscreenMonitorInfo.rcMonitor) != FALSE));
        }
    }

    static_cast<void>(context.expectTrue("windowed placement restores", owner.setWindowMode(Desktop::Types::WindowModes::windowed()).ok()));
    static_cast<void>(context.expectEq("windowed mode is cached", Desktop::Types::WindowMode::Windowed, owner.windowMode()));

    static_cast<void>(
        context.expectTrue("runtime borderless decorations succeed", owner.setDecorationMode(Desktop::Types::DecorationMode::Borderless).ok()));
    static_cast<void>(context.expectTrue("system decorations restore", owner.setDecorationMode(Desktop::Types::DecorationMode::System).ok()));
    // Verify decoration and pointer policies.
    static_cast<void>(context.expectTrue(
        "click-through pointer policy succeeds",
        owner.setPointerInputLayout({.mode = Desktop::Types::PointerInputMode::ClickThrough}).ok()));
    const LONG_PTR clickThroughStyle = GetWindowLongPtrW(handle->window, GWL_EXSTYLE);
    static_cast<void>(context.expectTrue(
        "click-through uses documented layered hit-testing styles",
        (static_cast<ULONG_PTR>(clickThroughStyle) & static_cast<ULONG_PTR>(WS_EX_LAYERED)) != 0 &&
            (static_cast<ULONG_PTR>(clickThroughStyle) & static_cast<ULONG_PTR>(WS_EX_TRANSPARENT)) != 0));
    static_cast<void>(context.expectTrue("normal pointer policy restores", owner.setPointerInputLayout({}).ok()));
    const LONG_PTR normalPointerStyle = GetWindowLongPtrW(handle->window, GWL_EXSTYLE);
    static_cast<void>(context.expectFalse(
        "normal pointer policy removes transparent hit-testing style",
        (static_cast<ULONG_PTR>(normalPointerStyle) & static_cast<ULONG_PTR>(WS_EX_TRANSPARENT)) != 0));

    const std::array<Desktop::Types::LogicalRect, 1> pointerRegions{{{{0, 0}, {10, 10}}}};
    static_cast<void>(context.expectEq(
        "rectangular pointer routing remains Unsupported",
        ErrorCode::Unsupported,
        owner.setPointerInputLayout({.mode = Desktop::Types::PointerInputMode::AcceptRegions, .regions = pointerRegions}).code));
    static_cast<void>(
        context.expectEq("unsupported region request preserves pointer mode", Desktop::Types::PointerInputMode::Normal, owner.pointerInputMode()));

    // Check opacity and file-drop styles.
    static_cast<void>(context.expectTrue("opacity update succeeds", owner.setOpacity(0.8F).ok()));
    static_cast<void>(context.expectTrue("opacity restores", owner.setOpacity(1.0F).ok()));
    static_cast<void>(context.expectTrue("file-drop enable succeeds", owner.setFileDropEnabled(true).ok()));
    static_cast<void>(context.expectTrue(
        "file-drop native style is enabled",
        (static_cast<ULONG_PTR>(GetWindowLongPtrW(handle->window, GWL_EXSTYLE)) & static_cast<ULONG_PTR>(WS_EX_ACCEPTFILES)) != 0));
    static_cast<void>(context.expectTrue(
        "style mutation while file drops are enabled succeeds",
        owner.setDecorationMode(Desktop::Types::DecorationMode::Borderless).ok()));
    static_cast<void>(context.expectTrue(
        "style mutation preserves file-drop native state",
        (static_cast<ULONG_PTR>(GetWindowLongPtrW(handle->window, GWL_EXSTYLE)) & static_cast<ULONG_PTR>(WS_EX_ACCEPTFILES)) != 0));
    static_cast<void>(context.expectTrue(
        "system decoration restores after file-drop style check",
        owner.setDecorationMode(Desktop::Types::DecorationMode::System).ok()));
    static_cast<void>(context.expectTrue("file-drop disable succeeds", owner.setFileDropEnabled(false).ok()));

    // Preserve disabled interaction across style changes.
    static_cast<void>(context.expectTrue("interaction disable succeeds", owner.setUserInteractionEnabled(false).ok()));
    static_cast<void>(context.expectTrue(
        "style mutation while interaction is disabled succeeds",
        owner.setDecorationMode(Desktop::Types::DecorationMode::Borderless).ok()));
    static_cast<void>(context.expectFalse("style mutation preserves native disabled state", IsWindowEnabled(handle->window) != FALSE));
    static_cast<void>(context.expectTrue(
        "system decoration restores while interaction is disabled",
        owner.setDecorationMode(Desktop::Types::DecorationMode::System).ok()));
    static_cast<void>(context.expectFalse("decoration restoration preserves native disabled state", IsWindowEnabled(handle->window) != FALSE));
    static_cast<void>(context.expectTrue("interaction re-enable succeeds", owner.setUserInteractionEnabled(true).ok()));

    // Check resize and control constraints.
    static_cast<void>(
        context.expectEq("resize cannot be disabled while maximize remains enabled", ErrorCode::InvalidArgument, owner.setResizable(false).code));
    Desktop::Types::Controls controls = owner.controls();
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

    // Check capabilities, backdrop support, and icon handling.
    const auto capabilitiesResult = Desktop::getCapabilities();
    const Desktop::Types::Capabilities capabilities = capabilitiesResult.value_or({});
    static_cast<void>(context.expectFalse(
        "Win32 does not advertise cross-application pointer regions",
        capabilities.supports(Desktop::Types::Capability::PointerRegions)));
    static_cast<void>(
        context.expectEq("unsupported pointer regions expose zero native limit", std::uint32_t{0}, capabilities.maximumPointerInputRegions));

    if(capabilities.supports(Desktop::Types::Capability::SystemBackdrop))
    {
        static_cast<void>(
            context.expectTrue("runtime-supported system backdrop applies", owner.setBackdropEffect(Desktop::Types::BackdropEffect::Automatic).ok()));
        static_cast<void>(context.expectTrue("system backdrop clears", owner.setBackdropEffect(Desktop::Types::BackdropEffect::None).ok()));
    }
    else
    {
        static_cast<void>(context.expectEq(
            "unsupported system backdrop is rejected",
            ErrorCode::Unsupported,
            owner.setBackdropEffect(Desktop::Types::BackdropEffect::Automatic).code));
    }

    const std::array<std::byte, 4> redPixel{std::byte{0xFF}, std::byte{0x00}, std::byte{0x00}, std::byte{0xFF}};
    const std::array iconImages{Desktop::Types::IconImageView{{1, 1}, redPixel}};
    static_cast<void>(context.expectTrue("RGBA icon copy succeeds", owner.setIcon(iconImages).ok()));
    static_cast<void>(context.expectTrue("icon clears", owner.clearIcon().ok()));

    // Check close requests and owner-linked Window behavior.
    static_cast<void>(context.expectTrue("programmatic close request succeeds", owner.requestClose().ok()));
    static_cast<void>(context.expectTrue("programmatic close request is sticky", owner.hasCloseRequest()));
    static_cast<void>(context.expectTrue("clear close request succeeds", owner.clearCloseRequest().ok()));
    static_cast<void>(context.expectFalse("close request clears", owner.hasCloseRequest()));

    Desktop::Types::Description childDescription = description;
    childDescription.owner = owner.id();
    Desktop::Window child;
    static_cast<void>(context.expectTrue("same-thread owned Window opens", child.open(childDescription, Desktop::Events::owned(16)).ok()));
    HWND childNativeHandle = nullptr;

    if(child.isOpen())
    {
        static_cast<void>(context.expectEq("owner identity is cached", owner.id(), child.ownerId()));
        const auto childHandle = Desktop::Native::Win32::getHandle(child);
        if(context.expectTrue("owned Window native handle resolves", childHandle.status.ok() && childHandle->window != nullptr))
        {
            childNativeHandle = childHandle->window;

            // Update ownership and verify its taskbar style and event.
            static_cast<void>(context.expectFalse(
                "owned Window has no independent taskbar style",
                (static_cast<ULONG_PTR>(GetWindowLongPtrW(childNativeHandle, GWL_EXSTYLE)) & static_cast<ULONG_PTR>(WS_EX_APPWINDOW)) != 0));
            child.clearEvents();
            static_cast<void>(context.expectTrue("owner can be cleared at runtime", child.setOwner(std::nullopt).ok()));
            static_cast<void>(context.expectTrue(
                "owner removal restores independent taskbar style",
                (static_cast<ULONG_PTR>(GetWindowLongPtrW(childNativeHandle, GWL_EXSTYLE)) & static_cast<ULONG_PTR>(WS_EX_APPWINDOW)) != 0));
            static_cast<void>(
                context.expectTrue("owner clear translates to OwnerChangedEvent", consumeEventOfType<Desktop::Types::Events::OwnerChanged>(child)));
            child.clearEvents();
            static_cast<void>(context.expectTrue("owner can be restored at runtime", child.setOwner(owner.id()).ok()));
            static_cast<void>(context.expectFalse(
                "owner restoration removes independent taskbar style",
                (static_cast<ULONG_PTR>(GetWindowLongPtrW(childNativeHandle, GWL_EXSTYLE)) & static_cast<ULONG_PTR>(WS_EX_APPWINDOW)) != 0));
            static_cast<void>(
                context.expectTrue("owner restore translates to OwnerChangedEvent", consumeEventOfType<Desktop::Types::Events::OwnerChanged>(child)));
        }
    }
    static_cast<void>(context.expectTrue("owner closes", owner.close().ok()));

    if(child.isOpen())
    {
        static_cast<void>(context.expectFalse("closing owner clears child owner identity", child.ownerId().has_value()));
        if(childNativeHandle != nullptr)
        {
            static_cast<void>(context.expectTrue(
                "closing owner restores child taskbar style",
                (static_cast<ULONG_PTR>(GetWindowLongPtrW(childNativeHandle, GWL_EXSTYLE)) & static_cast<ULONG_PTR>(WS_EX_APPWINDOW)) != 0));
        }
        static_cast<void>(context.expectTrue("child closes after owner", child.close().ok()));
    }
    static_cast<void>(context.expectFalse("closed owner reports closed", owner.isOpen()));
    static_cast<void>(context.expectTrue("repeated native close succeeds", owner.close().ok()));

    // Unsupported transparent framebuffers must fail before creating a native Window.
    Desktop::Types::Description alphaDescription = description;
    alphaDescription.title = "Transparent framebuffer capability validation";
    alphaDescription.transparentFramebuffer = true;
    Desktop::Window alphaWindow;
    const IO::Types::Status alphaStatus = alphaWindow.open(alphaDescription, Desktop::Events::owned(4));

    if(capabilities.supports(Desktop::Types::Capability::TransparentFramebuffer))
    {
        static_cast<void>(context.expectTrue("runtime-supported transparent framebuffer opens", alphaStatus.ok()));

        if(alphaWindow.isOpen())
        {
            static_cast<void>(alphaWindow.close());
        }
    }
    else
    {
        static_cast<void>(context.expectEq("unsupported transparent framebuffer is rejected before open", ErrorCode::Unsupported, alphaStatus.code));
        static_cast<void>(context.expectFalse("unsupported alpha request creates no HWND", alphaWindow.isOpen()));
    }
}

void testInitialMonitorGeometry(TestSupport::Context &context)
{
    const auto monitors = Desktop::Display::getMonitors();
    if(!context.expectTrue("initial geometry monitor enumeration succeeds", monitors.ok()))
    {
        return;
    }

    // Exercise centered and explicit placement on each connected monitor.
    for(const auto &monitor : *monitors)
    {
        for(const auto placement : {Desktop::Types::PlacementKind::Centered, Desktop::Types::PlacementKind::Explicit})
        {
            Desktop::Types::Description description;
            description.title = "Initial monitor geometry validation";
            description.activation.visible = false;
            description.windowed.clientSize = {240, 160};
            description.windowed.placement.kind = placement;

            if(placement == Desktop::Types::PlacementKind::Centered)
            {
                description.windowed.placement.monitor = monitor.id;
            }
            else
            {
                description.windowed.placement.position = {monitor.workArea.position.x + 80, monitor.workArea.position.y + 80};
            }

            Desktop::Window window;

            if(!context.expectTrue("hidden initial geometry fixture opens", window.open(description).ok()))
            {
                continue;
            }

            static_cast<void>(
                context.expectEq("initial logical size survives destination DPI", description.windowed.clientSize, window.clientSize()));
            static_cast<void>(context.expectFalse("initial geometry correction keeps the window hidden", window.visible()));
            static_cast<void>(context.expectEq("initial geometry uses the selected monitor", monitor.id, window.currentMonitor()));

            if(placement == Desktop::Types::PlacementKind::Explicit)
            {
                static_cast<void>(context.expectEq(
                    "initial explicit client origin is preserved",
                    description.windowed.placement.position,
                    window.clientPosition()));
            }

            static_cast<void>(context.expectTrue("initial geometry fixture closes", window.close().ok()));
        }
    }
}

void testResultAccessPreconditions(TestSupport::Context &context)
{
    for(const std::string_view argument : {"--desktop-test-child=result-empty-arrow", "--desktop-test-child=result-failed-dereference"})
    {
        TestSupport::Types::Process::Options child;
        child.executablePath = GameWIP::Validation::currentExecutablePath();
        child.arguments = {std::string(argument)};
        child.waitTimeout = std::chrono::seconds(10);
        child.captureOutput = true;

        const auto result = TestSupport::runChildProcess(child);
        if(!context.expectTrue("result precondition child starts", result.status.ok()))
        {
            continue;
        }

        static_cast<void>(context.expectEq("result precondition child exits", TestSupport::Types::Process::Outcome::Exited, result.outcome));
        static_cast<void>(context.expectEq("invalid result access invokes terminate", std::uint32_t{73}, result.exitCode));
    }
}
