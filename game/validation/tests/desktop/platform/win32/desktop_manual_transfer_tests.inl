/// @file desktop_manual_transfer_tests.inl
/// @brief Shell and Window file-drop manual validation cases.

/// ------------------------------------------------------------
/// Shell launch probe
/// ------------------------------------------------------------

[[nodiscard]] std::filesystem::path shellLaunchReceiptPath(std::string_view token)
{
    return std::filesystem::temp_directory_path() / std::format("GameWIP-shell-launch-{}.txt", token);
}

[[nodiscard]] std::filesystem::path shellGenericLaunchReceiptPath(const std::filesystem::path &targetPath)
{
    return targetPath.parent_path() / ".gamewip-generic-launch-receipt.txt";
}

/// @brief Confirms a controlled shell launch without starting the full validation runner again.
[[nodiscard]] int runShellLaunchProbeChild(int argc, char **argv)
{
    constexpr std::string_view prefix = "--desktop-shell-launch-token=";
    constexpr std::string_view receiptPrefix = "--desktop-shell-launch-receipt=";
    constexpr std::string_view targetPrefix = "--desktop-shell-launch-expected-target=";
    const auto arguments = GameWIP::Validation::processArguments(argc, argv);
    std::string_view expectedTarget;
    std::filesystem::path receiptPath;

    for(const char *argument : arguments)
    {
        if(argument != nullptr && std::string_view{argument}.starts_with(targetPrefix))
        {
            expectedTarget = std::string_view{argument}.substr(targetPrefix.size());
        }

        if(argument != nullptr && std::string_view{argument}.starts_with(receiptPrefix))
        {
            receiptPath = std::filesystem::path(std::string_view{argument}.substr(receiptPrefix.size()));
        }
    }

    for(const char *argument : arguments)
    {
        if(argument == nullptr || !std::string_view{argument}.starts_with(prefix))
        {
            continue;
        }
        const std::string_view token = std::string_view{argument}.substr(prefix.size());

        if(token.empty() || token.size() > 64 ||
           !std::ranges::all_of(
               token,
               [](char value)
               {
                   return (value >= '0' && value <= '9') || value == '-';
               }))
        {
            return 2;
        }

        if(!expectedTarget.empty())
        {
            std::string receivedArguments;

            for(const char *received : arguments)
            {
                receivedArguments += received == nullptr ? "<null>" : received;
                receivedArguments.push_back('\n');
            }
            const std::filesystem::path expectedArgumentsPath = receiptPath.empty() ? shellLaunchReceiptPath(token) : receiptPath;
            static_cast<void>(TestSupport::writeTextFile(expectedArgumentsPath.string() + ".arguments", receivedArguments));

            if(arguments.size() < 5 || arguments.back() == nullptr || std::string_view{arguments.back()} != expectedTarget)
            {
                return 4;
            }
        }

        if(receiptPath.empty())
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
    if(!beginManualSuite(context, options, "Window files and shell behavior"))
    {
        return;
    }

    const auto capabilitiesResult = Desktop::getCapabilities();
    const Desktop::Types::Capabilities capabilities = capabilitiesResult.value_or({});
    Desktop::Types::Description description;
    description.fileDropEnabled = includeWindow && capabilities.supports(Desktop::Types::Capability::FileDrop);
    Desktop::Window window;

    if(!openManualWindow(context, window, "GameWIP file and shell validation", description))
    {
        return;
    }

    // Validate Window file-drop and icon behavior.
    if(includeWindow)
    {
        if(capabilities.supports(Desktop::Types::Capability::FileDrop))
        {
            const auto runDrop = [&](std::string_view name, std::string_view instruction, std::size_t minimumPaths)
            {
                window.clearEvents();
                std::size_t groupedEvents = 0;
                std::size_t pathCount = 0;
                const auto observeDrops = [&]
                {
                    Desktop::Types::Event event;

                    while(window.popEvent(event))
                    {
                        if(const auto *drop = event.getIf<Desktop::Types::Events::FilesDropped>())
                        {
                            ++groupedEvents;
                            pathCount += drop->paths.size();
                            std::string pathList;

                            for(const auto &path : drop->paths)
                            {
                                const std::u8string utf8 = path.generic_u8string();

                                if(!pathList.empty())
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

                            if(manualStatusWindow != nullptr)
                            {
                                manualStatusWindow->setObservation(observation);
                            }
                            static_cast<void>(window.setTitle(std::format("Drop received: {} path(s)", pathCount)));
                        }
                    }
                };
                static_cast<void>(window.setTitle("Waiting for file drop..."));

                if(manualStatusWindow != nullptr)
                {
                    manualStatusWindow->setObservation("Waiting for FilesDroppedEvent.");
                }
                const TestSupport::Types::Reporting::ManualAnswer answer = recordManualCheck(context, window, name, instruction, observeDrops);

                if(answer != TestSupport::Types::Reporting::ManualAnswer::Yes)
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

            if(disabledAnswer == TestSupport::Types::Reporting::ManualAnswer::Yes)
            {
                static_cast<void>(
                    context.expectFalse("disabled file drops queue no event", consumeEventOfType<Desktop::Types::Events::FilesDropped>(window)));
            }
        }
        else
        {
            context.skip("Window file drops", "backend does not advertise FileDrop");
        }

        if(capabilities.supports(Desktop::Types::Capability::WindowIcon))
        {
            const auto makeIcon = [](std::uint32_t extent)
            {
                std::vector<std::byte> pixels(static_cast<std::size_t>(extent) * extent * 4u);

                for(std::uint32_t y = 0; y < extent; ++y)
                {
                    for(std::uint32_t x = 0; x < extent; ++x)
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

    // Exercise shell integrations through one event queue.
    if(includeShell)
    {
        const auto shellCapabilityResult = Desktop::Shell::getCapabilities();

        if(!requireManualStatus(context, "shell capability query", shellCapabilityResult.status))
        {
            return;
        }
        const auto &shellCapabilities = *shellCapabilityResult;
        Desktop::ShellEventQueue shellQueue;

        if(requireManualStatus(context, "shell event queue setup", shellQueue.open(Desktop::Types::Shell::QueueConfiguration::owned(64))))
        {
            bool taskbarCommandSeen = false;
            bool trayCommandSeen = false;
            std::size_t shellEventCount = 0;
            std::string shellObservation = "Waiting for shell interaction.";
            const auto drainShellEvents = [&]
            {
                Desktop::Types::Shell::Event event;

                while(shellQueue.popEvent(event))
                {
                    ++shellEventCount;

                    if(const auto *trayActivation = event.getIf<Desktop::Types::Shell::Events::TrayActivated>())
                    {
                        shellObservation =
                            std::format("Tray activation: icon={} kind={}", trayActivation->iconId.value, static_cast<int>(trayActivation->kind));
                    }
                    else if(const auto *trayCommand = event.getIf<Desktop::Types::Shell::Events::TrayCommandInvoked>())
                    {
                        trayCommandSeen = trayCommandSeen || trayCommand->commandId.value == 702;
                        shellObservation = std::format("Tray command: icon={} command={}", trayCommand->iconId.value, trayCommand->commandId.value);
                    }
                    else if(const auto *taskbarCommand = event.getIf<Desktop::Types::Shell::Events::TaskbarThumbnailButtonInvoked>())
                    {
                        taskbarCommandSeen = taskbarCommandSeen || taskbarCommand->commandId.value == 701;
                        shellObservation =
                            std::format("Taskbar command: window={} command={}", taskbarCommand->windowId.value, taskbarCommand->commandId.value);
                    }
                    else if(const auto *notification = event.getIf<Desktop::Types::Shell::Events::NotificationActivated>())
                    {
                        shellObservation = std::format("Notification activation: id={}", notification->notificationId.value);
                    }
                }

                if(manualStatusWindow != nullptr)
                {
                    manualStatusWindow->setObservation(std::format("Shell events received={} {}", shellEventCount, shellObservation));
                }
            };

            // Check taskbar progress and command callbacks.
            if(shellCapabilities.supports(Desktop::Types::Shell::Capability::TaskbarThumbnailButtons))
            {
                Desktop::Types::Taskbar::ThumbnailButton thumbnailButton{{701}, "Shell manual command"};
                Desktop::Types::Taskbar::Description taskbarDescription;
                taskbarDescription.progress = Desktop::Types::Shell::Progress{Desktop::Types::Shell::ProgressState::Normal, 0.35};
                taskbarDescription.thumbnailButtons = std::span{&thumbnailButton, 1};
                Desktop::TaskbarItem taskbar;
                const IO::Types::Status taskbarStatus = taskbar.open(window, taskbarDescription, shellQueue);

                if(taskbarStatus.ok())
                {
                    using Desktop::Types::Shell::Progress;
                    using Desktop::Types::Shell::ProgressState;
                    const std::array progressCases{
                        std::pair<std::string_view, Progress>{"normal 35 percent", {ProgressState::Normal, 0.35}},
                        std::pair<std::string_view, Progress>{"indeterminate", {ProgressState::Indeterminate, 0.0}},
                        std::pair<std::string_view, Progress>{"paused 65 percent", {ProgressState::Paused, 0.65}},
                        std::pair<std::string_view, Progress>{"error 65 percent", {ProgressState::Error, 0.65}},
                        std::pair<std::string_view, Progress>{"normal 100 percent", {ProgressState::Normal, 1.0}}};

                    for(const auto &[label, progress] : progressCases)
                    {
                        if(requireManualStatus(context, std::format("taskbar progress {} applies", label), taskbar.setProgress(progress)))
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

                    if(answer == TestSupport::Types::Reporting::ManualAnswer::Yes)
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

            // Check tray publication and menu callbacks.
            if(shellCapabilities.supports(Desktop::Types::Shell::Capability::TrayMenu))
            {
                const IO::Types::Status trayStatus = tray.open(trayDescription, shellQueue);

                if(trayStatus.ok())
                {
                    const auto answer = recordManualCheck(
                        context,
                        window,
                        "tray icon and recursive menu",
                        "Hover the cyan tray icon and verify the GameWIP shell manual validation tooltip. Right-click it, choose Shell manual "
                        "command, and answer yes only if diagnostics show command=702.",
                        drainShellEvents);

                    if(answer == TestSupport::Types::Reporting::ManualAnswer::Yes)
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

            // Check notification publication and dismissal.
            Desktop::NotificationCenter notificationCenter;

            if(shellCapabilities.supports(Desktop::Types::Shell::Capability::Notifications))
            {
                const IO::Types::Status notificationOpenStatus = notificationCenter.open(shellQueue);

                if(notificationOpenStatus.ok())
                {
                    Desktop::Types::Notifications::Description notificationDescription;
                    notificationDescription.title = "GameWIP shell manual validation";
                    notificationDescription.body = "Verify the notification appearance and dismissal.";
                    const auto published = notificationCenter.publish(notificationDescription);

                    if(published.status.ok())
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

            // Verify shell-launched arguments and registered handlers.
            std::array<wchar_t, 32768> executableText{};
            const DWORD executableLength = GetModuleFileNameW(nullptr, executableText.data(), static_cast<DWORD>(executableText.size()));

            if(!context.expectTrue("shell launch executable resolves", executableLength > 0 && executableLength < executableText.size()))
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

                if(receipt.status.ok() && receipt.text == launchToken)
                {
                    return true;
                }

                if(genericLaunchReceipt.empty())
                {
                    return false;
                }
                const auto genericReceipt = TestSupport::readTextFile(genericLaunchReceipt);

                if(genericReceipt.status.ok() && !genericReceipt.text.empty())
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
                for(int attempt = 0; attempt < 20; ++attempt)
                {
                    if(launchReceived())
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

                if(manualStatusWindow != nullptr)
                {
                    manualStatusWindow->setObservation(
                        launchReceived() ? "Controlled shell launch receipt received." : "Waiting for controlled shell launch receipt.");
                }
            };
            const Desktop::Types::JumpLists::Task jumpTask{"GameWIP shell manual validation", "Desktop shell integration", launchAction};
            const std::array jumpTasks{jumpTask};
            Desktop::Types::JumpLists::Description jumpListDescription;
            jumpListDescription.tasks = jumpTasks;

            if(shellCapabilities.supports(Desktop::Types::Shell::Capability::JumpLists))
            {
                const IO::Types::Status jumpListStatus = Desktop::JumpLists::publish(jumpListDescription);

                if(jumpListStatus.ok())
                {
                    const auto answer = recordManualCheck(
                        context,
                        window,
                        "jump-list publication",
                        "Open this application's taskbar context menu and verify the GameWIP shell manual validation task appears and launches this "
                        "validation executable. The diagnostics must show a controlled launch receipt.",
                        observeLaunch);

                    if(answer == TestSupport::Types::Reporting::ManualAnswer::Yes)
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
            const TestSupport::ScopedTemporaryDirectory registrationFixtures("desktop-manual-shell-registration");

            if(!context.expectTrue("shell registration fixtures create", registrationFixtures.status().ok()))
            {
                return;
            }
            const auto fileProbe = registrationFixtures.path() / ("probe with spaces" + extension);
            const auto uriProbe = registrationFixtures.path() / "uri-launch.html";
            genericLaunchReceipt = shellGenericLaunchReceiptPath(fileProbe);
            receiptCleanup.genericPath = genericLaunchReceipt;
            static_cast<void>(TestSupport::removeIfExists(genericLaunchReceipt));

            if(!context.expectTrue(
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

            if(shellCapabilities.supports(Desktop::Types::Shell::Capability::FileAssociationRegistration))
            {
                const auto registration = Desktop::Registration::registerFileExtension(fileRegistration);

                if(registration.status.ok())
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

                    if(answer == TestSupport::Types::Reporting::ManualAnswer::Yes)
                    {
                        const bool launched = waitForLaunch();

                        if(!launched)
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

            if(shellCapabilities.supports(Desktop::Types::Shell::Capability::UriSchemeRegistration))
            {
                const auto registration = Desktop::Registration::registerUriScheme(uriRegistration);

                if(registration.status.ok())
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

                    if(answer == TestSupport::Types::Reporting::ManualAnswer::Yes)
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

    if(!includeWindow)
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

    for(const bool resizable : {false, true})
    {
        for(const bool maximizable : {false, true})
        {
            static_cast<void>(window.setResizable(true));
            Desktop::Types::Controls controls = window.controls();
            controls.maximizable = maximizable;
            const IO::Types::Status controlsStatus = window.setControls(controls);
            const IO::Types::Status resizeStatus = window.setResizable(resizable);

            if(!resizable && maximizable)
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

    if(openManualWindow(context, owned, "GameWIP owned taskbar validation", ownedDescription))
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
