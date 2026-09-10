/// @file desktop_manual_display_tests.inl
/// @brief Fullscreen, topology, color, and modern-capability manual validation cases.

/// @brief Exercises selected fullscreen transitions, monitor movement, and live topology recovery.
void testManualFullscreenAndTopology(
    TestSupport::Context &context,
    const GameWIP::Test::DesktopTestOptions &options,
    ManualFullscreenSections sections)
{
    if (!beginManualSuite(context, options, "Window fullscreen and display topology"))
        return;

    const Desktop::Types::Capabilities capabilities = Desktop::getCapabilities().capabilities;
    const Desktop::Types::Display::MonitorsResult monitors = Desktop::Display::getMonitors();
    if (!monitors.status.ok() || monitors.monitors.empty())
    {
        context.fail("fullscreen monitor enumeration", monitors.status.message);
        return;
    }

    Desktop::Window window;
    if (!openManualWindow(context, window, "GameWIP fullscreen validation"))
        return;
    const Desktop::Types::ScreenPosition savedPosition = window.clientPosition();
    const Desktop::Types::LogicalSize savedSize = window.clientSize();
    const auto disconnectableMonitor = []() -> std::optional<Desktop::Types::Display::MonitorId>
    {
        const Desktop::Types::Display::MonitorsResult connected = Desktop::Display::getMonitors();
        if (!connected.status.ok())
            return std::nullopt;
        const auto secondary = std::ranges::find_if(
            connected.monitors,
            [](const Desktop::Types::Display::Info &monitor)
            {
                return !monitor.primary;
            });
        return secondary == connected.monitors.end() ? std::nullopt : std::optional{secondary->id};
    };

    if (sections.borderless)
    {
        for (std::size_t index = 0; index < monitors.monitors.size(); ++index)
        {
            const Desktop::Types::Display::Info &monitor = monitors.monitors[index];
            Desktop::Types::ModeRequest borderless;
            borderless.mode = Desktop::Types::Mode::BorderlessFullscreen;
            borderless.monitor = monitor.id;
            const IO::Types::Status enterStatus = setManualModeWithDiagnostics(context, window, "borderless-enter", borderless);
            if (requireManualStatus(context, "borderless fullscreen enters", enterStatus))
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
                        monitors.monitors.size(),
                        monitor.name));
                const IO::Types::Status leaveStatus = setManualModeWithDiagnostics(context, window, "borderless-leave", {});
                static_cast<void>(requireManualStatus(context, "borderless fullscreen leaves", leaveStatus));
            }
            else
            {
                context.skip("borderless fullscreen monitor", "borderless-mode setup failed; see preceding status");
            }
        }
        const Desktop::Types::LogicalSize restoredSize = window.clientSize();
        if (savedSize == restoredSize)
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

    if (sections.exclusive || sections.topology)
    {
        if (capabilities.supports(Desktop::Types::Capability::ExclusiveFullscreen))
        {
            const std::optional<Desktop::Types::Display::MonitorId> topologyMonitor = sections.topology ? disconnectableMonitor() : std::nullopt;
            const Desktop::Types::Display::MonitorId monitor = topologyMonitor.value_or(window.currentMonitor());
            const Desktop::Types::Display::ModeResult currentMode = Desktop::Display::getCurrentMode(monitor);
            const Desktop::Types::Display::ModesResult availableModes = Desktop::Display::getModes(monitor);
            if (currentMode.status.ok() && availableModes.status.ok() && !availableModes.modes.empty())
            {
                const auto selected = std::ranges::min_element(
                    availableModes.modes,
                    {},
                    [&](const Desktop::Types::Display::Mode &mode)
                    {
                        const std::uint64_t resolutionPenalty = mode.resolution == currentMode.mode.resolution ? 0 : std::uint64_t{1} << 48;
                        const std::uint64_t depthPenalty = mode.bitsPerPixel == currentMode.mode.bitsPerPixel ? 0 : std::uint64_t{1} << 40;
                        const std::uint64_t interlacePenalty = mode.interlaced == currentMode.mode.interlaced ? 0 : std::uint64_t{1} << 39;
                        const std::uint64_t refreshDifference = mode.refreshRateMillihertz > currentMode.mode.refreshRateMillihertz
                                                                    ? mode.refreshRateMillihertz - currentMode.mode.refreshRateMillihertz
                                                                    : currentMode.mode.refreshRateMillihertz - mode.refreshRateMillihertz;
                        return resolutionPenalty + depthPenalty + interlacePenalty + refreshDifference;
                    });
                Desktop::Types::ModeRequest exclusive;
                exclusive.mode = Desktop::Types::Mode::ExclusiveFullscreen;
                exclusive.monitor = monitor;
                exclusive.displayMode = *selected;
                if (manualStatusWindow != nullptr)
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

                if (sections.exclusive)
                {
                    const IO::Types::Status enterStatus = setManualModeWithDiagnostics(context, window, "exclusive-enter", exclusive);
                    if (requireManualStatus(context, "exclusive fullscreen enters", enterStatus))
                    {
                        const ManualNativeWindowState native = manualNativeWindowState(window);
                        static_cast<void>(context.expectTrue("exclusive native HWND query succeeds", native.valid));
                        static_cast<void>(context.expectTrue("exclusive native popup style applies", native.popupStyle));
                        static_cast<void>(context.expectTrue("exclusive native bounds match active monitor", native.fullscreenBounds));
                        static_cast<void>(context.expectTrue("exclusive Window remains taskbar eligible", native.taskbarEligible));
                        bool sawExclusiveActive = window.focused() && !window.fullscreenInfo().suspended;
                        bool sawExclusiveSuspended = window.fullscreenInfo().suspended;
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
                                const Desktop::Types::FullscreenInfo liveFullscreen = window.fullscreenInfo();
                                sawExclusiveActive = sawExclusiveActive || (window.focused() && !liveFullscreen.suspended);
                                sawExclusiveSuspended = sawExclusiveSuspended || liveFullscreen.suspended;
                                if (manualStatusWindow != nullptr)
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
                        const IO::Types::Status leaveStatus = setManualModeWithDiagnostics(context, window, "exclusive-leave", {});
                        if (requireManualStatus(context, "exclusive fullscreen leaves", leaveStatus))
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

                    Desktop::Types::ModeRequest unsupported = exclusive;
                    unsupported.displayMode->resolution = {1, 1};
                    const Desktop::Types::Mode previousMode = window.mode();
                    const IO::Types::Status unsupportedStatus = setManualModeWithDiagnostics(context, window, "exclusive-unsupported", unsupported);
                    static_cast<void>(context.expectTrue("unsupported exact mode is rejected", !unsupportedStatus.ok()));
                    static_cast<void>(context.expectEq("unsupported exact mode preserves Window mode", previousMode, window.mode()));
                }

                if (sections.topology)
                {
                    if (!topologyMonitor)
                    {
                        context.skip("active exclusive target disconnect", "no connected non-primary monitor can be physically disconnected");
                    }
                    else
                    {
                        const IO::Types::Status recoveryEnter =
                            setManualModeWithDiagnostics(context, window, "exclusive-disconnect-enter", exclusive);
                        if (recoveryEnter.ok())
                        {
                            const TestSupport::Types::Reporting::ManualAnswer recovery = recordManualCheck(
                                context,
                                window,
                                "active exclusive target disconnect",
                                "Disconnect/disable this non-primary exclusive-fullscreen monitor. Confirm the desktop mode restores and the "
                                "Window recovers visibly on the surviving primary, then reconnect it before answering. Otherwise skip.");
                            if (recovery == TestSupport::Types::Reporting::ManualAnswer::Yes)
                            {
                                static_cast<void>(
                                    context.expectEq("exclusive disconnect recovers windowed mode", Desktop::Types::Mode::Windowed, window.mode()));
                                static_cast<void>(
                                    context.expectFalse("exclusive disconnect clears fullscreen monitor", window.fullscreenInfo().monitor.isValid()));
                            }
                            if (window.mode() != Desktop::Types::Mode::Windowed)
                            {
                                static_cast<void>(
                                    setManualModeWithDiagnostics(context, window, "exclusive-disconnect-cleanup", Desktop::Types::ModeRequest{}));
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
                if (sections.exclusive)
                    context.skip("exclusive fullscreen", "no enumerated exact display mode is available for the current monitor");
                if (sections.topology)
                    context.skip("active exclusive target disconnect", "no enumerated exact display mode is available for the current monitor");
            }
        }
        else
        {
            if (sections.exclusive)
                context.skip("exclusive fullscreen", "backend does not advertise ExclusiveFullscreen");
            if (sections.topology)
                context.skip("active exclusive target disconnect", "backend does not advertise ExclusiveFullscreen");
        }
    }

    if (sections.topology)
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
        if (!topologyMonitor)
        {
            context.skip("active borderless target disconnect", "no connected non-primary monitor can be physically disconnected");
        }
        else
        {
            Desktop::Types::ModeRequest activeBorderless;
            activeBorderless.mode = Desktop::Types::Mode::BorderlessFullscreen;
            activeBorderless.monitor = *topologyMonitor;
            const IO::Types::Status borderlessEnter = setManualModeWithDiagnostics(context, window, "borderless-disconnect-enter", activeBorderless);
            if (borderlessEnter.ok())
            {
                const TestSupport::Types::Reporting::ManualAnswer recovery = recordManualCheck(
                    context,
                    window,
                    "active borderless target disconnect",
                    "Disconnect/disable this non-primary fullscreen monitor. Confirm the Window recovers visibly in windowed mode on the "
                    "surviving primary, then reconnect it before answering. Otherwise skip.");
                if (recovery == TestSupport::Types::Reporting::ManualAnswer::Yes)
                {
                    static_cast<void>(
                        context.expectEq("borderless disconnect recovers windowed mode", Desktop::Types::Mode::Windowed, window.mode()));
                    static_cast<void>(
                        context.expectFalse("borderless disconnect clears fullscreen monitor", window.fullscreenInfo().monitor.isValid()));
                }
                if (window.mode() != Desktop::Types::Mode::Windowed)
                {
                    static_cast<void>(setManualModeWithDiagnostics(context, window, "borderless-disconnect-cleanup", Desktop::Types::ModeRequest{}));
                }
            }
        }

        context.pass("fullscreen recovery event ordering and failure-state cleanup are covered deterministically");
    }
    static_cast<void>(window.close());
}

/// @brief Exercises current SDR/HDR facts and user-driven advanced-color transitions.
void testManualHdrAndAdvancedColor(TestSupport::Context &context, const GameWIP::Test::DesktopTestOptions &options)
{
    if (!beginManualSuite(context, options, "Window HDR and advanced color"))
        return;

    const Desktop::Types::Display::MonitorsResult monitors = Desktop::Display::getMonitors();
    if (!monitors.status.ok() || monitors.monitors.empty())
    {
        context.fail("HDR monitor enumeration", monitors.status.message);
        return;
    }

    Desktop::Window window;
    if (!openManualWindow(context, window, "GameWIP HDR validation"))
        return;

    for (const Desktop::Types::Display::Info &monitor : monitors.monitors)
    {
        const Desktop::Types::Display::ColorInfoResult direct = Desktop::Display::getColorInfo(monitor.id);
        static_cast<void>(context.expectTrue("monitor display-color query succeeds", direct.status.ok()));
        if (!direct.status.ok())
            continue;
        const auto &info = direct.info;
        static_cast<void>(context.expectEq("display-color query retains monitor identity", monitor.id, info.monitor));
        static_cast<void>(context.expectTrue("HDR enabled implies HDR supported", !info.hdrEnabled || info.hdrSupported));
        static_cast<void>(context.expectTrue("SDR-only display is not marked HDR enabled", info.hdrSupported || !info.hdrEnabled));
        static_cast<void>(context.expectTrue(
            "HDR-disabled display is not classified as HDR",
            info.hdrEnabled || info.activeColorSpace != Desktop::Types::Display::ColorSpace::Hdr10Pq));
        static_cast<void>(context.expectTrue(
            "HDR classification is truthful",
            !info.hdrEnabled || info.activeColorSpace == Desktop::Types::Display::ColorSpace::Hdr10Pq ||
                info.activeColorSpace == Desktop::Types::Display::ColorSpace::Unknown));
        recordManualCheck(
            context,
            window,
            "display-color facts",
            std::format(
                "For display '{}', do support={}, enabled={}, bits/channel={}, min/peak/full-frame={:.1f}/{:.1f}/{:.1f} nits and SDR "
                "white={:.1f} nits match Windows/driver reports?",
                monitor.name,
                info.hdrSupported,
                info.hdrEnabled,
                info.bitsPerColorChannel,
                info.minimumLuminanceNits,
                info.maximumLuminanceNits,
                info.maximumFullFrameLuminanceNits,
                info.sdrWhiteLevelNits));
    }

    const Desktop::Types::Display::ColorInfoResult windowInfo = Desktop::Display::getColorInfo(window);
    static_cast<void>(context.expectTrue("Window display-color query succeeds", windowInfo.status.ok()));
    if (windowInfo.status.ok())
        static_cast<void>(context.expectEq("Window display-color monitor matches current monitor", window.currentMonitor(), windowInfo.info.monitor));

    const TestSupport::Types::Reporting::ManualAnswer toggle = recordManualCheck(
        context,
        window,
        "HDR toggle in place",
        "If this display supports HDR, toggle HDR in Windows, return here, and verify the Window remains stable. Skip on SDR-only hardware.");
    if (toggle == TestSupport::Types::Reporting::ManualAnswer::Yes)
    {
        static_cast<void>(context.expectTrue("HDR toggle remains queryable", Desktop::Display::getColorInfo(window).status.ok()));
        static_cast<void>(context.expectTrue(
            "HDR toggle delivers display configuration event",
            consumeEventOfType<Desktop::Types::Events::DisplayConfigurationChanged>(window)));
    }

    recordManualCheck(
        context,
        window,
        "SDR/HDR monitor movement",
        "If both SDR and HDR displays are available, move the Window between them. Do MonitorChangedEvent-triggered queries follow the "
        "destination state? Skip otherwise.");
    recordManualCheck(
        context,
        window,
        "HDR display reconnect",
        "If safe, disconnect and reconnect the queried display. Does the stale ID fail, followed by a successful fresh enumeration/query? Skip "
        "otherwise.");
    recordManualCheck(
        context,
        window,
        "Windows 10 advanced-color compatibility",
        "On an available Windows 10 compatibility host, does the legacy query work while unavailable WCG metadata remains unknown? Skip on the "
        "supported Windows 11 host.");
    static_cast<void>(window.close());
}

/// @brief Exercises runtime-gated backdrop effects and renderer-provided framebuffer alpha.
void testManualModernWindowsCapabilities(TestSupport::Context &context, const GameWIP::Test::DesktopTestOptions &options)
{
    if (!beginManualSuite(context, options, "Window modern Windows capabilities"))
        return;

    const Desktop::Types::Capabilities capabilities = Desktop::getCapabilities().capabilities;
    Desktop::Window window;
    if (!openManualWindow(context, window, "GameWIP modern capability validation"))
        return;

    constexpr std::array effects{
        Desktop::Types::BackdropEffect::Automatic,
        Desktop::Types::BackdropEffect::MainWindow,
        Desktop::Types::BackdropEffect::TransientWindow,
        Desktop::Types::BackdropEffect::TabbedWindow};
    if (capabilities.supports(Desktop::Types::Capability::SystemBackdrop))
    {
        for (std::size_t index = 0; index < effects.size(); ++index)
        {
            const IO::Types::Status status = window.setBackdropEffect(effects[index]);
            static_cast<void>(context.expectTrue("supported backdrop effect applies", status.ok()));
            if (status.ok())
            {
                recordManualCheck(
                    context,
                    window,
                    "system backdrop presentation",
                    std::format("Does backdrop effect {}/{} render as the corresponding native Windows treatment?", index + 1, effects.size()));
            }
            static_cast<void>(context.expectTrue("system backdrop clears", window.setBackdropEffect(Desktop::Types::BackdropEffect::None).ok()));
        }
    }
    else
    {
        for (const Desktop::Types::BackdropEffect effect : effects)
        {
            static_cast<void>(
                context.expectEq("unsupported backdrop effect is rejected", ErrorCode::Unsupported, window.setBackdropEffect(effect).code));
        }
        context.skip("system backdrop presentation", "runtime does not advertise SystemBackdrop");
    }
    static_cast<void>(window.close());

    Desktop::Types::Description alphaDescription;
    alphaDescription.transparentFramebuffer = true;
    Desktop::Window alpha;
    const IO::Types::Status alphaStatus = alpha.open(alphaDescription);
    if (capabilities.supports(Desktop::Types::Capability::TransparentFramebuffer))
    {
        static_cast<void>(context.expectTrue("modern alpha Window opens", alphaStatus.ok()));
        if (alpha.isOpen())
        {
            static_cast<void>(alpha.show());
            context.skip(
                "redirection-bitmap framebuffer alpha",
                "GameWIPTests has no renderer-provided premultiplied-alpha surface; run this observation in a renderer-backed host");
            context.skip("opacity and framebuffer-alpha independence", "requires the same renderer-backed alpha host");
            static_cast<void>(alpha.close());
        }
    }
    else
    {
        static_cast<void>(context.expectEq("unsupported modern alpha Window is rejected", ErrorCode::Unsupported, alphaStatus.code));
    }
}
