/// @file desktop_manual_color_tests.inl
/// @brief HDR and advanced color manual validation.

/// ------------------------------------------------------------
/// HDR and advanced color
/// ------------------------------------------------------------

/// @brief Exercises current SDR/HDR facts and user-driven advanced-color transitions.
void testManualHdrAndAdvancedColor(TestSupport::Context &context, const GameWIP::Test::DesktopTestOptions &options)
{
    if(!beginManualSuite(context, options, "Window HDR and advanced color"))
    {
        return;
    }

    // Enumerate displays and report each native color-space description.
    const auto monitors = Desktop::Display::getMonitors();

    if(!monitors.ok())
    {
        const GameWIP::Status::Types::Diagnostic *diagnostic = monitors.status().diagnostic();
        context.fail("HDR monitor enumeration", diagnostic != nullptr ? diagnostic->message : "monitor enumeration failed");
        return;
    }

    if(!context.expectTrue("HDR monitor enumeration returns at least one display", !monitors.value().empty()))
    {
        return;
    }

    Desktop::Window window;

    if(!openManualWindow(context, window, "GameWIP HDR validation"))
    {
        return;
    }

    for(const Desktop::Types::Display::Info &monitor : monitors.value())
    {
        const auto direct = Desktop::Display::getColorInfo(monitor.id);
        static_cast<void>(context.expectTrue("monitor display-color query succeeds", direct.ok()));

        if(!direct.ok())
        {
            continue;
        }
        const auto &info = direct.value();
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
                info.bitsPerColorChannel.value_or(0),
                info.minimumLuminanceNits.value_or(0.0F),
                info.maximumLuminanceNits.value_or(0.0F),
                info.maximumFullFrameLuminanceNits.value_or(0.0F),
                info.sdrWhiteLevelNits.value_or(0.0F)));
    }

    // Compare Window state, then observe user-driven HDR transitions.
    const auto windowInfo = Desktop::Display::getColorInfo(window);
    static_cast<void>(context.expectTrue("Window display-color query succeeds", windowInfo.ok()));

    if(windowInfo.ok())
    {
        static_cast<void>(context.expectEq("Window display-color monitor matches current monitor", window.currentMonitor(), windowInfo.value().monitor));
    }

    const TestSupport::Types::Reporting::ManualAnswer toggle = recordManualCheck(
        context,
        window,
        "HDR toggle in place",
        "If this display supports HDR, toggle HDR in Windows, return here, and verify the Window remains stable. Skip on SDR-only hardware.");

    if(toggle == TestSupport::Types::Reporting::ManualAnswer::Yes)
    {
        static_cast<void>(context.expectTrue("HDR toggle remains queryable", Desktop::Display::getColorInfo(window).status().ok()));
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
    static_cast<void>(window.close());
}
