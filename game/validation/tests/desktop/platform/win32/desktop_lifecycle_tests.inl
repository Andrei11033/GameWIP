/// @file desktop_lifecycle_tests.inl
/// @brief Window construction, threading, and lifetime tests.

/// ------------------------------------------------------------
/// Construction and failure handling
/// ------------------------------------------------------------

void testNativeConstructionLifecycle(TestSupport::Context &context)
{
    // Verify ordinary visible construction and its first event pump.
    Desktop::Types::Description visible;
    visible.title = "Visible native construction lifecycle";
    visible.windowed.clientSize = {280, 180};
    visible.activation.visible = true;

    Desktop::Window normal;
    static_cast<void>(context.expectTrue("visible Window opens", normal.open(visible, Desktop::Events::owned(8)).ok()));

    if(normal.isOpen())
    {
        const Desktop::Types::Events::PumpResult result = Desktop::Events::poll();
        static_cast<void>(context.expectTrue("visible Window creation leaves no deferred pump failure", result.status.ok()));
        static_cast<void>(context.expectFalse("visible Window pump is not NotOpen", result.status.code == ErrorCode::NotOpen));
        static_cast<void>(context.expectTrue("visible Window closes", normal.close().ok()));
    }

    // Verify borderless mode can be selected during initial creation.
    const auto primary = Desktop::Display::getPrimaryMonitor();
    static_cast<void>(context.expectTrue("primary monitor resolves for initial borderless mode", primary.ok()));

    if(!primary.ok())
    {
        return;
    }

    Desktop::Types::Description borderless = visible;
    borderless.title = "Borderless native construction lifecycle";
    borderless.initialWindowMode = Desktop::Types::WindowModes::borderlessFullscreen(primary->id);

    Desktop::Window fullscreen;
    static_cast<void>(context.expectTrue("initial borderless-fullscreen Window opens", fullscreen.open(borderless, Desktop::Events::owned(8)).ok()));

    if(fullscreen.isOpen())
    {
        const Desktop::Types::Events::PumpResult pollResult = Desktop::Events::poll();
        static_cast<void>(context.expectTrue("borderless creation poll succeeds", pollResult.status.ok()));
        static_cast<void>(context.expectFalse("borderless creation poll is not NotOpen", pollResult.status.code == ErrorCode::NotOpen));
        const Desktop::Types::Events::PumpResult waitResult = Desktop::Events::wait(std::chrono::milliseconds{0});
        static_cast<void>(context.expectTrue("borderless creation wait succeeds", waitResult.status.ok()));
        static_cast<void>(context.expectFalse("borderless creation wait is not NotOpen", waitResult.status.code == ErrorCode::NotOpen));
        static_cast<void>(context.expectTrue("initial borderless-fullscreen Window closes", fullscreen.close().ok()));
    }
}

#if DESKTOP_INTERNAL_TEST_HOOKS
void testFailureInjection(TestSupport::Context &context)
{
    using Desktop::Internal::TestHooks::FailurePoint;
    Desktop::Internal::TestHooks::resetFailures();

    Desktop::Types::Description description;
    description.title = "Window failure validation";
    description.windowed.clientSize = {280, 180};
    description.activation.visible = false;

    const auto expectFailedOpen = [&context, &description](std::string_view name, FailurePoint point, ErrorCode expected)
    {
        Desktop::Window candidate;
        Desktop::Internal::TestHooks::failNext(point);
        const IO::Types::Status status = candidate.open(description, Desktop::Events::owned(8));
        static_cast<void>(context.expectEq(name, expected, status.code));
        static_cast<void>(context.expectFalse("failed open rolls back native ownership", candidate.isOpen()));
        static_cast<void>(context.expectFalse("failed open publishes no candidate monitor", candidate.currentMonitor().isValid()));
        Desktop::Types::Event event;
        static_cast<void>(context.expectFalse("failed native creation queues no NativeDestroyed event", candidate.popEvent(event)));

        if(point == FailurePoint::WindowUserDataInstallation)
        {
            static_cast<void>(
                context.expectTrue("same Window reopens after userdata failure", candidate.open(description, Desktop::Events::owned(8)).ok()));
            static_cast<void>(candidate.close());
        }
        Desktop::Internal::TestHooks::resetFailures();
    };

    // Failed native creation must leave the object reusable.
    expectFailedOpen("allocation failure is translated", FailurePoint::Allocation, ErrorCode::OutOfMemory);
    expectFailedOpen("dispatcher failure is translated", FailurePoint::Dispatcher, ErrorCode::OpenFailed);
    expectFailedOpen("native creation failure is translated", FailurePoint::NativeCreation, ErrorCode::OpenFailed);
    expectFailedOpen("partial native open rolls back", FailurePoint::PartialOpen, ErrorCode::NativeFailure);
    expectFailedOpen("top-level userdata installation failure is translated", FailurePoint::WindowUserDataInstallation, ErrorCode::OpenFailed);
    expectFailedOpen("WM_CREATE failure after userdata installation is translated", FailurePoint::WindowCreationCallback, ErrorCode::OpenFailed);

    Desktop::Window window;
    static_cast<void>(context.expectTrue("open succeeds after injected rollback", window.open(description, Desktop::Events::owned(16)).ok()));

    if(!window.isOpen())
    {
        return;
    }
    static_cast<void>(context.expectTrue("successful open commits current monitor publication", window.currentMonitor().isValid()));

    // Failed property transitions must preserve the cached state.
    Desktop::Internal::TestHooks::failNext(FailurePoint::WindowStyleQuery);
    static_cast<void>(context.expectEq(
        "style query failure is returned before control mutation",
        ErrorCode::NativeFailure,
        window.setDecorationMode(Desktop::Types::DecorationMode::Borderless).code));
    static_cast<void>(
        context.expectEq("style query failure rolls back logical decoration state", Desktop::Types::DecorationMode::System, window.decorationMode()));

    Desktop::Internal::TestHooks::failNext(FailurePoint::WindowStyleQuery);
    static_cast<void>(context.expectEq(
        "windowed placement query failure aborts fullscreen transition",
        ErrorCode::NativeFailure,
        window.setWindowMode(Desktop::Types::WindowModes::borderlessFullscreen(window.currentMonitor())).code));
    static_cast<void>(
        context.expectEq("windowed placement query failure preserves windowed mode", Desktop::Types::WindowMode::Windowed, window.windowMode()));

    static_cast<void>(context.expectTrue(
        "fullscreen transition succeeds after placement query failure",
        window.setWindowMode(Desktop::Types::WindowModes::borderlessFullscreen(window.currentMonitor())).ok()));
    const Desktop::Types::WindowMode previousMode = window.windowMode();
    Desktop::Internal::TestHooks::failNext(FailurePoint::WindowStyleQuery);
    static_cast<void>(context.expectEq(
        "mode snapshot style query failure aborts transition",
        ErrorCode::NativeFailure,
        window.setWindowMode(Desktop::Types::WindowModes::windowed()).code));
    static_cast<void>(context.expectEq("mode snapshot failure preserves previous mode", previousMode, window.windowMode()));

    // Check conversion, region, icon, and cursor failures independently.
    const std::string originalTitle(window.title());
    Desktop::Internal::TestHooks::failNext(FailurePoint::TitleConversion);
    static_cast<void>(context.expectEq("title conversion failure is translated", ErrorCode::EncodingFailed, window.setTitle("changed").code));
    static_cast<void>(context.expectEq("failed title update preserves cache", std::string_view{originalTitle}, window.title()));

    const std::array pointerRegions{Desktop::Types::LogicalRect{{0, 0}, {16, 16}}};
    Desktop::Internal::TestHooks::failNext(FailurePoint::RegionCopy);
    const Desktop::Types::CustomChromeLayout chromeLayout{.draggableRegions = pointerRegions};
    static_cast<void>(context.expectEq("region copy failure is translated", ErrorCode::OutOfMemory, window.setCustomChromeLayout(chromeLayout).code));

    const std::array<std::byte, 4> pixel{std::byte{0x40}, std::byte{0x80}, std::byte{0xC0}, std::byte{0xFF}};
    const std::array iconImages{Desktop::Types::IconImageView{{1, 1}, pixel}};
    Desktop::Internal::TestHooks::failNext(FailurePoint::IconConversion);
    static_cast<void>(context.expectEq("icon conversion failure is translated", ErrorCode::NativeFailure, window.setIcon(iconImages).code));
    static_cast<void>(context.expectTrue("icon operation recovers after failure", window.setIcon(iconImages).ok()));
    static_cast<void>(window.clearIcon());

    Desktop::Internal::TestHooks::failNext(FailurePoint::Cursor);
    static_cast<void>(
        context.expectEq("cursor failure is translated", ErrorCode::NativeFailure, window.setCursorMode(Desktop::Types::CursorMode::Confined).code));
    static_cast<void>(context.expectEq("failed cursor update rolls back cache", Desktop::Types::CursorMode::Normal, window.cursorMode()));

    // Display and fullscreen failures must restore the prior mode.
    Desktop::Internal::TestHooks::failNext(FailurePoint::MonitorQuery);
    static_cast<void>(
        context.expectEq("monitor query failure is translated", ErrorCode::StatFailed, Desktop::Display::getPrimaryMonitor().status.code));
    Desktop::Internal::TestHooks::failNext(FailurePoint::DisplayEnumeration);
    static_cast<void>(
        context.expectEq("display enumeration failure is translated", ErrorCode::StatFailed, Desktop::Display::getMonitors().status.code));

    const Desktop::Types::Display::MonitorId monitor = window.currentMonitor();
    static_cast<void>(context.expectTrue(
        "return to windowed mode before partial fullscreen test",
        window.setWindowMode(Desktop::Types::WindowModes::windowed()).ok()));
    Desktop::Internal::TestHooks::failNext(FailurePoint::FullscreenPartial);
    static_cast<void>(context.expectEq(
        "partial fullscreen failure is translated",
        ErrorCode::NativeFailure,
        window.setWindowMode(Desktop::Types::WindowModes::borderlessFullscreen(monitor)).code));
    static_cast<void>(context.expectEq("partial fullscreen failure restores mode", Desktop::Types::WindowMode::Windowed, window.windowMode()));

    static_cast<void>(context.expectTrue(
        "borderless mode opens restoration boundary",
        window.setWindowMode(Desktop::Types::WindowModes::borderlessFullscreen(monitor)).ok()));
    Desktop::Internal::TestHooks::failNext(FailurePoint::DisplayRestoration);
    static_cast<void>(context.expectEq(
        "display restoration failure is translated",
        ErrorCode::NativeFailure,
        window.setWindowMode(Desktop::Types::WindowModes::windowed()).code));
    static_cast<void>(context.expectEq(
        "failed display restoration preserves previous mode",
        Desktop::Types::WindowMode::BorderlessFullscreen,
        window.windowMode()));
    static_cast<void>(context.expectTrue("display restoration retry succeeds", window.setWindowMode(Desktop::Types::WindowModes::windowed()).ok()));

    // Callback and pump failures are reported by the next event pump.
    const auto callbackHandle = Desktop::Native::Win32::getHandle(window);
    static_cast<void>(context.expectTrue("callback failure fixture exposes HWND", callbackHandle.status.ok() && callbackHandle->window != nullptr));

    if(callbackHandle.status.ok() && callbackHandle->window != nullptr)
    {
        static_cast<void>(SendMessageW(callbackHandle->window, WM_SHOWWINDOW, FALSE, 0));
        Desktop::Internal::TestHooks::failNext(FailurePoint::Cursor);
        static_cast<void>(SendMessageW(callbackHandle->window, WM_SHOWWINDOW, TRUE, 0));
        static_cast<void>(
            context.expectEq("genuine callback failure is returned by the next pump", ErrorCode::NativeFailure, Desktop::Events::poll().status.code));
        static_cast<void>(SendMessageW(callbackHandle->window, WM_SHOWWINDOW, FALSE, 0));
    }

    Desktop::Internal::TestHooks::failNext(FailurePoint::EventPump);
    static_cast<void>(context.expectEq("event pump failure is translated", ErrorCode::NativeFailure, Desktop::Events::poll().status.code));
    static_cast<void>(context.expectTrue("event pump recovers after failure", Desktop::Events::poll().status.ok()));

    Desktop::Internal::TestHooks::failNext(FailurePoint::Close);
    static_cast<void>(context.expectEq("close failure is translated", ErrorCode::CloseFailed, window.close().code));
    static_cast<void>(context.expectTrue("failed close preserves ownership", window.isOpen()));
    static_cast<void>(context.expectTrue("close retry succeeds", window.close().ok()));
    Desktop::Internal::TestHooks::resetFailures();
}

/// ------------------------------------------------------------
/// Threading and lifetime
/// ------------------------------------------------------------

void testThreadingContracts(TestSupport::Context &context)
{
    // Mutations and native queries belong to the Window's owner thread.
    Desktop::Types::Description description;
    description.title = "Window threading validation";
    description.windowed.clientSize = {240, 160};
    description.activation.visible = false;

    Desktop::Window window;
    static_cast<void>(context.expectTrue("threading fixture opens", window.open(description, Desktop::Events::owned(8)).ok()));

    if(!window.isOpen())
    {
        return;
    }

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
            nativeHandleCode = Desktop::Native::Win32::getHandle(window).status.code;
        });
    worker.join();

    static_cast<void>(context.expectEq("wrong-thread mutation is rejected", ErrorCode::ResourceBusy, mutationCode));
    static_cast<void>(context.expectEq("wrong-thread close is rejected", ErrorCode::ResourceBusy, closeCode));
    static_cast<void>(context.expectEq("wake is intentionally cross-thread safe", ErrorCode::Success, wakeCode));
    static_cast<void>(context.expectEq("wrong-thread native handle is rejected", ErrorCode::ResourceBusy, nativeHandleCode));
    static_cast<void>(context.expectEq("wrong-thread mutation preserves title", std::string_view{description.title}, window.title()));
    static_cast<void>(context.expectTrue("wrong-thread close preserves ownership", window.isOpen()));

    const Desktop::Types::Events::PumpResult wakeResult = Desktop::Events::wait(std::chrono::milliseconds{100});
    static_cast<void>(context.expectTrue("owner pump receives cross-thread wake", wakeResult.status.ok() && !wakeResult.timedOut));
    static_cast<void>(
        context.expectEq("reentrant event pump is rejected", ErrorCode::ResourceBusy, Desktop::Internal::TestHooks::pumpReentrantly().status.code));
    static_cast<void>(context.expectTrue("threading fixture closes", window.close().ok()));
}

void testExceptionalLifetime(TestSupport::Context &context)
{
    // Finalize an unexpected native destruction and verify the object can reopen.
    Desktop::Types::Description description;
    description.title = "Window exceptional lifetime validation";
    description.windowed.clientSize = {240, 160};
    description.activation.visible = false;

    Desktop::Window unexpected;
    static_cast<void>(context.expectTrue("unexpected-destruction fixture opens", unexpected.open(description, Desktop::Events::owned(1)).ok()));

    if(unexpected.isOpen())
    {
        static_cast<void>(context.expectTrue(
            "unexpected-destruction fixture enables concurrent reads",
            Desktop::Renderer::enableConcurrentPresentationReads(unexpected).ok()));
        static_cast<void>(Desktop::Internal::TestHooks::enqueue(unexpected, Desktop::Types::Events::RedrawRequested{}));
        static_cast<void>(
            context.expectTrue("test hook destroys native HWND unexpectedly", Desktop::Internal::TestHooks::destroyNativeWindow(unexpected).ok()));
        static_cast<void>(context.expectFalse("unexpectedly destroyed HWND is not open", unexpected.isOpen()));
        static_cast<void>(context.expectEq(
            "native destruction clears renderer-facing framebuffer size",
            Desktop::Types::PixelSize{},
            unexpected.framebufferSize()));
        static_cast<void>(context.expectFalse("native destruction clears renderer-facing visibility", unexpected.visible()));
        static_cast<void>(context.expectFalse("native destruction clears renderer-facing current monitor", unexpected.currentMonitor().isValid()));
        static_cast<void>(
            context.expectFalse("native destruction clears renderer-facing interactive state", unexpected.interactiveMoveResizeActive()));
        static_cast<void>(context.expectFalse("native destruction clears renderer-facing occlusion", unexpected.occluded()));
        static_cast<void>(context.expectEq(
            "unexpected destruction enters pending finalize",
            Desktop::Types::LifetimeState::NativeDestroyedPendingFinalize,
            unexpected.lifetimeState()));
        static_cast<void>(
            context.expectEq("mutation while pending finalize reports NotOpen", ErrorCode::NotOpen, unexpected.setTitle("unused").code));
        static_cast<void>(context.expectEq("reopen before finalization is rejected", ErrorCode::AlreadyOpen, unexpected.open(description).code));
        static_cast<void>(context.expectEq(
            "native handle is unavailable while pending finalize",
            ErrorCode::NotOpen,
            Desktop::Native::Win32::getHandle(unexpected).status.code));
        Desktop::Types::Event event;
        static_cast<void>(context.expectTrue("pending finalize retains a terminal event", unexpected.popEvent(event)));
        static_cast<void>(
            context.expectTrue("terminal event is typed NativeDestroyed", event.getIf<Desktop::Types::Events::NativeDestroyed>() != nullptr));
        static_cast<void>(context.expectTrue("controlled finalization succeeds", unexpected.close().ok()));
        static_cast<void>(
            context.expectEq("controlled finalization reaches Closed", Desktop::Types::LifetimeState::Closed, unexpected.lifetimeState()));
        static_cast<void>(context.expectTrue("Window reopens after finalization", unexpected.open(description, Desktop::Events::owned(4)).ok()));
        static_cast<void>(context.expectTrue("reopened Window closes normally", unexpected.close().ok()));
    }

    // A wrong-thread destructor queues cleanup for the owner pump.
    auto deferred = std::make_unique<Desktop::Window>();
    static_cast<void>(context.expectTrue("deferred-destruction fixture opens", deferred->open(description, Desktop::Events::owned(4)).ok()));
    HWND deferredHandle = nullptr;

    if(deferred->isOpen())
    {
        deferredHandle = Desktop::Native::Win32::getHandle(*deferred)->window;
    }
    std::thread destroyer(
        [owned = std::move(deferred)] mutable
        {
            owned.reset();
        });
    destroyer.join();
    static_cast<void>(context.expectTrue("wrong-thread destructor leaves cleanup queued", IsWindow(deferredHandle) != FALSE));
    static_cast<void>(context.expectTrue("owner pump drains deferred cleanup", Desktop::Events::poll().status.ok()));
    static_cast<void>(context.expectFalse("deferred owner cleanup destroys HWND", IsWindow(deferredHandle) != FALSE));

    // Owner-thread exit cleans up native state before object destruction.
    std::unique_ptr<Desktop::Window> survivingObject;
    HWND ownerExitHandle = nullptr;
    std::thread ownerThread(
        [&]
        {
            auto owned = std::make_unique<Desktop::Window>();

            if(owned->open(description, Desktop::Events::owned(4)).ok())
            {
                ownerExitHandle = Desktop::Native::Win32::getHandle(*owned)->window;
            }
            survivingObject = std::move(owned);
        });
    ownerThread.join();
    static_cast<void>(context.expectTrue("owner-exit fixture created a native HWND", ownerExitHandle != nullptr));
    static_cast<void>(context.expectFalse("thread-local dispatcher destroys HWND on owner exit", IsWindow(ownerExitHandle) != FALSE));

    if(survivingObject)
    {
        static_cast<void>(context.expectEq(
            "portable object observes Closed after owner exit",
            Desktop::Types::LifetimeState::Closed,
            survivingObject->lifetimeState()));
        survivingObject.reset();
    }
}

#endif
