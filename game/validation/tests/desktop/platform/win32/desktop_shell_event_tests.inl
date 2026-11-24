/// @file desktop_shell_event_tests.inl
/// @brief Shell queue and event callback tests.

/// ------------------------------------------------------------
/// Shell events and queues
/// ------------------------------------------------------------

void testTrayNativeCallbacks(TestSupport::Context &context)
{
    // Open a tray icon and locate its owner-thread callback window.
    const std::array<std::byte, 4> pixel{std::byte{0x11}, std::byte{0x22}, std::byte{0x33}, std::byte{0xFF}};
    const Desktop::Types::IconImageView icon{{1, 1}, pixel};
    Desktop::ShellEventQueue queue;

    if(!context.expectTrue("tray callback queue opens", queue.open(ShellTypes::QueueConfiguration::owned(16)).ok()))
    {
        return;
    }

    TrayTypes::Description description;
    description.icons = std::span{&icon, 1};
    description.tooltip = "GameWIP callback regression";
    Desktop::TrayIcon tray;

    if(!context.expectTrue("tray callback icon opens", tray.open(description, queue).ok()))
    {
        return;
    }
    const HWND window = FindWindowExW(nullptr, nullptr, L"GameWIP.Shell.TrayMessage", L"GameWIP tray");

    if(!context.expectTrue("tray owns a broadcast-capable hidden window", window != nullptr))
    {
        return;
    }

    DWORD process = 0;
    const DWORD thread = GetWindowThreadProcessId(window, &process);

    if(!context.expectTrue("tray callback target belongs to this owner", process == GetCurrentProcessId() && thread == GetCurrentThreadId()))
    {
        return;
    }
    constexpr UINT callback = WM_APP + 0x271;
    const auto verify = [&](UINT event, ShellTypes::Events::TrayActivationKind kind)
    {
        queue.clearEvents();
        SendMessageW(window, callback, MAKELPARAM(-120, 240), MAKELPARAM(event, 1));
        ShellTypes::Event received;
        const bool popped = queue.popEvent(received);
        const auto *activation = received.getIf<ShellTypes::Events::TrayActivated>();
        static_cast<void>(context.expectTrue(
            std::format("packed tray callback {} delivers exact activation and anchor", event),
            popped && activation != nullptr && activation->iconId == tray.id() && activation->kind == kind &&
                activation->screenPosition == std::optional{Desktop::Types::ScreenPosition{-120, 240}}));
        static_cast<void>(context.expectFalse("tray callback does not duplicate activation", queue.popEvent(received)));
    };

    // Check callback mappings and taskbar recreation.
    verify(NIN_SELECT, ShellTypes::Events::TrayActivationKind::Primary);
    verify(NIN_KEYSELECT, ShellTypes::Events::TrayActivationKind::Primary);
    verify(WM_LBUTTONDBLCLK, ShellTypes::Events::TrayActivationKind::DoublePrimary);
    verify(WM_CONTEXTMENU, ShellTypes::Events::TrayActivationKind::Secondary);

    NOTIFYICONDATAW deleted{};
    deleted.cbSize = sizeof(deleted);
    deleted.hWnd = window;
    deleted.uID = 1;
    static_cast<void>(context.expectTrue("recreation fixture removes only its own tray icon", Shell_NotifyIconW(NIM_DELETE, &deleted) != FALSE));
    SendMessageW(window, RegisterWindowMessageW(L"TaskbarCreated"), 0, 0);
    verify(NIN_KEYSELECT, ShellTypes::Events::TrayActivationKind::Primary);

    // Ignore version-4 button-up notifications.
    queue.clearEvents();
    SendMessageW(window, callback, 0, MAKELPARAM(WM_LBUTTONUP, 1));
    SendMessageW(window, callback, 0, MAKELPARAM(WM_RBUTTONUP, 1));
    ShellTypes::Event received;
    static_cast<void>(context.expectFalse("version 4 button-up does not duplicate select or context-menu", queue.popEvent(received)));
    static_cast<void>(context.expectFalse("tray message window is invisible", IsWindowVisible(window) != FALSE));
}

void testShellQueueAndCapabilities(TestSupport::Context &context)
{
    // Compare the capability snapshot with individual queries.
    const auto capabilities = Desktop::Shell::getCapabilities();

    if(!context.expectTrue("shell capability query succeeds", capabilities.status.ok()))
    {
        return;
    }

    for(std::uint8_t index = 0; index < static_cast<std::uint8_t>(ShellTypes::Capability::Count); ++index)
    {
        const auto capability = static_cast<ShellTypes::Capability>(index);
        static_cast<void>(context.expectEq(
            "Shell::supports agrees with capability snapshot",
            capabilities->supports(capability),
            Desktop::Shell::supports(capability)));
    }
    static_cast<void>(context.expectFalse("capability sentinel is never advertised", capabilities->supports(ShellTypes::Capability::Count)));

    // Verify internal queue validation, ownership, and overflow behavior.
    Desktop::ShellEventQueue queue;
    static_cast<void>(context.expectEq(
        "zero-capacity shell queue is rejected",
        ErrorCode::InvalidArgument,
        queue.open(ShellTypes::QueueConfiguration::owned(0)).code));

    if(!context.expectTrue("internal shell queue opens", queue.open(ShellTypes::QueueConfiguration::owned(2)).ok()))
    {
        return;
    }

    static_cast<void>(context.expectTrue("internal shell queue reports open", queue.isOpen()));
    static_cast<void>(context.expectTrue("internal shell queue binds to the opening thread", queue.ownedByCurrentThread()));
    static_cast<void>(context.expectEq("internal shell queue reports capacity", std::size_t{2}, queue.eventQueueInfo().capacity));
    static_cast<void>(context.expectEq(
        "reopening an open shell queue is rejected",
        ErrorCode::AlreadyOpen,
        queue.open(ShellTypes::QueueConfiguration::owned(2)).code));

#if DESKTOP_INTERNAL_TEST_HOOKS
    Desktop::Internal::TestHooks::enqueueShellEvent(
        queue,
        ShellTypes::Events::TrayActivated{{11}, ShellTypes::Events::TrayActivationKind::Primary, std::nullopt});
    Desktop::Internal::TestHooks::enqueueShellEvent(queue, ShellTypes::Events::TrayCommandInvoked{{11}, {7}});
    Desktop::Internal::TestHooks::enqueueShellEvent(
        queue,
        ShellTypes::Events::TrayActivated{{12}, ShellTypes::Events::TrayActivationKind::Secondary, std::nullopt});
    static_cast<void>(context.expectEq("full shell queue drops the newest event", std::uint64_t{1}, queue.eventQueueInfo().droppedEvents));

    std::array<ShellTypes::Event, 2> events;
    static_cast<void>(context.expectEq("shell queue pops its retained FIFO prefix", std::size_t{2}, queue.popEvents(events)));
    static_cast<void>(context.expectEq("first shell event sequence is one", std::uint64_t{1}, events[0].sequence));
    static_cast<void>(context.expectEq("second shell event sequence is two", std::uint64_t{2}, events[1].sequence));
    static_cast<void>(context.expectTrue("first shell event retains its payload", events[0].getIf<ShellTypes::Events::TrayActivated>() != nullptr));
    static_cast<void>(
        context.expectTrue("second shell event retains its payload", events[1].getIf<ShellTypes::Events::TrayCommandInvoked>() != nullptr));
    queue.clearDroppedEventCount();
    static_cast<void>(context.expectEq("shell queue drop count clears", std::uint64_t{0}, queue.eventQueueInfo().droppedEvents));
#endif

    // Enforce owner-thread access before reopening with borrowed storage.
    std::atomic<ErrorCode> foreignCloseCode{ErrorCode::Unknown};
    std::atomic_bool foreignPop = true;
    std::thread foreignThread(
        [&]
        {
            ShellTypes::Event event;
            foreignPop.store(queue.popEvent(event));
            foreignCloseCode.store(queue.close().code);
        });
    foreignThread.join();
    static_cast<void>(context.expectFalse("foreign shell thread cannot consume events", foreignPop.load()));
    static_cast<void>(context.expectEq("foreign shell thread cannot close the queue", ErrorCode::ResourceBusy, foreignCloseCode.load()));

    static_cast<void>(context.expectTrue("owner closes internal shell queue", queue.close().ok()));
    static_cast<void>(context.expectFalse("closed shell queue reports closed", queue.isOpen()));

    // Verify borrowed queue storage and sequence reset on reopen.
    std::array<ShellTypes::Event, 2> externalStorage;
    if(!context.expectTrue("external shell queue opens", queue.open(ShellTypes::QueueConfiguration::borrowed(std::span{externalStorage})).ok()))
    {
        return;
    }

    static_cast<void>(context.expectEq(
        "external shell queue reports borrowed storage",
        Desktop::Types::Events::StorageKind::External,
        queue.eventQueueInfo().storage));
#if DESKTOP_INTERNAL_TEST_HOOKS
    Desktop::Internal::TestHooks::enqueueShellEvent(queue, ShellTypes::Events::TrayCommandInvoked{{13}, {8}});
    ShellTypes::Event reopenedEvent;
    static_cast<void>(context.expectTrue("reopened shell queue accepts events", queue.popEvent(reopenedEvent)));
    static_cast<void>(context.expectEq("reopened shell queue restarts sequence numbers", std::uint64_t{1}, reopenedEvent.sequence));
#endif
    static_cast<void>(context.expectTrue("owner closes external shell queue", queue.close().ok()));
}

void testShellPumpingWithoutWindow(TestSupport::Context &context)
{
    // A fresh owner thread has no Window or ProgressDialog that could keep the pump active.
    std::thread ownerThread(
        [&]
        {
            constexpr UINT markerMessage = WM_APP + 0x377;
            const auto checkPump = [&](bool wait)
            {
                static_cast<void>(context.expectTrue(
                    "shell-only owner accepts a native message",
                    PostThreadMessageW(GetCurrentThreadId(), markerMessage, 0, 0) != FALSE));
                const auto result = wait ? Desktop::Events::wait(std::chrono::milliseconds{10}) : Desktop::Events::poll();
                static_cast<void>(context.expectTrue("shell-only event pumping succeeds", result.status.ok()));
                MSG pending{};
                static_cast<void>(context.expectFalse(
                    "shell-only pump consumes pending native messages",
                    PeekMessageW(&pending, nullptr, markerMessage, markerMessage, PM_REMOVE) != FALSE));
            };

            Desktop::NotificationCenter notifications;

            if(!notifications.open().ok())
            {
                context.fail("shell-only notification center", "the native message window could not be opened");
                return;
            }

            checkPump(false);
            checkPump(true);

            // Keep the native pump active until the final shell owner closes.
            Desktop::NotificationCenter secondNotifications;

            if(!context.expectTrue("second shell-only center opens", secondNotifications.open().ok()))
            {
                static_cast<void>(notifications.close());
                return;
            }

            static_cast<void>(context.expectTrue("first shell-only center closes", notifications.close().ok()));
            checkPump(false);
            static_cast<void>(context.expectTrue("last shell-only center closes", secondNotifications.close().ok()));

            const std::array<std::byte, 4> pixel{std::byte{0x11}, std::byte{0x22}, std::byte{0x33}, std::byte{0xFF}};
            const Desktop::Types::IconImageView icon{{1, 1}, pixel};
            TrayTypes::Description description;
            description.icons = std::span{&icon, 1};
            Desktop::TrayIcon tray;
            const auto opened = tray.open(description);

            // Confirm a tray icon also keeps shell-only pumping active.
            if(opened.ok())
            {
                checkPump(false);
                checkPump(true);
                static_cast<void>(context.expectTrue("shell-only tray closes", tray.close().ok()));
            }
            else
            {
                context.skip("shell-only tray pumping", std::format("native tray publication returned {}", IO::errorCodeName(opened.code)));
            }
            static_cast<void>(context.expectTrue(
                "closed shell owner accepts a native marker",
                PostThreadMessageW(GetCurrentThreadId(), markerMessage, 0, 0) != FALSE));
            static_cast<void>(context.expectTrue("pump without native resources succeeds", Desktop::Events::poll().status.ok()));
            MSG pending{};
            static_cast<void>(context.expectTrue(
                "closing the last shell resource restores the no-op pump",
                PeekMessageW(&pending, nullptr, markerMessage, markerMessage, PM_REMOVE) != FALSE));
        });
    ownerThread.join();
}
