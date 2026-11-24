/// @file desktop_manual_shell_tests.inl
/// @brief Manual validation for native shell integrations.

/// ------------------------------------------------------------
/// Shell integration behavior
/// ------------------------------------------------------------

[[nodiscard]] bool testManualShellIntegrations(TestSupport::Context &context, Desktop::Window &window, bool includeShell)
{
    // Query shell capabilities and open one event queue for the checks.
    if(includeShell)
    {
        const auto shellCapabilityResult = Desktop::Shell::getCapabilities();

        if(!requireManualStatus(context, "shell capability query", shellCapabilityResult.status))
        {
            return false;
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
                return false;
            }
            const std::filesystem::path executable{executableText.data()};
            const std::string launchToken = std::format("{}-{}", GetCurrentProcessId(), GetTickCount64());
            const std::string tokenArgument = std::format("--desktop-shell-launch-token={}", launchToken);
            const std::filesystem::path launchReceipt = shellLaunchReceiptPath(launchToken);
            const std::string launchReceiptText = GameWIP::Test::Internal::pathToUtf8(context, launchReceipt, "shell receipt path converts to UTF-8");

            if(launchReceiptText.empty())
            {
                return false;
            }

            const std::string receiptArgument = std::format("--desktop-shell-launch-receipt={}", launchReceiptText);
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

                // Retry transient reads while the launched child closes its receipt.
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
                return false;
            }
            const auto fileProbe = registrationFixtures.path() / ("probe with spaces" + extension);
            const auto uriProbe = registrationFixtures.path() / "uri-launch.html";
            const std::string fileProbeText =
                GameWIP::Test::Internal::pathToUtf8(context, fileProbe, "file association fixture path converts to UTF-8");
            const std::string uriProbeText = GameWIP::Test::Internal::pathToUtf8(context, uriProbe, "URI fixture path converts to UTF-8");

            if(fileProbeText.empty() || uriProbeText.empty())
            {
                return false;
            }

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
                return false;
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
                            fileProbeText),
                        observeLaunch);

                    if(answer == TestSupport::Types::Reporting::ManualAnswer::Yes)
                    {
                        const bool launched = waitForLaunch();

                        if(!launched)
                        {
                            const auto receipt = TestSupport::readTextFile(genericLaunchReceipt);
                            const std::string genericLaunchReceiptText =
                                GameWIP::Test::Internal::pathToUtf8(context, genericLaunchReceipt, "generic launch receipt path converts to UTF-8");

                            if(genericLaunchReceiptText.empty())
                            {
                                return false;
                            }

                            context.fail(
                                "file association launches controlled helper",
                                std::format(
                                    "generic receipt was not observed at '{}' (status={}, bytes={})",
                                    genericLaunchReceiptText,
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
                            uriProbeText,
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
    return true;
}
