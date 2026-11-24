/// @file desktop_shell_lifecycle_tests.inl
/// @brief Shell validation, ownership, and failure tests.

/// ------------------------------------------------------------
/// Shell validation and lifetimes
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

    if(!context.expectTrue("shell validation queue opens", queue.open(ShellTypes::QueueConfiguration::owned(8)).ok()))
    {
        return;
    }

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
    static_cast<void>(context.expectTrue("shell validation queue closes", queue.close().ok()));

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

    if(!context.expectTrue("taskbar event queue opens", taskbarQueue.open(ShellTypes::QueueConfiguration::owned(8)).ok()))
    {
        return;
    }

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
    static_cast<void>(context.expectTrue("taskbar event queue closes", taskbarQueue.close().ok()));

    // Verify tray queue ownership and later menu updates.
    Desktop::ShellEventQueue trayQueue;

    if(!context.expectTrue("tray event queue opens", trayQueue.open(ShellTypes::QueueConfiguration::owned(8)).ok()))
    {
        return;
    }

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
    static_cast<void>(context.expectTrue("tray event queue closes", trayQueue.close().ok()));

    if(!context.expectTrue("empty tray event queue reopens", trayQueue.open(ShellTypes::QueueConfiguration::owned(8)).ok()))
    {
        return;
    }

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
    static_cast<void>(context.expectTrue("empty tray event queue closes", trayQueue.close().ok()));

    // Verify notification queue ownership and item lifecycle.
    Desktop::NotificationCenter notifications;
    Desktop::ShellEventQueue notificationQueue;

    if(!context.expectTrue("notification event queue opens", notificationQueue.open(ShellTypes::QueueConfiguration::owned(8)).ok()))
    {
        return;
    }

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
    static_cast<void>(context.expectTrue("notification event queue closes", notificationQueue.close().ok()));
    static_cast<void>(context.expectTrue("shell native lifetime Window closes", window.close().ok()));
}

#if DESKTOP_INTERNAL_TEST_HOOKS
void testShellFailureHooks(TestSupport::Context &context)
{
    // Verify native-open failure is reported without leaving partial state.
    Desktop::Internal::TestHooks::resetFailures();
    Desktop::NotificationCenter notifications;
    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::ShellNativeOpen);
    const IO::Types::Status openFailure = notifications.open();

    if(!context.expectEq("shell native-open failure is deterministic", ErrorCode::NativeFailure, openFailure.code))
    {
        Desktop::Internal::TestHooks::resetFailures();

        if(notifications.isOpen())
        {
            static_cast<void>(notifications.close());
        }

        return;
    }

    Desktop::Internal::TestHooks::resetFailures();

    if(!context.expectTrue("notification center opens after injected failure", notifications.open().ok()))
    {
        return;
    }

    // Verify native apply and close failures can be retried.
    Desktop::Types::Notifications::Description description;
    description.title = "Shell failure hook";
    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::ShellNativeApply);
    const auto applyFailure = notifications.publish(description);

    if(!context.expectEq("shell native-apply failure is deterministic", ErrorCode::NativeFailure, applyFailure.status.code))
    {
        Desktop::Internal::TestHooks::resetFailures();
        static_cast<void>(notifications.close());
        return;
    }

    Desktop::Internal::TestHooks::resetFailures();
    Desktop::Internal::TestHooks::failNext(Desktop::Internal::TestHooks::FailurePoint::ShellNativeClose);
    const IO::Types::Status closeFailure = notifications.close();

    if(!context.expectEq("shell native-close failure is retryable", ErrorCode::NativeFailure, closeFailure.code))
    {
        Desktop::Internal::TestHooks::resetFailures();

        if(notifications.isOpen())
        {
            static_cast<void>(notifications.close());
        }

        return;
    }

    if(!context.expectTrue("shell native-close failure retains the open center", notifications.isOpen()))
    {
        Desktop::Internal::TestHooks::resetFailures();

        if(notifications.isOpen())
        {
            static_cast<void>(notifications.close());
        }

        return;
    }

    Desktop::Internal::TestHooks::resetFailures();
    static_cast<void>(context.expectTrue("shell native-close retry succeeds", notifications.close().ok()));
}
#endif
