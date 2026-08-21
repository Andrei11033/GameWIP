/// @file window_event_tests.inl
/// @brief Window event-queue and dispatch validation cases.

#if WINDOW_INTERNAL_TEST_HOOKS
void testFixedEventQueue(TestSupport::Context &context)
{
    std::array<Window::Types::Event, 3> storage;
    Window::Window window;
    static_cast<void>(context.expectTrue("portable queue hook opens", Window::TestHooks::openPortable(window, storage).ok()));
    static_cast<void>(
        context.expectEq("hook queue reports external storage", Window::Types::Events::StorageKind::External, window.eventQueueInfo().storage));
    static_cast<void>(context.expectEq("native open rejects existing hook state", ErrorCode::AlreadyOpen, window.open({}).code));

    static_cast<void>(Window::TestHooks::enqueue(window, Window::Types::Events::ClientPositionChanged{{1, 2}}));
    static_cast<void>(Window::TestHooks::enqueue(window, Window::Types::Events::ClientPositionChanged{{3, 4}}));
    static_cast<void>(context.expectEq("compatible movement coalesces", std::size_t{1}, window.eventQueueInfo().pendingEvents));

    static_cast<void>(Window::TestHooks::enqueue(window, Window::Types::Events::FocusChanged{true}));
    static_cast<void>(Window::TestHooks::enqueue(window, Window::Types::Events::ClientPositionChanged{{5, 6}}));
    static_cast<void>(context.expectEq("noncoalescible event is a barrier", std::size_t{3}, window.eventQueueInfo().pendingEvents));

    static_cast<void>(Window::TestHooks::enqueue(window, Window::Types::Events::RedrawRequested{}));
    static_cast<void>(context.expectEq("full queue evicts oldest coalescible event", std::uint64_t{1}, window.eventQueueInfo().droppedEvents));

    Window::Types::Event first;
    Window::Types::Event second;
    Window::Types::Event third;
    static_cast<void>(context.expectTrue("first retained event pops", window.popEvent(first)));
    static_cast<void>(context.expectTrue("focus barrier remains", first.getIf<Window::Types::Events::FocusChanged>() != nullptr));
    static_cast<void>(context.expectTrue("second retained event pops", window.popEvent(second)));
    const auto *moved = second.getIf<Window::Types::Events::ClientPositionChanged>();
    static_cast<void>(context.expectTrue("post-barrier movement remains", moved != nullptr));
    if (moved != nullptr)
        static_cast<void>(context.expectEq("coalesced movement keeps latest payload", Window::Types::ScreenPosition{5, 6}, moved->position));
    static_cast<void>(context.expectTrue("third retained event pops", window.popEvent(third)));
    static_cast<void>(context.expectTrue("new noncoalescible event remains", third.getIf<Window::Types::Events::RedrawRequested>() != nullptr));
    static_cast<void>(context.expectTrue("retained sequences are increasing", first.sequence < second.sequence && second.sequence < third.sequence));

    window.clearDroppedEventCount();
    static_cast<void>(context.expectEq("drop count clears", std::uint64_t{0}, window.eventQueueInfo().droppedEvents));
    static_cast<void>(context.expectTrue("hook queue closes", window.close().ok()));

    std::array<Window::Types::Event, 1> payloadStorage;
    Window::Window payloadWindow;
    static_cast<void>(Window::TestHooks::openPortable(payloadWindow, payloadStorage));
    Window::Types::Events::FilesDropped dropped;
    dropped.paths.emplace_back("retained-until-close.txt");
    static_cast<void>(Window::TestHooks::enqueue(payloadWindow, std::move(dropped)));
    static_cast<void>(payloadWindow.close());
    static_cast<void>(context.expectTrue(
        "close releases payloads from borrowed event slots",
        payloadStorage[0].getIf<Window::Types::Events::FilesDropped>() == nullptr));
}

void testStickyClose(TestSupport::Context &context)
{
    std::array<Window::Types::Event, 4> storage;
    Window::Window source;
    static_cast<void>(Window::TestHooks::openPortable(source, storage));
    static_cast<void>(Window::TestHooks::requestClose(source, Window::Types::Events::CloseRequestSource::User));
    static_cast<void>(Window::TestHooks::requestClose(source, Window::Types::Events::CloseRequestSource::System));
    static_cast<void>(context.expectTrue("close request is sticky", source.hasCloseRequest()));
    static_cast<void>(context.expectEq("repeated close request emits once", std::size_t{1}, source.eventQueueInfo().pendingEvents));

    Window::Types::Event event;
    static_cast<void>(context.expectTrue("queue remains readable", source.popEvent(event)));
    const auto *close = event.getIf<Window::Types::Events::CloseRequested>();
    static_cast<void>(context.expectTrue("typed close payload remains", close != nullptr));
    if (close != nullptr)
        static_cast<void>(context.expectEq("first close source wins", Window::Types::Events::CloseRequestSource::User, close->source));
    static_cast<void>(source.close());
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
        consumeEventOfType<Window::Types::Events::ClientSizeChanged>(window)));

    window.clearEvents();
    static_cast<void>(window.setMode({.mode = Window::Types::Mode::BorderlessFullscreen, .monitor = window.currentMonitor()}));
    static_cast<void>(
        context.expectTrue("fullscreen transition translates to ModeChangedEvent", consumeEventOfType<Window::Types::Events::ModeChanged>(window)));
#if WINDOW_INTERNAL_TEST_HOOKS
    window.clearEvents();
    static_cast<void>(
        context.expectTrue("synthetic monitor removal recovery succeeds", Window::TestHooks::simulateFullscreenMonitorRemoval(window).ok()));
    static_cast<void>(context.expectEq("monitor removal recovers Windowed mode", Window::Types::Mode::Windowed, window.mode()));
    static_cast<void>(context.expectFalse("monitor removal clears fullscreen monitor", window.fullscreenInfo().monitor.isValid()));
    Window::Types::Event recoveryEvent;
    static_cast<void>(context.expectTrue("recovery queues display event first", window.popEvent(recoveryEvent)));
    static_cast<void>(context.expectTrue(
        "first recovery event is display configuration",
        recoveryEvent.getIf<Window::Types::Events::DisplayConfigurationChanged>() != nullptr));
    static_cast<void>(context.expectTrue("recovery queues mode event second", window.popEvent(recoveryEvent)));
    static_cast<void>(
        context.expectTrue("second recovery event is mode change", recoveryEvent.getIf<Window::Types::Events::ModeChanged>() != nullptr));
    window.clearEvents();
    static_cast<void>(window.setMode({.mode = Window::Types::Mode::BorderlessFullscreen, .monitor = window.currentMonitor()}));
#endif
    window.clearEvents();
    static_cast<void>(window.setMode({}));
    static_cast<void>(
        context.expectTrue("windowed restoration translates to ModeChangedEvent", consumeEventOfType<Window::Types::Events::ModeChanged>(window)));

    static_cast<void>(SendMessageW(handle.handle.window, WM_SHOWWINDOW, FALSE, 0));
    window.clearEvents();
    static_cast<void>(SendMessageW(handle.handle.window, WM_SHOWWINDOW, TRUE, 0));
    static_cast<void>(context.expectTrue("WM_SHOWWINDOW updates visibility cache", window.isVisible()));
    static_cast<void>(context.expectTrue(
        "WM_SHOWWINDOW translates to VisibilityChangedEvent",
        consumeEventOfType<Window::Types::Events::VisibilityChanged>(window)));
    window.clearEvents();
    static_cast<void>(SendMessageW(handle.handle.window, WM_SHOWWINDOW, FALSE, 0));
    static_cast<void>(context.expectFalse("synthetic hide restores visibility cache", window.isVisible()));
    static_cast<void>(consumeEventOfType<Window::Types::Events::VisibilityChanged>(window));

    window.clearEvents();
    static_cast<void>(SendMessageW(handle.handle.window, WM_SIZE, SIZE_MINIMIZED, MAKELPARAM(300, 210)));
    static_cast<void>(context.expectTrue("WM_SIZE minimize updates presentation cache", window.isMinimized()));
    static_cast<void>(context.expectTrue(
        "WM_SIZE translates to PresentationStateChangedEvent",
        consumeEventOfType<Window::Types::Events::PresentationStateChanged>(window)));
    window.clearEvents();
    static_cast<void>(SendMessageW(handle.handle.window, WM_SIZE, SIZE_RESTORED, MAKELPARAM(300, 210)));
    static_cast<void>(context.expectFalse("synthetic restore resets minimized cache", window.isMinimized()));
    static_cast<void>(consumeEventOfType<Window::Types::Events::PresentationStateChanged>(window));

    static_cast<void>(SendMessageW(handle.handle.window, WM_KILLFOCUS, 0, 0));
    window.clearEvents();
    static_cast<void>(SendMessageW(handle.handle.window, WM_SETFOCUS, 0, 0));
    static_cast<void>(context.expectTrue("WM_SETFOCUS updates cache", window.isFocused()));
    static_cast<void>(
        context.expectTrue("WM_SETFOCUS translates to FocusChangedEvent", consumeEventOfType<Window::Types::Events::FocusChanged>(window)));
    window.clearEvents();
    static_cast<void>(SendMessageW(handle.handle.window, WM_KILLFOCUS, 0, 0));
    static_cast<void>(context.expectFalse("WM_KILLFOCUS updates cache", window.isFocused()));
    static_cast<void>(
        context.expectTrue("WM_KILLFOCUS translates to FocusChangedEvent", consumeEventOfType<Window::Types::Events::FocusChanged>(window)));

    window.clearEvents();
    static_cast<void>(SendMessageW(handle.handle.window, WM_MOUSEMOVE, 0, MAKELPARAM(2, 2)));
    static_cast<void>(context.expectTrue("WM_MOUSEMOVE updates cursor-presence cache", window.isCursorInside()));
    static_cast<void>(context.expectTrue(
        "WM_MOUSEMOVE translates to CursorPresenceChangedEvent",
        consumeEventOfType<Window::Types::Events::CursorPresenceChanged>(window)));
    window.clearEvents();
    static_cast<void>(SendMessageW(handle.handle.window, WM_MOUSELEAVE, 0, 0));
    static_cast<void>(context.expectFalse("WM_MOUSELEAVE updates cursor-presence cache", window.isCursorInside()));
    static_cast<void>(context.expectTrue(
        "WM_MOUSELEAVE translates to CursorPresenceChangedEvent",
        consumeEventOfType<Window::Types::Events::CursorPresenceChanged>(window)));

    window.clearEvents();
    static_cast<void>(SendMessageW(handle.handle.window, WM_DISPLAYCHANGE, 32, MAKELPARAM(1920, 1080)));
    static_cast<void>(context.expectTrue(
        "WM_DISPLAYCHANGE translates to DisplayConfigurationChangedEvent",
        consumeEventOfType<Window::Types::Events::DisplayConfigurationChanged>(window)));

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
                context.expectTrue("WM_DROPFILES translates to FilesDroppedEvent", consumeEventOfType<Window::Types::Events::FilesDropped>(window)));
        }
        if (dropMemory != nullptr)
            static_cast<void>(GlobalFree(dropMemory));
    }

    window.clearEvents();
    static_cast<void>(SendMessageW(handle.handle.window, WM_PAINT, 0, 0));
    static_cast<void>(
        context.expectTrue("WM_PAINT translates to RedrawRequestedEvent", consumeEventOfType<Window::Types::Events::RedrawRequested>(window)));

    window.clearEvents();
    static_cast<void>(SendMessageW(handle.handle.window, WM_CLOSE, 0, 0));
    static_cast<void>(context.expectTrue("WM_CLOSE sets sticky close intent", window.hasCloseRequest()));
    static_cast<void>(
        context.expectTrue("WM_CLOSE translates to CloseRequestedEvent", consumeEventOfType<Window::Types::Events::CloseRequested>(window)));
    static_cast<void>(window.clearCloseRequest());
    static_cast<void>(context.expectTrue("native event fixture closes", window.close().ok()));
}
