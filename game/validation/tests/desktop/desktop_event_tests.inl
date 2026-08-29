/// @file desktop_event_tests.inl
/// @brief Window event-queue and dispatch validation cases.

#if DESKTOP_INTERNAL_TEST_HOOKS
void testFixedEventQueue(TestSupport::Context &context)
{
    std::array<Desktop::Types::Event, 3> storage;
    Desktop::Window window;
    static_cast<void>(context.expectTrue("portable queue hook opens", Desktop::TestHooks::openPortable(window, storage).ok()));
    static_cast<void>(
        context.expectEq("hook queue reports external storage", Desktop::Types::Events::StorageKind::External, window.eventQueueInfo().storage));
    static_cast<void>(context.expectEq("native open rejects existing hook state", ErrorCode::AlreadyOpen, window.open({}).code));

    static_cast<void>(Desktop::TestHooks::enqueue(window, Desktop::Types::Events::ClientPositionChanged{{1, 2}}));
    static_cast<void>(Desktop::TestHooks::enqueue(window, Desktop::Types::Events::ClientPositionChanged{{3, 4}}));
    static_cast<void>(context.expectEq("compatible movement coalesces", std::size_t{1}, window.eventQueueInfo().pendingEvents));

    static_cast<void>(Desktop::TestHooks::enqueue(window, Desktop::Types::Events::FocusChanged{true}));
    static_cast<void>(Desktop::TestHooks::enqueue(window, Desktop::Types::Events::ClientPositionChanged{{5, 6}}));
    static_cast<void>(context.expectEq("noncoalescible event is a barrier", std::size_t{3}, window.eventQueueInfo().pendingEvents));

    static_cast<void>(Desktop::TestHooks::enqueue(window, Desktop::Types::Events::RedrawRequested{}));
    static_cast<void>(context.expectEq("full queue evicts oldest coalescible event", std::uint64_t{1}, window.eventQueueInfo().droppedEvents));

    Desktop::Types::Event first;
    Desktop::Types::Event second;
    Desktop::Types::Event third;
    static_cast<void>(context.expectTrue("first retained event pops", window.popEvent(first)));
    static_cast<void>(context.expectTrue("focus barrier remains", first.getIf<Desktop::Types::Events::FocusChanged>() != nullptr));
    static_cast<void>(context.expectTrue("second retained event pops", window.popEvent(second)));
    const auto *moved = second.getIf<Desktop::Types::Events::ClientPositionChanged>();
    static_cast<void>(context.expectTrue("post-barrier movement remains", moved != nullptr));
    if (moved != nullptr)
        static_cast<void>(context.expectEq("coalesced movement keeps latest payload", Desktop::Types::ScreenPosition{5, 6}, moved->position));
    static_cast<void>(context.expectTrue("third retained event pops", window.popEvent(third)));
    static_cast<void>(context.expectTrue("new noncoalescible event remains", third.getIf<Desktop::Types::Events::RedrawRequested>() != nullptr));
    static_cast<void>(context.expectTrue("retained sequences are increasing", first.sequence < second.sequence && second.sequence < third.sequence));

    window.clearDroppedEventCount();
    static_cast<void>(context.expectEq("drop count clears", std::uint64_t{0}, window.eventQueueInfo().droppedEvents));
    static_cast<void>(context.expectTrue("hook queue closes", window.close().ok()));

    std::array<Desktop::Types::Event, 1> payloadStorage;
    Desktop::Window payloadWindow;
    static_cast<void>(Desktop::TestHooks::openPortable(payloadWindow, payloadStorage));
    Desktop::Types::Events::FilesDropped dropped;
    dropped.paths.emplace_back("retained-until-close.txt");
    static_cast<void>(Desktop::TestHooks::enqueue(payloadWindow, std::move(dropped)));
    Desktop::Types::Event removedPayload;
    static_cast<void>(payloadWindow.popEvent(removedPayload));
    static_cast<void>(context.expectEq("pop clears the vacated borrowed event slot", std::uint64_t{0}, payloadStorage[0].sequence));
    static_cast<void>(Desktop::TestHooks::enqueue(payloadWindow, Desktop::Types::Events::FilesDropped{}));
    static_cast<void>(payloadWindow.close());
    static_cast<void>(context.expectTrue(
        "close releases payloads from borrowed event slots",
        payloadStorage[0].getIf<Desktop::Types::Events::FilesDropped>() == nullptr));
}

void testStickyClose(TestSupport::Context &context)
{
    std::array<Desktop::Types::Event, 4> storage;
    Desktop::Window source;
    static_cast<void>(Desktop::TestHooks::openPortable(source, storage));
    static_cast<void>(Desktop::TestHooks::requestClose(source, Desktop::Types::Events::CloseRequestSource::User));
    static_cast<void>(Desktop::TestHooks::requestClose(source, Desktop::Types::Events::CloseRequestSource::System));
    static_cast<void>(context.expectTrue("close request is sticky", source.hasCloseRequest()));
    static_cast<void>(context.expectEq("repeated close request emits once", std::size_t{1}, source.eventQueueInfo().pendingEvents));

    Desktop::Types::Event event;
    static_cast<void>(context.expectTrue("queue remains readable", source.popEvent(event)));
    const auto *close = event.getIf<Desktop::Types::Events::CloseRequested>();
    static_cast<void>(context.expectTrue("typed close payload remains", close != nullptr));
    if (close != nullptr)
        static_cast<void>(context.expectEq("first close source wins", Desktop::Types::Events::CloseRequestSource::User, close->source));
    static_cast<void>(source.close());
}

#endif

void testNativeEventTranslation(TestSupport::Context &context)
{
    Desktop::Types::Description description;
    description.title = "Window native event validation";
    description.clientSize = {260, 170};
    description.visible = false;
    description.fileDropEnabled = true;

    Desktop::Window window;
    static_cast<void>(context.expectTrue("native event fixture opens", window.open(description, 64).ok()));
    if (!window.isOpen())
        return;

    const Desktop::Native::Win32::HandleResult handle = Desktop::Native::Win32::getHandle(window);
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
        consumeEventOfType<Desktop::Types::Events::ClientSizeChanged>(window)));

    window.clearEvents();
    static_cast<void>(window.setMode({.mode = Desktop::Types::Mode::BorderlessFullscreen, .monitor = window.currentMonitor()}));
    static_cast<void>(
        context.expectTrue("fullscreen transition translates to ModeChangedEvent", consumeEventOfType<Desktop::Types::Events::ModeChanged>(window)));
#if DESKTOP_INTERNAL_TEST_HOOKS
    window.clearEvents();
    static_cast<void>(
        context.expectTrue("synthetic monitor removal recovery succeeds", Desktop::TestHooks::simulateFullscreenMonitorRemoval(window).ok()));
    static_cast<void>(context.expectEq("monitor removal recovers Windowed mode", Desktop::Types::Mode::Windowed, window.mode()));
    static_cast<void>(context.expectFalse("monitor removal clears fullscreen monitor", window.fullscreenInfo().monitor.isValid()));
    Desktop::Types::Event recoveryEvent;
    static_cast<void>(context.expectTrue("recovery queues display event first", window.popEvent(recoveryEvent)));
    static_cast<void>(context.expectTrue(
        "first recovery event is display configuration",
        recoveryEvent.getIf<Desktop::Types::Events::DisplayConfigurationChanged>() != nullptr));
    static_cast<void>(context.expectTrue("recovery queues mode event second", window.popEvent(recoveryEvent)));
    static_cast<void>(
        context.expectTrue("second recovery event is mode change", recoveryEvent.getIf<Desktop::Types::Events::ModeChanged>() != nullptr));
    window.clearEvents();
    static_cast<void>(window.setMode({.mode = Desktop::Types::Mode::BorderlessFullscreen, .monitor = window.currentMonitor()}));
#endif
    window.clearEvents();
    static_cast<void>(window.setMode({}));
    static_cast<void>(
        context.expectTrue("windowed restoration translates to ModeChangedEvent", consumeEventOfType<Desktop::Types::Events::ModeChanged>(window)));

    static_cast<void>(SendMessageW(handle.handle.window, WM_SHOWWINDOW, FALSE, 0));
    window.clearEvents();
    static_cast<void>(SendMessageW(handle.handle.window, WM_SHOWWINDOW, TRUE, 0));
    static_cast<void>(context.expectTrue("WM_SHOWWINDOW updates visibility cache", window.visible()));
    static_cast<void>(context.expectTrue(
        "WM_SHOWWINDOW translates to VisibilityChangedEvent",
        consumeEventOfType<Desktop::Types::Events::VisibilityChanged>(window)));
    window.clearEvents();
    static_cast<void>(SendMessageW(handle.handle.window, WM_SHOWWINDOW, FALSE, 0));
    static_cast<void>(context.expectFalse("synthetic hide restores visibility cache", window.visible()));
    static_cast<void>(consumeEventOfType<Desktop::Types::Events::VisibilityChanged>(window));

    window.clearEvents();
    static_cast<void>(SendMessageW(handle.handle.window, WM_SIZE, SIZE_MINIMIZED, MAKELPARAM(300, 210)));
    static_cast<void>(context.expectTrue("WM_SIZE minimize updates presentation cache", window.minimized()));
    static_cast<void>(context.expectTrue(
        "WM_SIZE translates to PresentationStateChangedEvent",
        consumeEventOfType<Desktop::Types::Events::PresentationStateChanged>(window)));
    window.clearEvents();
    static_cast<void>(SendMessageW(handle.handle.window, WM_SIZE, SIZE_RESTORED, MAKELPARAM(300, 210)));
    static_cast<void>(context.expectFalse("synthetic restore resets minimized cache", window.minimized()));
    static_cast<void>(consumeEventOfType<Desktop::Types::Events::PresentationStateChanged>(window));

    static_cast<void>(SendMessageW(handle.handle.window, WM_KILLFOCUS, 0, 0));
    window.clearEvents();
    static_cast<void>(SendMessageW(handle.handle.window, WM_SETFOCUS, 0, 0));
    static_cast<void>(context.expectTrue("WM_SETFOCUS updates cache", window.focused()));
    static_cast<void>(
        context.expectTrue("WM_SETFOCUS translates to FocusChangedEvent", consumeEventOfType<Desktop::Types::Events::FocusChanged>(window)));
    window.clearEvents();
    static_cast<void>(SendMessageW(handle.handle.window, WM_KILLFOCUS, 0, 0));
    static_cast<void>(context.expectFalse("WM_KILLFOCUS updates cache", window.focused()));
    static_cast<void>(
        context.expectTrue("WM_KILLFOCUS translates to FocusChangedEvent", consumeEventOfType<Desktop::Types::Events::FocusChanged>(window)));

    window.clearEvents();
    static_cast<void>(SendMessageW(handle.handle.window, WM_MOUSEMOVE, 0, MAKELPARAM(2, 2)));
    static_cast<void>(context.expectTrue("WM_MOUSEMOVE updates cursor-presence cache", window.cursorInside()));
    static_cast<void>(context.expectTrue(
        "WM_MOUSEMOVE translates to CursorPresenceChangedEvent",
        consumeEventOfType<Desktop::Types::Events::CursorPresenceChanged>(window)));
    window.clearEvents();
    static_cast<void>(SendMessageW(handle.handle.window, WM_MOUSELEAVE, 0, 0));
    static_cast<void>(context.expectFalse("WM_MOUSELEAVE updates cursor-presence cache", window.cursorInside()));
    static_cast<void>(context.expectTrue(
        "WM_MOUSELEAVE translates to CursorPresenceChangedEvent",
        consumeEventOfType<Desktop::Types::Events::CursorPresenceChanged>(window)));

    window.clearEvents();
    static_cast<void>(SendMessageW(handle.handle.window, WM_DISPLAYCHANGE, 32, MAKELPARAM(1920, 1080)));
    static_cast<void>(context.expectTrue(
        "WM_DISPLAYCHANGE translates to DisplayConfigurationChangedEvent",
        consumeEventOfType<Desktop::Types::Events::DisplayConfigurationChanged>(window)));

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
            // GlobalAlloc owns dropBytes and the payload intentionally models DROPFILES' variable trailing array.
#if defined(__clang__)
#pragma clang unsafe_buffer_usage begin
#endif
            auto *path = reinterpret_cast<wchar_t *>(reinterpret_cast<std::byte *>(drop) + sizeof(DROPFILES));
            std::copy(droppedPath.begin(), droppedPath.end(), path);
            path[droppedPath.size()] = wchar_t{};
            path[droppedPath.size() + 1] = wchar_t{};
#if defined(__clang__)
#pragma clang unsafe_buffer_usage end
#endif
            static_cast<void>(GlobalUnlock(dropMemory));
            static_cast<void>(SendMessageW(handle.handle.window, WM_DROPFILES, reinterpret_cast<WPARAM>(dropMemory), 0));
            dropMemory = nullptr;
            static_cast<void>(
                context.expectTrue("WM_DROPFILES translates to FilesDroppedEvent", consumeEventOfType<Desktop::Types::Events::FilesDropped>(window)));
        }
        if (dropMemory != nullptr)
            static_cast<void>(GlobalFree(dropMemory));
    }

    window.clearEvents();
    static_cast<void>(SendMessageW(handle.handle.window, WM_PAINT, 0, 0));
    static_cast<void>(
        context.expectTrue("WM_PAINT translates to RedrawRequestedEvent", consumeEventOfType<Desktop::Types::Events::RedrawRequested>(window)));

    window.clearEvents();
    static_cast<void>(SendMessageW(handle.handle.window, WM_CLOSE, 0, 0));
    static_cast<void>(context.expectTrue("WM_CLOSE sets sticky close intent", window.hasCloseRequest()));
    static_cast<void>(
        context.expectTrue("WM_CLOSE translates to CloseRequestedEvent", consumeEventOfType<Desktop::Types::Events::CloseRequested>(window)));
    static_cast<void>(window.clearCloseRequest());
    static_cast<void>(context.expectTrue("native event fixture closes", window.close().ok()));
}
