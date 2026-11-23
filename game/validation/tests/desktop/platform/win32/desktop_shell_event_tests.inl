/// @file desktop_shell_event_tests.inl
/// @brief Win32 shell queue, event, and lifetime tests.

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
    static_cast<void>(context.expectTrue("shell capability query succeeds", capabilities.status.ok()));

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
    static_cast<void>(context.expectTrue("internal shell queue opens", queue.open(ShellTypes::QueueConfiguration::owned(2)).ok()));
    static_cast<void>(context.expectTrue("internal shell queue reports open", queue.isOpen()));
    static_cast<void>(context.expectTrue("internal shell queue binds to the opening thread", queue.ownedByCurrentThread()));
    static_cast<void>(context.expectEq("internal shell queue reports capacity", std::size_t{2}, queue.eventQueueInfo().capacity));
    static_cast<void>(context.expectEq(
        "reopening an open shell queue is rejected",
        ErrorCode::AlreadyOpen,
        queue.open(ShellTypes::QueueConfiguration::owned(2)).code));

#if DESKTOP_INTERNAL_TEST_HOOKS
    Desktop::TestHooks::enqueueShellEvent(
        queue,
        ShellTypes::Events::TrayActivated{{11}, ShellTypes::Events::TrayActivationKind::Primary, std::nullopt});
    Desktop::TestHooks::enqueueShellEvent(queue, ShellTypes::Events::TrayCommandInvoked{{11}, {7}});
    Desktop::TestHooks::enqueueShellEvent(
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
    static_cast<void>(
        context.expectTrue("external shell queue opens", queue.open(ShellTypes::QueueConfiguration::borrowed(std::span{externalStorage})).ok()));
    static_cast<void>(context.expectEq(
        "external shell queue reports borrowed storage",
        Desktop::Types::Events::StorageKind::External,
        queue.eventQueueInfo().storage));
#if DESKTOP_INTERNAL_TEST_HOOKS
    Desktop::TestHooks::enqueueShellEvent(queue, ShellTypes::Events::TrayCommandInvoked{{13}, {8}});
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

            Desktop::NotificationCenter secondNotifications;
            static_cast<void>(context.expectTrue("second shell-only center opens", secondNotifications.open().ok()));
            static_cast<void>(context.expectTrue("first shell-only center closes", notifications.close().ok()));
            checkPump(false);
            static_cast<void>(context.expectTrue("last shell-only center closes", secondNotifications.close().ok()));

            const std::array<std::byte, 4> pixel{std::byte{0x11}, std::byte{0x22}, std::byte{0x33}, std::byte{0xFF}};
            const Desktop::Types::IconImageView icon{{1, 1}, pixel};
            TrayTypes::Description description;
            description.icons = std::span{&icon, 1};
            Desktop::TrayIcon tray;
            const auto opened = tray.open(description);

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

/// ------------------------------------------------------------
/// Shell events, validation, and lifetimes
/// ------------------------------------------------------------

void testShellValidation(TestSupport::Context &context)
{
    // Validate closed-object calls and malformed taskbar and tray descriptions.
    const std::array<std::byte, 4> pixel{std::byte{0x11}, std::byte{0x22}, std::byte{0x33}, std::byte{0xFF}};
    const Desktop::Types::IconImageView icon{{1, 1}, pixel};
    const std::array<std::byte, 3> invalidPixelBytes{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
    const Desktop::Types::IconImageView invalidIcon{{1, 1}, invalidPixelBytes};

    Desktop::Window closedWindow;
    Desktop::TaskbarItem taskbar;
    static_cast<void>(context.expectEq("taskbar open rejects a closed Window", ErrorCode::NotOpen, taskbar.open(closedWindow, {}).code));
    static_cast<void>(context.expectEq("closed taskbar progress update is rejected", ErrorCode::NotOpen, taskbar.setProgress({}).code));

    Desktop::Types::Description windowDescription;
    windowDescription.title = "Shell validation Window";
    windowDescription.windowed.clientSize = {160, 100};
    windowDescription.activation.visible = false;
    Desktop::Window window;

    if(!window.open(windowDescription, Desktop::Events::owned(8)).ok())
    {
        context.fail("shell validation Window", "the hidden native validation Window could not be opened");
        return;
    }

    TaskbarTypes::Description invalidTaskbarDescription;
    invalidTaskbarDescription.overlayIcon = invalidIcon;
    static_cast<void>(context.expectEq(
        "taskbar rejects malformed icon storage before native publication",
        ErrorCode::InvalidArgument,
        taskbar.open(window, invalidTaskbarDescription).code));

    TaskbarTypes::ThumbnailButton button{{1}, "Open"};
    TaskbarTypes::Description buttonDescription;
    buttonDescription.thumbnailButtons = std::span{&button, 1};
    static_cast<void>(context.expectEq(
        "taskbar rejects thumbnail buttons without the queue overload",
        ErrorCode::InvalidArgument,
        taskbar.open(window, buttonDescription).code));

    Desktop::TrayIcon tray;
    static_cast<void>(context.expectEq("tray rejects an empty icon set", ErrorCode::InvalidArgument, tray.open({}).code));
    TrayTypes::Description malformedTrayDescription;
    malformedTrayDescription.icons = std::span{&icon, 1};
    malformedTrayDescription.tooltip = std::string_view{"bad\0tooltip", 11};
    static_cast<void>(
        context.expectEq("tray rejects embedded NUL tooltip text", ErrorCode::InvalidArgument, tray.open(malformedTrayDescription).code));

    // Validate Win32 command identities and recursive tray menus.
    Desktop::ShellEventQueue queue;
    static_cast<void>(queue.open(ShellTypes::QueueConfiguration::owned(8)));
    TaskbarTypes::ThumbnailButton oversizedTaskbarButton{{0x10000U}, "Too-large taskbar command"};
    TaskbarTypes::Description oversizedTaskbarDescription;
    oversizedTaskbarDescription.thumbnailButtons = std::span{&oversizedTaskbarButton, 1};
    static_cast<void>(context.expectEq(
        "taskbar rejects command identities that cannot round-trip through Win32 messages",
        ErrorCode::InvalidArgument,
        taskbar.open(window, oversizedTaskbarDescription, queue).code));
    TrayTypes::MenuItem malformedMenuItem;
    malformedMenuItem.kind = TrayTypes::MenuItemKind::Command;
    malformedMenuItem.label = "Missing command identity";
    TrayTypes::Description malformedMenuDescription;
    malformedMenuDescription.icons = std::span{&icon, 1};
    malformedMenuDescription.menu.items = std::span{&malformedMenuItem, 1};
    static_cast<void>(context.expectEq(
        "tray rejects malformed recursive menu entries before native publication",
        ErrorCode::InvalidArgument,
        tray.open(malformedMenuDescription, queue).code));
    static_cast<void>(queue.close());

    // Validate notifications, Jump Lists, and registration inputs.
    Desktop::NotificationCenter notifications;
    static_cast<void>(context.expectEq("closed notification publication is rejected", ErrorCode::NotOpen, notifications.publish({}).status.code));

    const std::filesystem::path executable = L"C:\\Windows\\System32\\notepad.exe";
    const std::array<ShellTypes::LaunchArgumentView, 1> placeholders{ShellTypes::LaunchPlaceholder::SelectedPath};
    const ShellTypes::LaunchActionView jumpAction{ShellTypes::PathTargetView{std::cref(executable)}, placeholders};
    const JumpListTypes::Task jumpTask{"Open selected path", {}, jumpAction};
    const std::array<JumpListTypes::Task, 1> jumpTasks{jumpTask};
    JumpListTypes::Category emptyCategory{"Empty", {}};
    JumpListTypes::Description invalidCategoryDescription;
    invalidCategoryDescription.categories = std::span{&emptyCategory, 1};
    static_cast<void>(context.expectEq(
        "jump-list categories require at least one task",
        ErrorCode::InvalidArgument,
        Desktop::JumpLists::publish(invalidCategoryDescription).code));

    JumpListTypes::Description placeholderDescription;
    placeholderDescription.tasks = jumpTasks;
    static_cast<void>(context.expectEq(
        "jump-list tasks reject target placeholders",
        ErrorCode::InvalidArgument,
        Desktop::JumpLists::publish(placeholderDescription).code));

    const std::array<ShellTypes::TargetView, 1> invalidRecentItems{ShellTypes::TargetView{std::monostate{}}};
    JumpListTypes::Description invalidRecentDescription;
    invalidRecentDescription.recentItems = invalidRecentItems;
    static_cast<void>(context.expectEq(
        "jump-list recent items reject an empty target",
        ErrorCode::InvalidArgument,
        Desktop::JumpLists::publish(invalidRecentDescription).code));

    RegistrationTypes::FileExtensionDescription invalidRegistration;
    invalidRegistration.ownerKey = "GameWIP.Validation.Shell";
    invalidRegistration.displayName = "Validation shell registration";
    static_cast<void>(context.expectEq(
        "file registration requires an extension",
        ErrorCode::InvalidArgument,
        Desktop::Registration::registerFileExtension(invalidRegistration).status.code));
    static_cast<void>(context.expectEq(
        "unregister rejects an empty owner key",
        ErrorCode::InvalidArgument,
        Desktop::Registration::unregisterFileExtension({}, ".gamewip-validation").status.code));

    static_cast<void>(context.expectTrue("shell validation Window closes", window.close().ok()));
}

void testShellNativeLifetimes(TestSupport::Context &context)
{
    const std::array<std::byte, 4> pixel{std::byte{0x11}, std::byte{0x22}, std::byte{0x33}, std::byte{0xFF}};
    const Desktop::Types::IconImageView icon{{1, 1}, pixel};

    Desktop::Types::Description windowDescription;
    windowDescription.title = "Shell native lifetime Window";
    windowDescription.windowed.clientSize = {160, 100};
    windowDescription.activation.visible = false;
    Desktop::Window window;

    if(!window.open(windowDescription, Desktop::Events::owned(8)).ok())
    {
        context.skip("shell native lifetimes", "the hidden native validation Window could not be opened");
        return;
    }

    // Bind taskbar state to the Window lifetime.
    Desktop::TaskbarItem taskbar;
    const Desktop::Types::Shell::Progress progress{Desktop::Types::Shell::ProgressState::Normal, 0.5};
    TaskbarTypes::Description taskbarDescription;
    taskbarDescription.progress = progress;
    const IO::Types::Status taskbarStatus = taskbar.open(window, taskbarDescription);

    if(!taskbarStatus.ok())
    {
        context.skip(
            "taskbar lifetime and one-binding rule",
            std::format("native taskbar publication returned {}", IO::errorCodeName(taskbarStatus.code)));
    }
    else
    {
        static_cast<void>(context.expectTrue("taskbar binding reports open", taskbar.isOpen()));
        static_cast<void>(context.expectEq("taskbar binding retains Window identity", window.id(), taskbar.windowId()));
        Desktop::TaskbarItem secondTaskbar;
        static_cast<void>(
            context.expectEq("one taskbar binding is allowed per Window lifetime", ErrorCode::AlreadyExists, secondTaskbar.open(window, {}).code));
        static_cast<void>(context.expectEq(
            "taskbar rejects a non-normalized progress fraction",
            ErrorCode::InvalidArgument,
            taskbar.setProgress(Desktop::Types::Shell::Progress{Desktop::Types::Shell::ProgressState::Normal, 2.0}).code));
        static_cast<void>(context.expectTrue("taskbar binding closes", taskbar.close().ok()));
    }

    // Verify taskbar queue ownership and later button updates.
    Desktop::ShellEventQueue taskbarQueue;
    static_cast<void>(taskbarQueue.open(ShellTypes::QueueConfiguration::owned(8)));
    Desktop::TaskbarItem queuedTaskbar;
    const IO::Types::Status queuedTaskbarStatus = queuedTaskbar.open(window, {}, taskbarQueue);

    if(!queuedTaskbarStatus.ok())
    {
        context.skip(
            "empty taskbar queue binding",
            std::format("native taskbar publication returned {}", IO::errorCodeName(queuedTaskbarStatus.code)));
    }
    else
    {
        static_cast<void>(context.expectTrue("empty taskbar queue overload retains its queue", queuedTaskbar.hasEventQueue()));
        TaskbarTypes::ThumbnailButton queuedTaskbarButton{{703}, "Queued taskbar command"};
        static_cast<void>(context.expectTrue(
            "empty taskbar queue binding permits later thumbnail buttons",
            queuedTaskbar.setThumbnailButtons(std::span{&queuedTaskbarButton, 1}).ok()));
        static_cast<void>(context.expectEq("bound taskbar queue cannot close early", ErrorCode::ResourceBusy, taskbarQueue.close().code));
        static_cast<void>(context.expectTrue("queued taskbar binding closes", queuedTaskbar.close().ok()));
    }
    static_cast<void>(taskbarQueue.close());

    // Verify tray queue ownership and later menu updates.
    Desktop::ShellEventQueue trayQueue;
    static_cast<void>(trayQueue.open(ShellTypes::QueueConfiguration::owned(8)));
    TrayTypes::Description trayDescription;
    trayDescription.icons = std::span{&icon, 1};
    TrayTypes::MenuItem trayMenuItem;
    trayMenuItem.kind = TrayTypes::MenuItemKind::Command;
    trayMenuItem.commandId = {1};
    trayMenuItem.label = "Validation command";
    trayDescription.menu.items = std::span{&trayMenuItem, 1};
    Desktop::TrayIcon tray;
    const IO::Types::Status trayStatus = tray.open(trayDescription, trayQueue);

    if(!trayStatus.ok())
    {
        context.skip("tray native lifetime", std::format("native tray publication returned {}", IO::errorCodeName(trayStatus.code)));
    }
    else
    {
        static_cast<void>(context.expectTrue("tray binding reports open", tray.isOpen()));
        static_cast<void>(context.expectTrue("tray binding generates a valid identity", tray.id().isValid()));
        static_cast<void>(context.expectTrue("tray binding retains its event queue", tray.hasEventQueue()));
        static_cast<void>(context.expectEq("bound tray queue cannot close early", ErrorCode::ResourceBusy, trayQueue.close().code));
        static_cast<void>(context.expectTrue("tray binding closes", tray.close().ok()));
    }
    static_cast<void>(trayQueue.close());

    static_cast<void>(trayQueue.open(ShellTypes::QueueConfiguration::owned(8)));
    TrayTypes::Description queuedEmptyTrayDescription;
    queuedEmptyTrayDescription.icons = std::span{&icon, 1};
    queuedEmptyTrayDescription.tooltip = "Queued empty tray menu";
    Desktop::TrayIcon queuedEmptyTray;
    const IO::Types::Status queuedEmptyTrayStatus = queuedEmptyTray.open(queuedEmptyTrayDescription, trayQueue);

    if(!queuedEmptyTrayStatus.ok())
    {
        context.skip("empty tray queue binding", std::format("native tray publication returned {}", IO::errorCodeName(queuedEmptyTrayStatus.code)));
    }
    else
    {
        static_cast<void>(context.expectTrue("empty tray queue overload retains its queue", queuedEmptyTray.hasEventQueue()));
        static_cast<void>(context.expectTrue(
            "empty tray queue binding permits later menus",
            queuedEmptyTray.setMenu(TrayTypes::MenuDescription{std::span{&trayMenuItem, 1}}).ok()));
        static_cast<void>(context.expectEq("bound empty tray queue cannot close early", ErrorCode::ResourceBusy, trayQueue.close().code));
        static_cast<void>(context.expectTrue("queued empty tray binding closes", queuedEmptyTray.close().ok()));
    }
    static_cast<void>(trayQueue.close());

    // Verify notification queue ownership and item lifecycle.
    Desktop::NotificationCenter notifications;
    Desktop::ShellEventQueue notificationQueue;
    static_cast<void>(notificationQueue.open(ShellTypes::QueueConfiguration::owned(8)));
    const IO::Types::Status notificationStatus = notifications.open(notificationQueue);

    if(!notificationStatus.ok())
    {
        context.skip("notification native lifetime", std::format("notification center returned {}", IO::errorCodeName(notificationStatus.code)));
    }
    else
    {
        static_cast<void>(context.expectTrue("notification center reports open", notifications.isOpen()));
        static_cast<void>(context.expectTrue("notification center retains its event queue", notifications.hasEventQueue()));
        static_cast<void>(context.expectEq("bound notification queue cannot close early", ErrorCode::ResourceBusy, notificationQueue.close().code));

        NotificationTypes::Description basicDescription;
        basicDescription.title = "GameWIP shell validation";
        basicDescription.body = "The notification lifetime test is active.";
        const auto published = notifications.publish(basicDescription);

        if(!published.status.ok())
        {
            context.skip("basic notification publication", std::format("notification service returned {}", IO::errorCodeName(published.status.code)));
        }
        else
        {
            static_cast<void>(context.expectTrue("basic notification generates an identity", published->isValid()));
            static_cast<void>(context.expectTrue("basic notification update succeeds", notifications.update(*published, basicDescription).ok()));
            static_cast<void>(context.expectTrue("basic notification dismissal succeeds", notifications.dismiss(*published).ok()));
            static_cast<void>(context.expectEq(
                "dismissed notification identity is no longer accepted",
                ErrorCode::NotFound,
                notifications.dismiss(*published).code));
        }
        static_cast<void>(context.expectTrue("notification center closes", notifications.close().ok()));
    }
    static_cast<void>(notificationQueue.close());
    static_cast<void>(context.expectTrue("shell native lifetime Window closes", window.close().ok()));
}

#if DESKTOP_INTERNAL_TEST_HOOKS
void testShellFailureHooks(TestSupport::Context &context)
{
    Desktop::TestHooks::resetFailures();
    Desktop::NotificationCenter notifications;
    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::ShellNativeOpen);
    static_cast<void>(context.expectEq("shell native-open failure is deterministic", ErrorCode::NativeFailure, notifications.open().code));

    Desktop::TestHooks::resetFailures();
    static_cast<void>(context.expectTrue("notification center opens after injected failure", notifications.open().ok()));
    Desktop::Types::Notifications::Description description;
    description.title = "Shell failure hook";
    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::ShellNativeApply);
    static_cast<void>(
        context.expectEq("shell native-apply failure is deterministic", ErrorCode::NativeFailure, notifications.publish(description).status.code));

    Desktop::TestHooks::resetFailures();
    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::ShellNativeClose);
    static_cast<void>(context.expectEq("shell native-close failure is retryable", ErrorCode::NativeFailure, notifications.close().code));
    static_cast<void>(context.expectTrue("shell native-close failure retains the open center", notifications.isOpen()));
    Desktop::TestHooks::resetFailures();
    static_cast<void>(context.expectTrue("shell native-close retry succeeds", notifications.close().ok()));
}
#endif
