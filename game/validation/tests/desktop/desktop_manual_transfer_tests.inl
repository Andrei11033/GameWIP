/// @file desktop_manual_transfer_tests.inl
/// @brief Shell, file-drop, and native drag-and-drop manual validation cases.

[[nodiscard]] std::filesystem::path shellLaunchReceiptPath(std::string_view token)
{
    return std::filesystem::temp_directory_path() / std::format("GameWIP-shell-launch-{}.txt", token);
}

[[nodiscard]] std::filesystem::path shellGenericLaunchReceiptPath(const std::filesystem::path &targetPath)
{
    return targetPath.parent_path() / ".gamewip-generic-launch-receipt.txt";
}

/// @brief Confirms a controlled shell launch without starting the full validation runner again.
int runShellLaunchProbeChild(int argc, char **argv)
{
    constexpr std::string_view prefix = "--desktop-shell-launch-token=";
    constexpr std::string_view receiptPrefix = "--desktop-shell-launch-receipt=";
    constexpr std::string_view targetPrefix = "--desktop-shell-launch-expected-target=";
    const auto arguments = GameWIP::Validation::processArguments(argc, argv);
    std::string_view expectedTarget;
    std::filesystem::path receiptPath;

    for (char *argument : arguments)
    {
        if (argument != nullptr && std::string_view{argument}.starts_with(targetPrefix))
        {
            expectedTarget = std::string_view{argument}.substr(targetPrefix.size());
        }

        if (argument != nullptr && std::string_view{argument}.starts_with(receiptPrefix))
        {
            receiptPath = std::filesystem::path(std::string_view{argument}.substr(receiptPrefix.size()));
        }
    }

    for (char *argument : arguments)
    {
        if (argument == nullptr || !std::string_view{argument}.starts_with(prefix))
        {
            continue;
        }
        const std::string_view token = std::string_view{argument}.substr(prefix.size());

        if (token.empty() || token.size() > 64 ||
            !std::ranges::all_of(
                token,
                [](char value)
                {
                    return (value >= '0' && value <= '9') || value == '-';
                }))
        {
            return 2;
        }

        if (!expectedTarget.empty())
        {
            std::string receivedArguments;

            for (char *received : arguments)
            {
                receivedArguments += received == nullptr ? "<null>" : received;
                receivedArguments.push_back('\n');
            }
            const std::filesystem::path expectedArgumentsPath = receiptPath.empty() ? shellLaunchReceiptPath(token) : receiptPath;
            static_cast<void>(TestSupport::writeTextFile(expectedArgumentsPath.string() + ".arguments", receivedArguments));

            if (arguments.size() < 5 || arguments.back() == nullptr || std::string_view{arguments.back()} != expectedTarget)
            {
                return 4;
            }
        }

        if (receiptPath.empty())
        {
            receiptPath = shellLaunchReceiptPath(token);
        }

        return TestSupport::writeTextFile(receiptPath, token).ok() ? 0 : 3;
    }

    return 0;
}

/// @brief Exercises independently selectable file/Window and native shell checks.
void testManualFilesAndShell(
    TestSupport::Context &context,
    const GameWIP::Test::DesktopTestOptions &options,
    bool includeWindow = true,
    bool includeShell = true)
{
    if (!beginManualSuite(context, options, "Window files and shell behavior"))
    {
        return;
    }

    const auto capabilitiesResult = Desktop::getCapabilities();
    const Desktop::Types::Capabilities capabilities = capabilitiesResult.value_or({});
    Desktop::Types::Description description;
    description.fileDropEnabled = includeWindow && capabilities.supports(Desktop::Types::Capability::FileDrop);
    Desktop::Window window;

    if (!openManualWindow(context, window, "GameWIP file and shell validation", description))
    {
        return;
    }

    if (includeWindow)
    {
        if (capabilities.supports(Desktop::Types::Capability::FileDrop))
        {
            const auto runDrop = [&](std::string_view name, std::string_view instruction, std::size_t minimumPaths)
            {
                window.clearEvents();
                std::size_t groupedEvents = 0;
                std::size_t pathCount = 0;
                const auto observeDrops = [&]
                {
                    Desktop::Types::Event event;

                    while (window.popEvent(event))
                    {
                        if (const auto *drop = event.getIf<Desktop::Types::Events::FilesDropped>())
                        {
                            ++groupedEvents;
                            pathCount += drop->paths.size();
                            std::string pathList;

                            for (const auto &path : drop->paths)
                            {
                                const std::u8string utf8 = path.generic_u8string();

                                if (!pathList.empty())
                                {
                                    pathList.append("\r\n");
                                }
                                pathList.append(reinterpret_cast<const char *>(utf8.data()), utf8.size());
                            }
                            const std::string observation = std::format(
                                "Received FilesDroppedEvent: groupedEvents={} paths={} clientPosition={}\r\n{}",
                                groupedEvents,
                                pathCount,
                                drop->clientPosition.has_value(),
                                pathList);

                            if (manualStatusWindow != nullptr)
                            {
                                manualStatusWindow->setObservation(observation);
                            }
                            static_cast<void>(window.setTitle(std::format("Drop received: {} path(s)", pathCount)));
                        }
                    }
                };
                static_cast<void>(window.setTitle("Waiting for file drop..."));

                if (manualStatusWindow != nullptr)
                {
                    manualStatusWindow->setObservation("Waiting for FilesDroppedEvent.");
                }
                const TestSupport::Types::Reporting::ManualAnswer answer = recordManualCheck(context, window, name, instruction, observeDrops);

                if (answer != TestSupport::Types::Reporting::ManualAnswer::Yes)
                {
                    return;
                }
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
            {
                static_cast<void>(
                    context.expectFalse("disabled file drops queue no event", consumeEventOfType<Desktop::Types::Events::FilesDropped>(window)));
            }
        }
        else
        {
            context.skip("Window file drops", "backend does not advertise FileDrop");
        }

        if (capabilities.supports(Desktop::Types::Capability::WindowIcon))
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
            const std::array images{Desktop::Types::IconImageView{{16, 16}, smallPixels}, Desktop::Types::IconImageView{{32, 32}, largePixels}};
            static_cast<void>(context.expectTrue("custom shell icons apply", window.setIcon(images).ok()));
            recordManualCheck(
                context,
                window,
                "custom shell icons",
                "Does the blue/cyan GameWIP validation icon appear clearly at the appropriate small title-bar and larger taskbar shell sizes?");
            static_cast<void>(window.clearIcon());
        }
    }

    if (includeShell)
    {
        const auto shellCapabilityResult = Desktop::Shell::getCapabilities();

        if (!requireManualStatus(context, "shell capability query", shellCapabilityResult.status))
        {
            return;
        }
        const auto &shellCapabilities = *shellCapabilityResult;
        Desktop::ShellEventQueue shellQueue;

        if (requireManualStatus(context, "shell event queue setup", shellQueue.open(Desktop::Types::Shell::QueueConfiguration::owned(64))))
        {
            bool taskbarCommandSeen = false;
            bool trayCommandSeen = false;
            std::size_t shellEventCount = 0;
            std::string shellObservation = "Waiting for shell interaction.";
            const auto drainShellEvents = [&]
            {
                Desktop::Types::Shell::Event event;

                while (shellQueue.popEvent(event))
                {
                    ++shellEventCount;

                    if (const auto *trayActivation = event.getIf<Desktop::Types::Shell::Events::TrayActivated>())
                    {
                        shellObservation =
                            std::format("Tray activation: icon={} kind={}", trayActivation->iconId.value, static_cast<int>(trayActivation->kind));
                    }
                    else if (const auto *trayCommand = event.getIf<Desktop::Types::Shell::Events::TrayCommandInvoked>())
                    {
                        trayCommandSeen = trayCommandSeen || trayCommand->commandId.value == 702;
                        shellObservation = std::format("Tray command: icon={} command={}", trayCommand->iconId.value, trayCommand->commandId.value);
                    }
                    else if (const auto *taskbarCommand = event.getIf<Desktop::Types::Shell::Events::TaskbarThumbnailButtonInvoked>())
                    {
                        taskbarCommandSeen = taskbarCommandSeen || taskbarCommand->commandId.value == 701;
                        shellObservation =
                            std::format("Taskbar command: window={} command={}", taskbarCommand->windowId.value, taskbarCommand->commandId.value);
                    }
                    else if (const auto *notification = event.getIf<Desktop::Types::Shell::Events::NotificationActivated>())
                    {
                        shellObservation = std::format("Notification activation: id={}", notification->notificationId.value);
                    }
                }

                if (manualStatusWindow != nullptr)
                {
                    manualStatusWindow->setObservation(std::format("Shell events received={} {}", shellEventCount, shellObservation));
                }
            };

            if (shellCapabilities.supports(Desktop::Types::Shell::Capability::TaskbarThumbnailButtons))
            {
                Desktop::Types::Taskbar::ThumbnailButton thumbnailButton{{701}, "Shell manual command"};
                Desktop::Types::Taskbar::Description taskbarDescription;
                taskbarDescription.progress = Desktop::Types::Shell::Progress{Desktop::Types::Shell::ProgressState::Normal, 0.35};
                taskbarDescription.thumbnailButtons = std::span{&thumbnailButton, 1};
                Desktop::TaskbarItem taskbar;
                const IO::Types::Status taskbarStatus = taskbar.open(window, taskbarDescription, shellQueue);

                if (taskbarStatus.ok())
                {
                    using Progress = Desktop::Types::Shell::Progress;
                    using ProgressState = Desktop::Types::Shell::ProgressState;
                    const std::array progressCases{
                        std::pair<std::string_view, Progress>{"normal 35 percent", {ProgressState::Normal, 0.35}},
                        std::pair<std::string_view, Progress>{"indeterminate", {ProgressState::Indeterminate, 0.0}},
                        std::pair<std::string_view, Progress>{"paused 65 percent", {ProgressState::Paused, 0.65}},
                        std::pair<std::string_view, Progress>{"error 65 percent", {ProgressState::Error, 0.65}},
                        std::pair<std::string_view, Progress>{"normal 100 percent", {ProgressState::Normal, 1.0}}};

                    for (const auto &[label, progress] : progressCases)
                    {
                        if (requireManualStatus(context, std::format("taskbar progress {} applies", label), taskbar.setProgress(progress)))
                        {
                            recordManualCheck(
                                context,
                                window,
                                std::format("taskbar progress {}", label),
                                std::format("Does the taskbar show {} progress? This state remains active until you answer.", label),
                                drainShellEvents);
                        }
                    }
                    const auto answer = recordManualCheck(
                        context,
                        window,
                        "taskbar thumbnail command",
                        "Open the thumbnail preview and click the Shell manual command. Answer yes only if the "
                        "command event is shown in the diagnostics.",
                        drainShellEvents);

                    if (answer == TestSupport::Types::Reporting::ManualAnswer::Yes)
                    {
                        static_cast<void>(context.expectTrue("thumbnail command 701 reaches shell queue", taskbarCommandSeen));
                    }
                    static_cast<void>(taskbar.close());
                }
                else
                {
                    static_cast<void>(requireManualStatus(context, "taskbar publication", taskbarStatus));
                }
            }
            else
            {
                context.skip("taskbar progress and thumbnail buttons", "backend does not advertise TaskbarThumbnailButtons");
            }

            const std::array<std::byte, 4> trayPixel{std::byte{0x36}, std::byte{0xD2}, std::byte{0xFF}, std::byte{0xFF}};
            const Desktop::Types::IconImageView trayIconImage{{1, 1}, trayPixel};
            Desktop::Types::Tray::MenuItem trayCommand;
            trayCommand.kind = Desktop::Types::Tray::MenuItemKind::Command;
            trayCommand.commandId = {702};
            trayCommand.label = "Shell manual command";
            Desktop::Types::Tray::Description trayDescription;
            trayDescription.icons = std::span{&trayIconImage, 1};
            trayDescription.tooltip = "GameWIP shell manual validation";
            trayDescription.menu.items = std::span{&trayCommand, 1};
            Desktop::TrayIcon tray;

            if (shellCapabilities.supports(Desktop::Types::Shell::Capability::TrayMenu))
            {
                const IO::Types::Status trayStatus = tray.open(trayDescription, shellQueue);

                if (trayStatus.ok())
                {
                    const auto answer = recordManualCheck(
                        context,
                        window,
                        "tray icon and recursive menu",
                        "Hover the cyan tray icon and verify the GameWIP shell manual validation tooltip. Right-click it, choose Shell manual "
                        "command, and answer yes only if diagnostics show command=702.",
                        drainShellEvents);

                    if (answer == TestSupport::Types::Reporting::ManualAnswer::Yes)
                    {
                        static_cast<void>(context.expectTrue("tray command 702 reaches shell queue", trayCommandSeen));
                    }
                    static_cast<void>(tray.close());
                }
                else
                {
                    static_cast<void>(requireManualStatus(context, "tray publication", trayStatus));
                }
            }
            else
            {
                context.skip("tray icon and recursive menu", "backend does not advertise TrayMenu");
            }

            Desktop::NotificationCenter notificationCenter;

            if (shellCapabilities.supports(Desktop::Types::Shell::Capability::Notifications))
            {
                const IO::Types::Status notificationOpenStatus = notificationCenter.open(shellQueue);

                if (notificationOpenStatus.ok())
                {
                    Desktop::Types::Notifications::Description notificationDescription;
                    notificationDescription.title = "GameWIP shell manual validation";
                    notificationDescription.body = "Verify the notification appearance and dismissal.";
                    const auto published = notificationCenter.publish(notificationDescription);

                    if (published.status.ok())
                    {
                        recordManualCheck(
                            context,
                            window,
                            "notification appearance and dismissal",
                            "Verify the title/body and sound policy, then dismiss the notification. Answer yes only if the notification appeared and "
                            "disappeared normally.",
                            drainShellEvents);
                        static_cast<void>(notificationCenter.dismiss(*published));
                    }
                    else
                    {
                        static_cast<void>(requireManualStatus(context, "notification publication", published.status));
                    }
                    static_cast<void>(notificationCenter.close());
                }
                else
                {
                    static_cast<void>(requireManualStatus(context, "notification center setup", notificationOpenStatus));
                }
            }
            else
            {
                context.skip("notification appearance and dismissal", "backend does not advertise Notifications");
            }

            std::array<wchar_t, 32768> executableText{};
            const DWORD executableLength = GetModuleFileNameW(nullptr, executableText.data(), static_cast<DWORD>(executableText.size()));

            if (!context.expectTrue("shell launch executable resolves", executableLength > 0 && executableLength < executableText.size()))
            {
                return;
            }
            const std::filesystem::path executable{executableText.data()};
            const std::string launchToken = std::format("{}-{}", GetCurrentProcessId(), GetTickCount64());
            const std::string tokenArgument = std::format("--desktop-shell-launch-token={}", launchToken);
            const std::filesystem::path launchReceipt = shellLaunchReceiptPath(launchToken);
            const std::string receiptArgument = std::format("--desktop-shell-launch-receipt={}", launchReceipt.string());
            std::filesystem::path genericLaunchReceipt;
            struct ReceiptCleanup
            {
                std::filesystem::path path;
                std::filesystem::path genericPath;
                ~ReceiptCleanup()
                {
                    static_cast<void>(TestSupport::removeIfExists(path));
                    static_cast<void>(TestSupport::removeIfExists(genericPath));
                }
            } receiptCleanup{launchReceipt, {}};
            const std::array<Desktop::Types::Shell::LaunchArgumentView, 3> launchArguments{
                Desktop::Types::Shell::LiteralLaunchArgumentView{"--desktop-test-child=shell-launch-probe"},
                Desktop::Types::Shell::LiteralLaunchArgumentView{tokenArgument},
                Desktop::Types::Shell::LiteralLaunchArgumentView{receiptArgument}};
            const Desktop::Types::Shell::LaunchActionView launchAction{Desktop::Types::Shell::PathTargetView{std::cref(executable)}, launchArguments};
            const auto launchReceived = [&]
            {
                const auto receipt = TestSupport::readTextFile(launchReceipt);

                if (receipt.status.ok() && receipt.text == launchToken)
                {
                    return true;
                }

                if (genericLaunchReceipt.empty())
                {
                    return false;
                }
                const auto genericReceipt = TestSupport::readTextFile(genericLaunchReceipt);

                if (genericReceipt.status.ok() && !genericReceipt.text.empty())
                {
                    return true;
                }
                // The generic Applications command is a separate process.  A
                // successful launch is established by a non-empty receipt;
                // tolerate a transient read result while that child is still
                // closing its file handle and let waitForLaunch poll again.
                std::error_code fileError;
                return std::filesystem::is_regular_file(genericLaunchReceipt, fileError) &&
                       std::filesystem::file_size(genericLaunchReceipt, fileError) > 0;
            };
            const auto waitForLaunch = [&]
            {
                for (int attempt = 0; attempt < 20; ++attempt)
                {
                    if (launchReceived())
                    {
                        return true;
                    }
                    Sleep(50);
                }

                return launchReceived();
            };
            const auto observeLaunch = [&]
            {
                drainShellEvents();

                if (manualStatusWindow != nullptr)
                {
                    manualStatusWindow->setObservation(
                        launchReceived() ? "Controlled shell launch receipt received." : "Waiting for controlled shell launch receipt.");
                }
            };
            const Desktop::Types::JumpLists::Task jumpTask{"GameWIP shell manual validation", "Desktop shell integration", launchAction};
            const std::array jumpTasks{jumpTask};
            Desktop::Types::JumpLists::Description jumpListDescription;
            jumpListDescription.tasks = jumpTasks;

            if (shellCapabilities.supports(Desktop::Types::Shell::Capability::JumpLists))
            {
                const IO::Types::Status jumpListStatus = Desktop::JumpLists::publish(jumpListDescription);

                if (jumpListStatus.ok())
                {
                    const auto answer = recordManualCheck(
                        context,
                        window,
                        "jump-list publication",
                        "Open this application's taskbar context menu and verify the GameWIP shell manual validation task appears and launches this "
                        "validation executable. The diagnostics must show a controlled launch receipt.",
                        observeLaunch);

                    if (answer == TestSupport::Types::Reporting::ManualAnswer::Yes)
                    {
                        static_cast<void>(context.expectTrue("jump-list task launches controlled helper", launchReceived()));
                    }
                    static_cast<void>(requireManualStatus(context, "jump-list cleanup", Desktop::JumpLists::publish({})));
                }
                else
                {
                    static_cast<void>(requireManualStatus(context, "jump-list publication", jumpListStatus));
                }
            }
            else
            {
                context.skip("jump-list publication", "backend does not advertise JumpLists");
            }

            static_cast<void>(TestSupport::removeIfExists(launchReceipt));
            const std::string ownerKey = std::format("GameWIP.Validation.Shell.{}", GetCurrentProcessId());
            const std::string extension = std::format(".gamewip-validation-{}", GetCurrentProcessId());
            const std::string scheme = std::format("gamewip-validation-{}", GetCurrentProcessId());
            TestSupport::ScopedTemporaryDirectory registrationFixtures("desktop-manual-shell-registration");

            if (!context.expectTrue("shell registration fixtures create", registrationFixtures.status().ok()))
            {
                return;
            }
            const auto fileProbe = registrationFixtures.path() / ("probe with spaces" + extension);
            const auto uriProbe = registrationFixtures.path() / "uri-launch.html";
            genericLaunchReceipt = shellGenericLaunchReceiptPath(fileProbe);
            receiptCleanup.genericPath = genericLaunchReceipt;
            static_cast<void>(TestSupport::removeIfExists(genericLaunchReceipt));

            if (!context.expectTrue(
                    "shell file fixture writes",
                    TestSupport::writeTextFile(fileProbe, "Controlled GameWIP shell validation fixture.\n").ok()) ||
                !context.expectTrue(
                    "shell URI link fixture writes",
                    TestSupport::writeTextFile(
                        uriProbe,
                        std::format(
                            "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><title>GameWIP URI validation</title>"
                            "<body><p><a target=\"_blank\" rel=\"noopener\" href=\"{}://probe\">Launch the controlled GameWIP validation "
                            "helper</a></p>"
                            "<p>Address-bar fallback: <code>{}://probe</code></p></body></html>",
                            scheme,
                            scheme))
                        .ok()))
            {
                return;
            }
            const std::array<Desktop::Types::Shell::LaunchArgumentView, 4> fileLaunchArguments{
                launchArguments[0],
                launchArguments[1],
                launchArguments[2],
                Desktop::Types::Shell::LaunchPlaceholder::SelectedPath};
            const std::array<Desktop::Types::Shell::LaunchArgumentView, 4> uriLaunchArguments{
                launchArguments[0],
                launchArguments[1],
                launchArguments[2],
                Desktop::Types::Shell::LaunchPlaceholder::SelectedUri};
            Desktop::Types::Registration::FileExtensionDescription fileRegistration;
            fileRegistration.ownerKey = ownerKey;
            fileRegistration.extension = extension;
            fileRegistration.displayName = "GameWIP shell manual validation";
            fileRegistration.defaultAction =
                Desktop::Types::Shell::LaunchActionView{Desktop::Types::Shell::PathTargetView{std::cref(executable)}, fileLaunchArguments};
            fileRegistration.conflictPolicy = Desktop::Types::Registration::ConflictPolicy::Coexist;

            if (shellCapabilities.supports(Desktop::Types::Shell::Capability::FileAssociationRegistration))
            {
                const auto registration = Desktop::Registration::registerFileExtension(fileRegistration);

                if (registration.status.ok())
                {
                    const auto answer = recordManualCheck(
                        context,
                        window,
                        "current-user file registration",
                        std::format(
                            "Open {} and verify this validation executable is offered or launched. Choose it with Just once. Answer yes only if "
                            "the current-user registration behaves as documented and the diagnostics show a fresh controlled launch receipt.",
                            fileProbe.string()),
                        observeLaunch);

                    if (answer == TestSupport::Types::Reporting::ManualAnswer::Yes)
                    {
                        const bool launched = waitForLaunch();

                        if (!launched)
                        {
                            const auto receipt = TestSupport::readTextFile(genericLaunchReceipt);
                            context.fail(
                                "file association launches controlled helper",
                                std::format(
                                    "generic receipt was not observed at '{}' (status={}, bytes={})",
                                    genericLaunchReceipt.string(),
                                    static_cast<int>(receipt.status.error),
                                    receipt.text.size()));
                        }
                        else
                        {
                            context.pass("file association launches controlled helper");
                        }
                    }
                }
                else
                {
                    static_cast<void>(requireManualStatus(context, "current-user file registration setup", registration.status));
                }
                static_cast<void>(requireManualStatus(
                    context,
                    "file registration cleanup",
                    Desktop::Registration::unregisterFileExtension(ownerKey, extension).status));
                static_cast<void>(TestSupport::removeIfExists(genericLaunchReceipt));
            }
            else
            {
                context.skip("current-user file registration", "backend does not advertise FileAssociationRegistration");
            }

            static_cast<void>(TestSupport::removeIfExists(launchReceipt));
            Desktop::Types::Registration::UriSchemeDescription uriRegistration;
            uriRegistration.ownerKey = ownerKey;
            uriRegistration.scheme = scheme;
            uriRegistration.displayName = "GameWIP shell manual validation";
            uriRegistration.defaultAction =
                Desktop::Types::Shell::LaunchActionView{Desktop::Types::Shell::PathTargetView{std::cref(executable)}, uriLaunchArguments};
            uriRegistration.conflictPolicy = Desktop::Types::Registration::ConflictPolicy::Coexist;

            if (shellCapabilities.supports(Desktop::Types::Shell::Capability::UriSchemeRegistration))
            {
                const auto registration = Desktop::Registration::registerUriScheme(uriRegistration);

                if (registration.status.ok())
                {
                    const auto answer = recordManualCheck(
                        context,
                        window,
                        "current-user URI registration",
                        std::format(
                            "Open {} in a browser and click its controlled {}:// link. If the browser blocks custom protocols from a local file "
                            "page, "
                            "copy the exact {}://probe address shown in the page into the browser address bar and press Enter. Verify the "
                            "diagnostics "
                            "show a fresh shell launch receipt.",
                            uriProbe.string(),
                            scheme,
                            scheme),
                        observeLaunch);

                    if (answer == TestSupport::Types::Reporting::ManualAnswer::Yes)
                    {
                        static_cast<void>(context.expectTrue("URI association launches controlled helper", launchReceived()));
                    }
                }
                else
                {
                    static_cast<void>(requireManualStatus(context, "current-user URI registration setup", registration.status));
                }
                static_cast<void>(
                    requireManualStatus(context, "URI registration cleanup", Desktop::Registration::unregisterUriScheme(ownerKey, scheme).status));
            }
            else
            {
                context.skip("current-user URI registration", "backend does not advertise UriSchemeRegistration");
            }

            static_cast<void>(shellQueue.close());
        }
    }

    if (!includeWindow)
    {
        static_cast<void>(window.close());
        return;
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
            Desktop::Types::Controls controls = window.controls();
            controls.maximizable = maximizable;
            const IO::Types::Status controlsStatus = window.setControls(controls);
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
    Desktop::Types::Controls maximizableControls = window.controls();
    maximizableControls.maximizable = true;
    static_cast<void>(window.setControls(maximizableControls));
    static_cast<void>(context.expectTrue("maximizable-then-nonresizable transition is rejected", !window.setResizable(false).ok()));
    static_cast<void>(window.setControls({.closable = true, .minimizable = true, .maximizable = false}));
    static_cast<void>(window.setResizable(false));
    maximizableControls = window.controls();
    maximizableControls.maximizable = true;
    static_cast<void>(context.expectTrue("nonresizable-then-maximizable transition is rejected", !window.setControls(maximizableControls).ok()));
    static_cast<void>(window.setResizable(true));
    static_cast<void>(window.setControls({}));
    Desktop::Types::Controls independentControls = window.controls();
    independentControls.closable = false;
    independentControls.minimizable = false;
    static_cast<void>(context.expectTrue("close and minimize controls disable independently", window.setControls(independentControls).ok()));
    static_cast<void>(context.expectFalse("close control cached disabled", window.controls().closable));
    static_cast<void>(context.expectFalse("minimize control cached disabled", window.controls().minimizable));
    recordManualCheck(
        context,
        window,
        "standard control combinations",
        "The diagnostics must show close=false, minimize=false, maximize=true, resizable=true. Are only the close and minimize controls disabled "
        "in the native frame?");
    static_cast<void>(window.setControls({}));

    Desktop::Types::Description ownedDescription;
    ownedDescription.owner = window.id();
    Desktop::Window owned;

    if (openManualWindow(context, owned, "GameWIP owned taskbar validation", ownedDescription))
    {
        recordManualCheck(
            context,
            owned,
            "owned taskbar default",
            "The diagnostics must show a nonzero ownerId. Does the owned Window have no independent taskbar entry?");
        static_cast<void>(owned.setOwner(std::nullopt));
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

struct ManualDragDropCapture
{
    std::size_t entered = 0;
    std::size_t moved = 0;
    std::size_t left = 0;
    std::size_t dropped = 0;
    std::size_t regionTransitions = 0;
    std::optional<Desktop::Types::DragDrop::Events::Dropped> lastDrop;

    void clear(Desktop::DragDropTarget &target)
    {
        target.clearEvents();
        *this = {};
    }

    void drain(Desktop::DragDropTarget &target)
    {
        namespace DDEvents = Desktop::Types::DragDrop::Events;
        Desktop::Types::DragDrop::Event event;

        while (target.popEvent(event))
        {
            if (event.getIf<DDEvents::Entered>() != nullptr)
            {
                ++entered;
            }
            else if (const auto *movement = event.getIf<DDEvents::Moved>())
            {
                ++moved;

                if (movement->previousRegion != movement->region)
                {
                    ++regionTransitions;
                }
            }
            else if (event.getIf<DDEvents::Left>() != nullptr)
            {
                ++left;
            }
            else if (auto *drop = event.getIf<DDEvents::Dropped>())
            {
                ++dropped;
                lastDrop.emplace(std::move(*drop));
            }
        }

        if (manualStatusWindow != nullptr)
        {
            manualStatusWindow->setObservation(observation(target));
        }
    }

    [[nodiscard]] std::string observation(const Desktop::DragDropTarget &target) const
    {
        std::string text = std::format(
            "DragDrop events: Entered={} Moved={} transitions={} Left={} Dropped={} pending={} queueDrops={}",
            entered,
            moved,
            regionTransitions,
            left,
            dropped,
            target.eventQueueInfo().pendingEvents,
            target.eventQueueInfo().droppedEvents);

        if (lastDrop)
        {
            for (const auto &item : lastDrop->payload)
            {
                if (const auto *value = std::get_if<Desktop::Types::DataTransfer::Text>(&item))
                {
                    text += std::format("\r\nText ({} UTF-8 bytes): {}", value->text.size(), value->text);
                }
                else if (const auto *files = std::get_if<Desktop::Types::DataTransfer::FileList>(&item))
                {
                    text += std::format("\r\nFiles: {}", files->paths.size());

                    for (const auto &path : files->paths)
                    {
                        const auto name = path.u8string();
                        text += "\r\n" + std::string(name.begin(), name.end());
                    }
                }
                else if (const auto *image = std::get_if<Desktop::Types::DataTransfer::Image>(&item))
                {
                    text += std::format("\r\nImage: {} x {}, {} RGBA bytes", image->size.width, image->size.height, image->rgba8.size());
                }
                else if (const auto *custom = std::get_if<Desktop::Types::DataTransfer::CustomData>(&item))
                {
                    text += std::format("\r\nCustom: {}, {} bytes", custom->formatName, custom->bytes.size());
                }
            }
        }

        return text;
    }
};

[[nodiscard]] bool manualDragButtonDown(Desktop::Types::DragDrop::TriggerButton button) noexcept
{
    int key = VK_LBUTTON;

    if (button == Desktop::Types::DragDrop::TriggerButton::Right)
    {
        key = VK_RBUTTON;
    }
    else if (button == Desktop::Types::DragDrop::TriggerButton::Middle)
    {
        key = VK_MBUTTON;
    }

    return (GetAsyncKeyState(key) & 0x8000) != 0;
}

[[nodiscard]] bool manualPointerInside(const Desktop::Window &window) noexcept
{
    const auto native = Desktop::Native::Win32::getHandle(window);

    if (!native.status.ok() || native->window == nullptr)
    {
        return false;
    }
    POINT pointer{};
    RECT client{};
    return GetCursorPos(&pointer) != FALSE && ScreenToClient(native->window, &pointer) != FALSE && GetClientRect(native->window, &client) != FALSE &&
           PtInRect(&client, pointer) != FALSE;
}

struct ManualDragDropRun
{
    TestSupport::Types::Reporting::ManualAnswer answer = TestSupport::Types::Reporting::ManualAnswer::Skipped;
    bool attempted = false;
    std::size_t attempts = 0;
    Desktop::Types::DragDrop::Result result;
};

ManualDragDropRun runManualDragDropSource(
    TestSupport::Context &context,
    Desktop::Window &source,
    Desktop::Window *targetWindow,
    Desktop::DragDropTarget *target,
    ManualDragDropCapture *capture,
    std::string_view name,
    std::string_view instruction,
    const Desktop::Types::DragDrop::Description &description)
{
    ManualDragDropRun run;

    if (!manualCaseSelected(name))
    {
        return run;
    }
    bool wasDown = manualDragButtonDown(description.triggerButton);
    const auto effectName = [](Desktop::Types::DragDrop::Effect effect) -> std::string_view
    {
        switch (effect)
        {
        case Desktop::Types::DragDrop::Effect::None:
            return "None";
        case Desktop::Types::DragDrop::Effect::Copy:
            return "Copy";
        case Desktop::Types::DragDrop::Effect::Move:
            return "Move";
        case Desktop::Types::DragDrop::Effect::Link:
            return "Link";
        default:
            return "Combined";
        }
    };
    const auto observe = [&]
    {
        if (target != nullptr && capture != nullptr)
        {
            capture->drain(*target);
        }

        if (targetWindow != nullptr)
        {
            paintManualValidationSurface(*targetWindow, ManualSurfaceLayout::DragDropTarget);
        }
        const bool down = manualDragButtonDown(description.triggerButton);

        if (!wasDown && down && manualPointerInside(source))
        {
            run.attempted = true;
            ++run.attempts;

            if (target != nullptr && capture != nullptr)
            {
                capture->clear(*target);
            }
            run.result = Desktop::DragDrop::beginDrag(source, description);

            if (target != nullptr && capture != nullptr)
            {
                capture->drain(*target);
            }
        }
        wasDown = down;

        if (manualStatusWindow != nullptr)
        {
            manualStatusWindow->setObservation(
                std::format(
                    "Attempt {}: {} | effect={} ({}) | status={} native={}\r\n{}\r\nRelease the button, then drag again to retry. The last attempt "
                    "is checked.",
                    run.attempts,
                    !run.attempted                                                     ? "Waiting"
                    : run.result.outcome == Desktop::Types::DragDrop::Outcome::Dropped ? "Dropped"
                                                                                       : "Cancelled",
                    effectName(run.result.effect),
                    static_cast<int>(run.result.effect),
                    IO::errorCodeName(run.result.status.code),
                    run.result.status.nativeCode,
                    target != nullptr && capture != nullptr ? capture->observation(*target) : std::string{}));
        }
    };
    const std::string retryInstruction =
        std::string(instruction) +
        " Retry with a fresh press in the source if you miss. Confirm the last attempt; its named effect remains in diagnostics.";
    run.answer = recordManualCheck(context, source, name, retryInstruction, observe, ManualSurfaceLayout::DragDropSource);

    if (run.answer == TestSupport::Types::Reporting::ManualAnswer::Yes)
    {
        static_cast<void>(context.expectTrue(std::format("{} starts from the requested source button", name), run.attempted));

        if (run.attempted)
        {
            static_cast<void>(context.expectTrue(std::format("{} completes without a native error", name), run.result.status.ok()));
        }
    }

    return run;
}

/// @brief Exercises real native source/target interoperability with guided, reportable prompts.
void testManualDragDrop(TestSupport::Context &context, const GameWIP::Test::DesktopTestOptions &options)
{
    namespace DD = Desktop::Types::DragDrop;
    namespace Transfer = Desktop::Types::DataTransfer;
    using ManualAnswer = TestSupport::Types::Reporting::ManualAnswer;

    if (!beginManualSuite(context, options, "Window native data drag and drop"))
    {
        return;
    }

    context.manual(
        "This suite requires an interactive Windows desktop. Keep both GameWIP Windows visible. "
        "Answer skip when a requested foreign diagnostic provider or consumer is unavailable.");

    Desktop::Types::Description sourceDescription;
    sourceDescription.title = "GameWIP native drag source";
    sourceDescription.windowed.clientSize = {560, 360};
    sourceDescription.activation.visible = true;
    sourceDescription.activation.requestFocus = true;
    Desktop::Types::Description targetDescription = sourceDescription;
    targetDescription.title = "GameWIP native drop target";
    Desktop::Window sourceWindow;
    Desktop::Window targetWindow;

    if (!requireManualStatus(context, "drag source Window setup", sourceWindow.open(sourceDescription, Desktop::Events::owned(64))) ||
        !requireManualStatus(context, "drop target Window setup", targetWindow.open(targetDescription, Desktop::Events::owned(128))))
    {
        return;
    }
    static_cast<void>(sourceWindow.setClientPosition({60, 100}));
    static_cast<void>(targetWindow.setClientPosition({700, 100}));

    constexpr DD::Effect allEffects = DD::Effect::Copy | DD::Effect::Move | DD::Effect::Link;
    constexpr std::string_view customName = "GameWIP.Validation.NativeDragDrop";
    std::array<Transfer::FormatView, 4> allFormats{
        {Transfer::FormatView::text(), Transfer::FormatView::files(), Transfer::FormatView::image(), Transfer::FormatView::custom(customName)}};
    std::array<DD::RegionDescription, 2> regions{
        {{DD::RegionId{1}, std::nullopt, allFormats, DD::EffectPolicy::allow(allEffects, DD::Effect::Copy)},
         {DD::RegionId{2}, Desktop::Types::LogicalRect{{120, 90}, {360, 180}}, allFormats, DD::EffectPolicy::allow(allEffects, DD::Effect::Move)}}};
    Desktop::DragDropTarget target;

    if (!requireManualStatus(
            context,
            "native DragDrop target setup",
            target.open(targetWindow, DD::TargetDescription{regions}, DD::QueueConfiguration::owned(128))))
    {
        static_cast<void>(targetWindow.close());
        static_cast<void>(sourceWindow.close());
        return;
    }
    paintManualValidationSurface(sourceWindow, ManualSurfaceLayout::DragDropSource);
    paintManualValidationSurface(targetWindow, ManualSurfaceLayout::DragDropTarget);

    TestSupport::ScopedTemporaryDirectory fixtures("desktop-drag-interop");

    if (!context.expectTrue("drag fixture directory creates", fixtures.status().ok()))
    {
        return;
    }
    const auto singleFile = fixtures.path() / "single-file.txt";
    const auto unicodeFile = fixtures.path() / L"caf\u00e9-\u65e5\u672c\u8a9e-\U0001f600.txt";
    const auto readme = fixtures.path() / "README.md";
    const auto cmake = fixtures.path() / "CMakeLists.txt";
    const auto copyDirectory = fixtures.path() / "outbound-copy";
    const auto textPage = fixtures.path() / "unicode-drag.html";
    constexpr std::string_view unicodeText = kManualInteropText;

    if (!context.expectTrue(
            "controlled drag fixtures write",
            TestSupport::writeTextFile(singleFile, "single file fixture\n").ok() &&
                TestSupport::writeTextFile(unicodeFile, "Unicode file fixture\n").ok() &&
                TestSupport::writeTextFile(readme, "GameWIP outbound README fixture\n").ok() &&
                TestSupport::writeTextFile(cmake, "# GameWIP outbound CMake fixture\n").ok() && TestSupport::createDirectories(copyDirectory).ok() &&
                TestSupport::writeTextFile(
                    textPage,
                    std::format(
                        "<!doctype html><meta charset=\"utf-8\"><title>GameWIP drag fixture</title><p>Select this exact text and drag it into the "
                        "GameWIP target:</p><p>{}</p><p>Drop outbound text into this box:</p><div contenteditable=\"true\" style=\"border:2px solid "
                        "green;min-height:200px\"></div>",
                        unicodeText))
                    .ok()))
    {
        return;
    }
    context.info(std::format("Drag fixtures: {}", fixtures.path().string()));
    const std::array<GameWIP::FileSystem::Types::Path, 2> files{{readme, cmake}};
    const std::array<std::byte, 16> pixels{
        {std::byte{255},
         std::byte{0},
         std::byte{0},
         std::byte{255},
         std::byte{0},
         std::byte{255},
         std::byte{0},
         std::byte{192},
         std::byte{0},
         std::byte{0},
         std::byte{255},
         std::byte{128},
         std::byte{255},
         std::byte{255},
         std::byte{255},
         std::byte{64}}};
    const std::array<std::byte, 6> customBytes{{std::byte{'G'}, std::byte{'W'}, std::byte{0}, std::byte{0x58}, std::byte{0x3A}, std::byte{0xFF}}};
    const std::array<Transfer::ItemView, 4> allItems{
        {Transfer::TextView{unicodeText},
         Transfer::FileListView{files},
         Transfer::ImageView::packed({2, 2}, pixels),
         Transfer::CustomView{customName, customBytes}}};
    const std::array<Transfer::ItemView, 1> textItem{{Transfer::TextView{unicodeText}}};

    for (const DD::TriggerButton button : {DD::TriggerButton::Left, DD::TriggerButton::Right, DD::TriggerButton::Middle})
    {
        const DD::Result result = Desktop::DragDrop::beginDrag(sourceWindow, DD::Description{textItem, DD::EffectPolicy::source(allEffects), button});
        static_cast<void>(context.expectEq("unheld configured trigger is InvalidArgument", ErrorCode::InvalidArgument, result.status.code));
    }

    ManualDragDropCapture capture;
    capture.clear(target);
    ManualDragDropRun sameProcess = runManualDragDropSource(
        context,
        sourceWindow,
        &targetWindow,
        &target,
        &capture,
        "same-process all-format drop",
        "Hold LEFT in the green source, drag into the green Region 2, move across its boundary at least once, then release. "
        "Answer yes only if the Windows stayed responsive and the diagnostics report a drop.",
        DD::Description{allItems, DD::EffectPolicy::source(allEffects), DD::TriggerButton::Left});

    if (sameProcess.answer == ManualAnswer::Yes && sameProcess.attempted)
    {
        static_cast<void>(context.expectEq("same-process source reports Dropped", DD::Outcome::Dropped, sameProcess.result.outcome));
        static_cast<void>(context.expectEq("Region 2 selects Move", DD::Effect::Move, sameProcess.result.effect));
        static_cast<void>(context.expectTrue("target receives Entered", capture.entered > 0));
        static_cast<void>(context.expectTrue("target receives movement", capture.moved > 0));
        static_cast<void>(context.expectEq("target receives one terminal drop", std::size_t{1}, capture.dropped));
        static_cast<void>(context.expectTrue("target owns the completed payload", capture.lastDrop.has_value()));

        if (capture.lastDrop)
        {
            static_cast<void>(context.expectEq("drop selects Region 2", DD::RegionId{2}, capture.lastDrop->region));
            static_cast<void>(context.expectEq("drop materializes all four formats", std::size_t{4}, capture.lastDrop->payload.size()));

            if (capture.lastDrop->payload.size() == 4)
            {
                const auto *text = std::get_if<Transfer::Text>(&capture.lastDrop->payload[0]);
                const auto *fileList = std::get_if<Transfer::FileList>(&capture.lastDrop->payload[1]);
                const auto *image = std::get_if<Transfer::Image>(&capture.lastDrop->payload[2]);
                const auto *custom = std::get_if<Transfer::CustomData>(&capture.lastDrop->payload[3]);
                static_cast<void>(context.expectTrue("Unicode text round-trips exactly", text != nullptr && text->text == unicodeText));
                static_cast<void>(context.expectTrue(
                    "file list round-trips exactly",
                    fileList != nullptr && fileList->paths == std::vector(files.begin(), files.end())));
                static_cast<void>(context.expectTrue(
                    "RGBA image round-trips exactly",
                    image != nullptr && image->size == Desktop::Types::PixelSize{2, 2} && image->rgba8 == std::vector(pixels.begin(), pixels.end())));
                static_cast<void>(context.expectTrue(
                    "custom identity and bytes round-trip exactly",
                    custom != nullptr && custom->formatName == customName && custom->bytes == std::vector(customBytes.begin(), customBytes.end())));
            }
        }
    }

    struct EffectCase
    {
        DD::Effect source;
        DD::Effect target;
        DD::Effect preferred;
        DD::Effect expected;
        std::string_view label;
    };
    constexpr std::array effectCases{
        EffectCase{DD::Effect::Copy, DD::Effect::Copy, DD::Effect::Copy, DD::Effect::Copy, "Copy"},
        EffectCase{DD::Effect::Move, DD::Effect::Move, DD::Effect::Move, DD::Effect::Move, "Move"},
        EffectCase{DD::Effect::Link, DD::Effect::Link, DD::Effect::Link, DD::Effect::Link, "Link"},
        EffectCase{DD::Effect::Copy | DD::Effect::Move, allEffects, DD::Effect::Move, DD::Effect::Move, "Copy|Move preferred Move"},
        EffectCase{DD::Effect::Copy | DD::Effect::Link, allEffects, DD::Effect::Link, DD::Effect::Link, "Copy|Link preferred Link"},
        EffectCase{DD::Effect::Move | DD::Effect::Link, allEffects, DD::Effect::Link, DD::Effect::Link, "Move|Link preferred Link"},
        EffectCase{allEffects, allEffects, DD::Effect::Move, DD::Effect::Move, "Copy|Move|Link preferred Move"},
        EffectCase{DD::Effect::Copy, DD::Effect::Move, DD::Effect::Move, DD::Effect::None, "no intersection"}};

    for (const EffectCase &effectCase : effectCases)
    {
        std::array<Transfer::FormatView, 1> formats{{Transfer::FormatView::text()}};
        std::array<DD::RegionDescription, 1> effectRegion{
            {{DD::RegionId{1}, std::nullopt, formats, DD::EffectPolicy::allow(effectCase.target, effectCase.preferred)}}};
        static_cast<void>(context.expectTrue(std::format("{} target policy applies", effectCase.label), target.setRegions(effectRegion).ok()));
        capture.clear(target);
        const ManualDragDropRun run = runManualDragDropSource(
            context,
            sourceWindow,
            &targetWindow,
            &target,
            &capture,
            std::format("effect matrix {}", effectCase.label),
            std::format("Hold LEFT in the source, drag to the blue target, and release. Expected effect: {}.", effectCase.label),
            DD::Description{textItem, DD::EffectPolicy::source(effectCase.source), DD::TriggerButton::Left});

        if (run.answer == ManualAnswer::Yes && run.attempted)
        {
            const DD::Outcome expectedOutcome = effectCase.expected == DD::Effect::None ? DD::Outcome::Cancelled : DD::Outcome::Dropped;
            static_cast<void>(context.expectEq(std::format("{} outcome", effectCase.label), expectedOutcome, run.result.outcome));
            static_cast<void>(context.expectEq(std::format("{} negotiated effect", effectCase.label), effectCase.expected, run.result.effect));
        }
    }

    static_cast<void>(target.setRegions(regions));

    for (const DD::TriggerButton button : {DD::TriggerButton::Right, DD::TriggerButton::Middle})
    {
        capture.clear(target);
        const std::string_view buttonName = button == DD::TriggerButton::Right ? "RIGHT" : "MIDDLE";
        const ManualDragDropRun run = runManualDragDropSource(
            context,
            sourceWindow,
            &targetWindow,
            &target,
            &capture,
            std::format("{} trigger", buttonName),
            std::format("Hold {} in the source, drag to either target region, then release that same button.", buttonName),
            DD::Description{textItem, DD::EffectPolicy::source(allEffects), button});

        if (run.answer == ManualAnswer::Yes && run.attempted)
        {
            static_cast<void>(context.expectEq(std::format("{} trigger drops", buttonName), DD::Outcome::Dropped, run.result.outcome));
        }
    }

    capture.clear(target);
    const ManualDragDropRun cancellation = runManualDragDropSource(
        context,
        sourceWindow,
        &targetWindow,
        &target,
        &capture,
        "Escape and leave cancellation",
        "Hold LEFT in the source, enter and leave the target, press and release an unrelated mouse button, then press Escape while LEFT remains "
        "held.",
        DD::Description{textItem, DD::EffectPolicy::source(allEffects), DD::TriggerButton::Left});

    if (cancellation.answer == ManualAnswer::Yes && cancellation.attempted)
    {
        static_cast<void>(context.expectEq("Escape reports Cancelled", DD::Outcome::Cancelled, cancellation.result.outcome));
        static_cast<void>(context.expectEq("cancelled drag reports no effect", DD::Effect::None, cancellation.result.effect));
        static_cast<void>(context.expectTrue("cancelled target traversal enters", capture.entered > 0));
        static_cast<void>(context.expectTrue("leaving target queues Left", capture.left > 0));
        static_cast<void>(context.expectEq("cancellation queues no drop", std::size_t{0}, capture.dropped));
    }

    for (const std::string_view modifier : {"Ctrl", "Shift", "Alt"})
    {
        capture.clear(target);
        const ManualDragDropRun run = runManualDragDropSource(
            context,
            sourceWindow,
            &targetWindow,
            &target,
            &capture,
            std::format("{} modifier independence", modifier),
            std::format("Hold LEFT in the source, keep {} held, drop in green Region 2, then release {}.", modifier, modifier),
            DD::Description{textItem, DD::EffectPolicy::source(allEffects), DD::TriggerButton::Left});

        if (run.answer == ManualAnswer::Yes && run.attempted)
        {
            static_cast<void>(context.expectEq(std::format("{} does not override target preference", modifier), DD::Effect::Move, run.result.effect));
        }
    }

    const auto observeForeign = [&]
    {
        capture.drain(target);
        paintManualValidationSurface(targetWindow, ManualSurfaceLayout::DragDropTarget);

        if (capture.lastDrop)
        {
            for (const auto &item : capture.lastDrop->payload)
            {
                if (const auto *image = std::get_if<Transfer::Image>(&item))
                {
                    paintManualImagePreview(targetWindow, *image);
                }
            }
        }
    };
    const auto inbound = [&](std::string_view name, std::string_view instruction, const Transfer::Item *expected = nullptr)
    {
        capture.clear(target);
        const ManualAnswer answer = recordManualCheck(context, targetWindow, name, instruction, observeForeign, ManualSurfaceLayout::DragDropTarget);

        if (answer == ManualAnswer::Yes)
        {
            static_cast<void>(context.expectEq(std::format("{} yields one complete drop", name), std::size_t{1}, capture.dropped));

            if (expected != nullptr)
            {
                static_cast<void>(context.expectTrue(
                    std::format("{} matches exact controlled payload", name),
                    capture.lastDrop && std::ranges::find(capture.lastDrop->payload, *expected) != capture.lastDrop->payload.end()));
            }
        }
    };
    const Transfer::Item expectedSingle = Transfer::FileList{{singleFile}};
    const Transfer::Item expectedMultiple = Transfer::FileList{{unicodeFile, singleFile}};
    const Transfer::Item expectedText = Transfer::Text{std::string(unicodeText)};
    const Transfer::Item expectedCustom = Transfer::CustomData{std::string(customName), std::vector(customBytes.begin(), customBytes.end())};
    inbound(
        "Explorer single file",
        std::format("From Explorer, drop {}. The exact imported path will be checked.", singleFile.string()),
        &expectedSingle);
    inbound(
        "Explorer multiple Unicode files",
        std::format(
            "In {}, sort by Name ascending, select single-file.txt and the Unicode filename together and drop them. Exact paths in the visible order "
            "will be checked.",
            fixtures.path().string()),
        &expectedMultiple);
    inbound(
        "foreign strict Unicode text",
        std::format(
            "Open {} in a browser. Select just the GameWIP strict Unicode paragraph and drag it into the target. Exact UTF-8 bytes will be checked.",
            textPage.string()),
        &expectedText);
    inbound(
        "foreign DIB image",
        "Drag a known oriented/color test image from a DIB-capable application and verify orientation, RGBA channels, and supported alpha after the "
        "drop.");
    inbound(
        "controlled custom provider",
        "Using a controlled provider for GameWIP.Validation.NativeDragDrop, send the exact documented bytes. Answer skip if that provider is "
        "unavailable. Expected bytes: 47 57 00 58 3A FF.",
        &expectedCustom);

    capture.clear(target);
    const ManualAnswer malformed = recordManualCheck(
        context,
        targetWindow,
        "malformed foreign IDataObject",
        "Use the controlled malformed IDataObject provider to exercise malformed text, HDROP, DIB, FORMATETC, and partial-materialization cases. "
        "Answer yes only if every case was rejected without a drop or crash; answer skip if unavailable.",
        observeForeign,
        ManualSurfaceLayout::DragDropTarget);

    if (malformed == ManualAnswer::Yes)
    {
        static_cast<void>(context.expectEq("malformed provider queues no successful drop", std::size_t{0}, capture.dropped));
    }

    const auto outbound =
        [&](std::string_view name, std::string_view instruction, std::span<const Transfer::ItemView> items, DD::EffectPolicy effects)
    {
        const ManualDragDropRun run = runManualDragDropSource(
            context,
            sourceWindow,
            nullptr,
            nullptr,
            nullptr,
            name,
            instruction,
            DD::Description{items, effects, DD::TriggerButton::Left});

        if (run.answer == ManualAnswer::Yes && run.attempted)
        {
            static_cast<void>(context.expectEq(std::format("{} reports Dropped", name), DD::Outcome::Dropped, run.result.outcome));

            if (name == "GameWIP files to Explorer")
            {
                for (const auto &path : files)
                {
                    const auto original = TestSupport::readTextFile(path);
                    const auto copy = TestSupport::readTextFile(copyDirectory / path.filename());
                    static_cast<void>(context.expectTrue(
                        "Explorer drag copies exact file contents",
                        original.status.ok() && copy.status.ok() && original.text == copy.text));
                }
            }
        }
    };
    const std::array<Transfer::ItemView, 1> fileItem{{Transfer::FileListView{files}}};
    const std::array<Transfer::ItemView, 1> imageItem{{Transfer::ImageView::packed({2, 2}, pixels)}};
    const std::array<Transfer::ItemView, 1> customItem{{Transfer::CustomView{customName, customBytes}}};
    outbound(
        "GameWIP files to Explorer",
        std::format(
            "Hold LEFT in the source and drag to {} in Explorer. Verify Copy is reported and README.md/CMakeLists.txt arrive.",
            copyDirectory.string()),
        fileItem,
        DD::EffectPolicy::source(DD::Effect::Copy));
    outbound(
        "GameWIP Unicode text to editor",
        "Hold LEFT in the source and drag into a compatible text editor. Verify the exact accented, Japanese, punctuation, and emoji text.",
        textItem,
        DD::EffectPolicy::source(allEffects));
    outbound(
        "GameWIP image to foreign target",
        "Hold LEFT in the source and drag to a DIB-capable image target. Verify orientation, R/G/B order, and supported alpha.",
        imageItem,
        DD::EffectPolicy::source(allEffects));
    outbound(
        "GameWIP custom data to diagnostic consumer",
        "Hold LEFT in the source and drag to a controlled consumer that requests the custom format repeatedly. Answer skip if unavailable.",
        customItem,
        DD::EffectPolicy::source(allEffects));

    capture.clear(target);
    const ManualDragDropRun modalGeometry = runManualDragDropSource(
        context,
        sourceWindow,
        &targetWindow,
        &target,
        &capture,
        "modal presentation and interactive geometry",
        "Hold LEFT in the source and keep the drag active. Move or resize the target with Windows keyboard/shell controls where practical, "
        "cross both target regions, then drop. Answer yes only if geometry/presentation stayed coherent with no deadlock.",
        DD::Description{textItem, DD::EffectPolicy::source(allEffects), DD::TriggerButton::Left});

    if (modalGeometry.answer == ManualAnswer::Yes && modalGeometry.attempted)
    {
        static_cast<void>(context.expectEq("modal geometry scenario drops", DD::Outcome::Dropped, modalGeometry.result.outcome));
    }

    static_cast<void>(context.expectTrue("full target closes before lightweight mode", target.close().ok()));
    static_cast<void>(context.expectTrue("lightweight file drop enables after full target", targetWindow.setFileDropEnabled(true).ok()));
    static_cast<void>(context.expectEq(
        "full target conflicts with lightweight mode",
        ErrorCode::ResourceBusy,
        target.open(targetWindow, DD::TargetDescription{regions}, DD::QueueConfiguration::owned(8)).code));
    static_cast<void>(context.expectTrue("lightweight file drop disables", targetWindow.setFileDropEnabled(false).ok()));
    static_cast<void>(context.expectTrue(
        "full target reopens after lightweight mode",
        target.open(targetWindow, DD::TargetDescription{regions}, DD::QueueConfiguration::owned(8)).ok()));
    static_cast<void>(target.close());
    static_cast<void>(targetWindow.close());
    static_cast<void>(sourceWindow.close());
}
