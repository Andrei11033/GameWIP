/// @file desktop_manual_fullscreen_tests.inl
/// @brief Fullscreen and topology manual validation.

/// ------------------------------------------------------------
/// Fullscreen transitions and display topology
/// ------------------------------------------------------------

/// @brief Exercises selected fullscreen transitions, monitor movement, and live topology recovery.
void testManualFullscreenAndTopology(
    TestSupport::Context &context,
    const GameWIP::Test::DesktopTestOptions &options,
    ManualFullscreenSections sections)
{
    // Start the suite and confirm a usable display target.
    if(!beginManualSuite(context, options, "Window fullscreen and display topology"))
    {
        return;
    }

    const auto capabilitiesResult = Desktop::getCapabilities();
    if(!capabilitiesResult.ok())
    {
        context.fail("Window fullscreen capabilities", manualStatusDetails(capabilitiesResult.status()));
        return;
    }
    const Desktop::Types::Capabilities &capabilities = capabilitiesResult.value();
    const auto monitors = Desktop::Display::getMonitors();

    if(!monitors.ok() || monitors.value().empty())
    {
        const GameWIP::Status::Types::Diagnostic *diagnostic = monitors.status().diagnostic();
        const std::string_view detail =
            diagnostic != nullptr && !diagnostic->message.empty() ? diagnostic->message : GameWIP::Status::Types::codeName(monitors.status().code());
        context.fail("fullscreen monitor enumeration", detail);
        return;
    }

    // Keep one window across transitions so restoration can be compared.
    Desktop::Window window;

    if(!openManualWindow(context, window, "GameWIP fullscreen validation"))
    {
        return;
    }
    const Desktop::Types::ScreenPosition savedPosition = window.clientPosition();
    const Desktop::Types::LogicalSize savedSize = window.clientSize();
    const auto disconnectableMonitor = [] -> std::optional<Desktop::Types::Display::MonitorId>
    {
        const auto connected = Desktop::Display::getMonitors();

        if(!connected.ok())
        {
            return std::nullopt;
        }

        const auto secondary = std::ranges::find_if(
            connected.value(),
            [](const Desktop::Types::Display::Info &monitor)
            {
                return !monitor.primary;
            });
        return secondary == connected.value().end() ? std::nullopt : std::optional{secondary->id};
    };

    // Check borderless coverage and windowed placement restoration.
    if(sections.borderless)
    {
        for(std::size_t index = 0; index < monitors.value().size(); ++index)
        {
            const Desktop::Types::Display::Info &monitor = monitors.value()[index];
            const Desktop::Types::WindowModeRequest borderless = Desktop::Types::WindowModes::borderlessFullscreen(monitor.id);
            const GameWIP::Status::Types::Result<void> enterStatus = setManualModeWithDiagnostics(context, window, "borderless-enter", borderless);

            if(requireManualStatus(context, "borderless fullscreen enters", enterStatus))
            {
                const ManualNativeWindowState native = manualNativeWindowState(window);
                static_cast<void>(context.expectTrue("borderless native HWND query succeeds", native.valid));
                static_cast<void>(context.expectTrue("borderless native popup style applies", native.popupStyle));
                static_cast<void>(context.expectTrue("borderless native bounds match monitor", native.fullscreenBounds));
                static_cast<void>(context.expectTrue("borderless Window remains taskbar eligible", native.taskbarEligible));
                recordManualCheck(
                    context,
                    window,
                    "borderless fullscreen monitor",
                    std::format(
                        "The blue validation surface and cyan inset border must touch every display edge behind the diagnostics. The native section "
                        "must show popupStyle=true and fullscreenBounds=true. Press the Windows key: a separate GameWIP taskbar button must be "
                        "present, or use Alt+Tab to verify the Window is listed. On monitor {}/{} ({}), does it exactly cover the display and "
                        "remain switchable without changing its display mode?",
                        index + 1,
                        monitors.value().size(),
                        monitor.name));
                const GameWIP::Status::Types::Result<void> leaveStatus = setManualModeWithDiagnostics(context, window, "borderless-leave", {});
                static_cast<void>(requireManualStatus(context, "borderless fullscreen leaves", leaveStatus));
            }
            else
            {
                context.skip("borderless fullscreen monitor", "borderless-mode setup failed; see preceding status");
            }
        }

        const Desktop::Types::LogicalSize restoredSize = window.clientSize();

        if(savedSize == restoredSize)
        {
            context.pass("windowed size restores after borderless");
        }
        else
        {
            context.fail(
                "windowed size restores after borderless",
                std::format("expected {}x{}, got {}x{}", savedSize.width, savedSize.height, restoredSize.width, restoredSize.height));
        }
        recordManualCheck(
            context,
            window,
            "windowed placement restoration",
            std::format("After fullscreen transitions, did the Window restore its saved placement near ({}, {})?", savedPosition.x, savedPosition.y));
    }

    // Check exact exclusive modes and recovery when their target disappears.
    if(sections.exclusive || sections.topology)
    {
        if(capabilities.supports(Desktop::Types::Capability::ExclusiveFullscreen))
        {
            const std::optional<Desktop::Types::Display::MonitorId> topologyMonitor = sections.topology ? disconnectableMonitor() : std::nullopt;
            const Desktop::Types::Display::MonitorId monitor = topologyMonitor.value_or(window.currentMonitor());
            const auto currentMode = Desktop::Display::getCurrentMode(monitor);
            const auto availableModes = Desktop::Display::getModes(monitor);

            if(currentMode.ok() && availableModes.ok() && !availableModes.value().empty())
            {
                const auto selected = std::ranges::min_element(
                    availableModes.value(),
                    {},
                    [&](const Desktop::Types::Display::Mode &mode)
                    {
                        const std::uint64_t resolutionPenalty = mode.resolution == currentMode.value().resolution ? 0 : std::uint64_t{1} << 48U;
                        const std::uint64_t depthPenalty = mode.bitsPerPixel == currentMode.value().bitsPerPixel ? 0 : std::uint64_t{1} << 40U;
                        const std::uint64_t interlacePenalty = mode.interlaced == currentMode.value().interlaced ? 0 : std::uint64_t{1} << 39U;
                        const std::uint64_t refreshDifference = mode.refreshRateMillihertz > currentMode.value().refreshRateMillihertz
                                                                    ? mode.refreshRateMillihertz - currentMode.value().refreshRateMillihertz
                                                                    : currentMode.value().refreshRateMillihertz - mode.refreshRateMillihertz;
                        return resolutionPenalty + depthPenalty + interlacePenalty + refreshDifference;
                    });
                const Desktop::Types::WindowModeRequest exclusive = Desktop::Types::WindowModes::exclusiveFullscreen(monitor, *selected);

                if(manualStatusWindow != nullptr)
                {
                    manualStatusWindow->setObservation(
                        std::format(
                            "Requesting enumerated exact mode {}x{} @ {:.3f} Hz, {} bpp, interlaced={}.",
                            selected->resolution.width,
                            selected->resolution.height,
                            static_cast<double>(selected->refreshRateMillihertz) / 1000.0,
                            selected->bitsPerPixel,
                            selected->interlaced));
                }

                if(sections.exclusive)
                {
                    const GameWIP::Status::Types::Result<void> enterStatus = setManualModeWithDiagnostics(context, window, "exclusive-enter", exclusive);

                    if(requireManualStatus(context, "exclusive fullscreen enters", enterStatus))
                    {
                        const ManualNativeWindowState native = manualNativeWindowState(window);
                        static_cast<void>(context.expectTrue("exclusive native HWND query succeeds", native.valid));
                        static_cast<void>(context.expectTrue("exclusive native popup style applies", native.popupStyle));
                        static_cast<void>(context.expectTrue("exclusive native bounds match active monitor", native.fullscreenBounds));
                        static_cast<void>(context.expectTrue("exclusive Window remains taskbar eligible", native.taskbarEligible));
                        bool sawExclusiveActive = window.focused() && !window.fullscreenState().suspended;
                        bool sawExclusiveSuspended = window.fullscreenState().suspended;
                        recordManualCheck(
                            context,
                            window,
                            "exclusive fullscreen activation cycle",
                            "Alt+Tab to the blue validation surface, back to the terminal, to the validation Window once more, and finally back "
                            "to the terminal to answer. While focused it must cover the display; while back at the terminal, suspended=true and "
                            "a windowed-sized surface are expected. The test records both states automatically. Does that activation cycle "
                            "behave correctly?",
                            [&]
                            {
                                const Desktop::Types::FullscreenState liveFullscreen = window.fullscreenState();
                                sawExclusiveActive = sawExclusiveActive || (window.focused() && !liveFullscreen.suspended);
                                sawExclusiveSuspended = sawExclusiveSuspended || liveFullscreen.suspended;

                                if(manualStatusWindow != nullptr)
                                {
                                    manualStatusWindow->setObservation(
                                        std::format(
                                            "Activation evidence: activeSeen={} suspendedSeen={} (finish in the terminal to answer).",
                                            sawExclusiveActive,
                                            sawExclusiveSuspended));
                                }
                            });
                        static_cast<void>(context.expectTrue("exclusive activation state is observed", sawExclusiveActive));
                        static_cast<void>(context.expectTrue("exclusive suspension state is observed", sawExclusiveSuspended));
                        const GameWIP::Status::Types::Result<void> leaveStatus = setManualModeWithDiagnostics(context, window, "exclusive-leave", {});

                        if(requireManualStatus(context, "exclusive fullscreen leaves", leaveStatus))
                        {
                            recordManualCheck(
                                context,
                                window,
                                "exclusive display restoration",
                                "The diagnostics must show mode=0 and the original geometry. Was the original desktop display mode restored "
                                "exactly?");
                        }
                    }
                    else
                    {
                        context.skip("exclusive fullscreen activation cycle", "exclusive-mode setup failed; see preceding status");
                        context.skip("exclusive display restoration", "exclusive-mode setup failed; no display transition occurred");
                    }

                    Desktop::Types::WindowModeRequest unsupported = exclusive;
                    auto &unsupportedRequest = std::get<Desktop::Types::ExclusiveFullscreenRequest>(unsupported);

                    if(!unsupportedRequest.displayMode)
                    {
                        static_cast<void>(context.expectTrue("exclusive request carries a display mode", false));
                        return;
                    }
                    unsupportedRequest.displayMode->resolution = {1, 1};
                    const Desktop::Types::WindowMode previousMode = window.windowMode();
                    const GameWIP::Status::Types::Result<void> unsupportedStatus = setManualModeWithDiagnostics(context, window, "exclusive-unsupported", unsupported);
                    static_cast<void>(context.expectTrue("unsupported exact mode is rejected", !unsupportedStatus.ok()));
                    static_cast<void>(context.expectEq("unsupported exact mode preserves Window mode", previousMode, window.windowMode()));
                }

                if(sections.topology)
                {
                    if(!topologyMonitor)
                    {
                        context.skip("active exclusive target disconnect", "no connected non-primary monitor can be physically disconnected");
                    }
                    else
                    {
                        const GameWIP::Status::Types::Result<void> recoveryEnter =
                            setManualModeWithDiagnostics(context, window, "exclusive-disconnect-enter", exclusive);

                        if(recoveryEnter.ok())
                        {
                            const TestSupport::Types::Reporting::ManualAnswer recovery = recordManualCheck(
                                context,
                                window,
                                "active exclusive target disconnect",
                                "Disconnect/disable this non-primary exclusive-fullscreen monitor. Confirm the desktop mode restores and the "
                                "Window recovers visibly on the surviving primary, then reconnect it before answering. Otherwise skip.");

                            if(recovery == TestSupport::Types::Reporting::ManualAnswer::Yes)
                            {
                                static_cast<void>(context.expectEq(
                                    "exclusive disconnect recovers windowed mode",
                                    Desktop::Types::WindowMode::Windowed,
                                    window.windowMode()));
                                static_cast<void>(context.expectFalse(
                                    "exclusive disconnect clears fullscreen monitor",
                                    window.fullscreenState().monitor.has_value()));
                            }

                            if(window.windowMode() != Desktop::Types::WindowMode::Windowed)
                            {
                                static_cast<void>(setManualModeWithDiagnostics(
                                    context,
                                    window,
                                    "exclusive-disconnect-cleanup",
                                    Desktop::Types::WindowModes::windowed()));
                            }
                        }
                        else
                        {
                            context.skip("active exclusive target disconnect", "exclusive-mode setup failed; no active target to disconnect");
                        }
                    }
                }
            }
            else
            {
                if(sections.exclusive)
                {
                    context.skip("exclusive fullscreen", "no enumerated exact display mode is available for the current monitor");
                }

                if(sections.topology)
                {
                    context.skip("active exclusive target disconnect", "no enumerated exact display mode is available for the current monitor");
                }
            }
        }
        else
        {
            if(sections.exclusive)
            {
                context.skip("exclusive fullscreen", "backend does not advertise ExclusiveFullscreen");
            }

            if(sections.topology)
            {
                context.skip("active exclusive target disconnect", "backend does not advertise ExclusiveFullscreen");
            }
        }
    }

    // Check monitor changes and borderless recovery on the same window.
    if(sections.topology)
    {
        recordManualCheck(
            context,
            window,
            "mixed-monitor fullscreen geometry",
            "Move the Window between monitors with different DPI when available. Do logical geometry, framebuffer extent, DPI/scale, and "
            "current-monitor state follow the destination?");
        recordManualCheck(
            context,
            window,
            "monitor connect and disconnect",
            "If practical, connect/disconnect or enable/disable a non-active monitor. Does re-enumeration succeed and does the stale MonitorId "
            "fail safely? Reconnect it before answering. Skip if impractical.");

        const std::optional<Desktop::Types::Display::MonitorId> topologyMonitor = disconnectableMonitor();

        if(!topologyMonitor)
        {
            context.skip("active borderless target disconnect", "no connected non-primary monitor can be physically disconnected");
        }
        else
        {
            const Desktop::Types::WindowModeRequest activeBorderless = Desktop::Types::WindowModes::borderlessFullscreen(*topologyMonitor);
            const GameWIP::Status::Types::Result<void> borderlessEnter = setManualModeWithDiagnostics(context, window, "borderless-disconnect-enter", activeBorderless);

            if(borderlessEnter.ok())
            {
                const TestSupport::Types::Reporting::ManualAnswer recovery = recordManualCheck(
                    context,
                    window,
                    "active borderless target disconnect",
                    "Disconnect/disable this non-primary fullscreen monitor. Confirm the Window recovers visibly in windowed mode on the "
                    "surviving primary, then reconnect it before answering. Otherwise skip.");

                if(recovery == TestSupport::Types::Reporting::ManualAnswer::Yes)
                {
                    static_cast<void>(
                        context.expectEq("borderless disconnect recovers windowed mode", Desktop::Types::WindowMode::Windowed, window.windowMode()));
                    static_cast<void>(
                        context.expectFalse("borderless disconnect clears fullscreen monitor", window.fullscreenState().monitor.has_value()));
                }

                if(window.windowMode() != Desktop::Types::WindowMode::Windowed)
                {
                    static_cast<void>(
                        setManualModeWithDiagnostics(context, window, "borderless-disconnect-cleanup", Desktop::Types::WindowModes::windowed()));
                }
            }
        }

        context.pass("fullscreen recovery event ordering and failure-state cleanup are covered deterministically");
    }

    static_cast<void>(window.close());
}
