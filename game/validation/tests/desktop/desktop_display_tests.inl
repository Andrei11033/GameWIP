/// @file desktop_display_tests.inl
/// @brief Window display-query and color-information validation cases.

void testDisplayColorInformation(TestSupport::Context &context)
{
    namespace Feedback = Desktop::Display;

    Desktop::Window closed;
    static_cast<void>(context.expectEq("closed Window rejects display-color query", ErrorCode::NotOpen, Feedback::getColorInfo(closed).status.code));
    static_cast<void>(context.expectEq(
        "invalid monitor rejects display-color query",
        ErrorCode::InvalidArgument,
        Feedback::getColorInfo(Desktop::Types::Display::MonitorId::invalid()).status.code));
    static_cast<void>(context.expectEq(
        "disconnected monitor rejects display-color query",
        ErrorCode::NotFound,
        Feedback::getColorInfo(Desktop::Types::Display::MonitorId{std::numeric_limits<std::uint64_t>::max()}).status.code));

    const auto primary = Desktop::Display::getPrimaryMonitor();
    static_cast<void>(context.expectTrue("display-color primary monitor resolves", primary.ok()));
    if (!primary.ok())
    {
        return;
    }

    const auto primaryColor = Feedback::getColorInfo(primary->id);
    static_cast<void>(context.expectTrue("native display-color query succeeds", primaryColor.ok()));
    if (primaryColor.ok())
    {
        static_cast<void>(context.expectEq("display-color result retains monitor identity", primary->id, primaryColor->monitor));
        static_cast<void>(context.expectTrue(
            "display-color channel precision is plausible",
            !primaryColor->bitsPerColorChannel || *primaryColor->bitsPerColorChannel <= 64));
        static_cast<void>(context.expectTrue(
            "display-color luminance is nonnegative",
            (!primaryColor->minimumLuminanceNits || *primaryColor->minimumLuminanceNits >= 0.0F) &&
                (!primaryColor->maximumLuminanceNits || *primaryColor->maximumLuminanceNits >= 0.0F) &&
                (!primaryColor->maximumFullFrameLuminanceNits || *primaryColor->maximumFullFrameLuminanceNits >= 0.0F) &&
                (!primaryColor->sdrWhiteLevelNits || *primaryColor->sdrWhiteLevelNits >= 0.0F)));
    }

    Desktop::Types::Description description;
    description.title = "Window display-color validation";
    description.windowed.clientSize = {240, 160};
    description.activation.visible = false;
    Desktop::Window window;
    static_cast<void>(context.expectTrue("display-color Window fixture opens", window.open(description, Desktop::Events::owned(8)).ok()));
    if (!window.isOpen())
    {
        return;
    }

    const auto windowColor = Feedback::getColorInfo(window);
    static_cast<void>(context.expectTrue("Window display-color query succeeds", windowColor.ok()));
    if (windowColor.ok())
    {
        static_cast<void>(context.expectEq("Window color query uses its cached monitor", window.currentMonitor(), windowColor->monitor));
    }

    ErrorCode wrongThreadCode = ErrorCode::Success;
    std::thread worker(
        [&window, &wrongThreadCode]
        {
            wrongThreadCode = Feedback::getColorInfo(window).status.code;
        });
    worker.join();
    static_cast<void>(context.expectEq("wrong-thread Window color query is rejected", ErrorCode::ResourceBusy, wrongThreadCode));

#if DESKTOP_INTERNAL_TEST_HOOKS
    Desktop::TestHooks::failNext(Desktop::TestHooks::FailurePoint::DisplayColorQuery);
    static_cast<void>(context.expectEq(
        "display-color native failure is translated",
        ErrorCode::StatFailed,
        Feedback::getColorInfo(window.currentMonitor()).status.code));

    Desktop::TestHooks::makeNextDisplayColorMetadataUnavailable();
    const auto runtimeUnavailable = Feedback::getColorInfo(window.currentMonitor());
    static_cast<void>(context.expectTrue("runtime metadata absence still succeeds", runtimeUnavailable.ok()));
    static_cast<void>(context.expectEq(
        "runtime metadata absence remains unknown",
        Desktop::Types::Display::ColorSpace::Unknown,
        runtimeUnavailable->activeColorSpace));
    static_cast<void>(context.expectFalse("runtime metadata absence has no precision", runtimeUnavailable->bitsPerColorChannel.has_value()));

    const Desktop::Types::Display::MonitorId fixtureMonitor{42};
    const Desktop::Types::Display::ColorInfo unavailable = Desktop::TestHooks::makeDisplayColorInfo(fixtureMonitor, {});
    static_cast<void>(context.expectEq("unavailable color metadata preserves identity", fixtureMonitor, unavailable.monitor));
    static_cast<void>(
        context.expectEq("unavailable color metadata remains unknown", Desktop::Types::Display::ColorSpace::Unknown, unavailable.activeColorSpace));
    static_cast<void>(context.expectFalse("unavailable color precision is absent", unavailable.bitsPerColorChannel.has_value()));
    static_cast<void>(context.expectFalse("unavailable SDR white is absent", unavailable.sdrWhiteLevelNits.has_value()));

    const Desktop::Types::Display::ColorInfo hdrDisabled = Desktop::TestHooks::makeDisplayColorInfo(
        fixtureMonitor,
        {.activeColorSpace = Desktop::Types::Display::ColorSpace::Srgb,
         .wideColorGamutSupported = true,
         .hdrSupported = true,
         .hdrEnabled = false,
         .bitsPerColorChannel = 10,
         .minimumLuminanceNits = -1.0F,
         .maximumLuminanceNits = 1000.0F,
         .maximumFullFrameLuminanceNits = std::numeric_limits<float>::quiet_NaN(),
         .sdrWhiteLevelMilli80Nits = 2500});
    static_cast<void>(context.expectTrue("HDR-capable disabled fixture retains support", hdrDisabled.hdrSupported));
    static_cast<void>(context.expectFalse("HDR-capable disabled fixture remains disabled", hdrDisabled.hdrEnabled));
    static_cast<void>(context.expectEq("HDR-disabled fixture remains SDR", Desktop::Types::Display::ColorSpace::Srgb, hdrDisabled.activeColorSpace));
    static_cast<void>(context.expectEq("color precision converts to public width", std::uint16_t{10}, hdrDisabled.bitsPerColorChannel.value_or(0)));
    static_cast<void>(context.expectFalse("negative luminance becomes unavailable", hdrDisabled.minimumLuminanceNits.has_value()));
    static_cast<void>(
        context.expectNear("peak luminance keeps nit units", 1000.0, static_cast<double>(hdrDisabled.maximumLuminanceNits.value_or(0.0F)), 0.001));
    static_cast<void>(
        context.expectFalse("non-finite full-frame luminance becomes unavailable", hdrDisabled.maximumFullFrameLuminanceNits.has_value()));
    static_cast<void>(context.expectNear(
        "SDR white converts from 80-nit thousandths",
        200.0,
        static_cast<double>(hdrDisabled.sdrWhiteLevelNits.value_or(0.0F)),
        0.001));

    const Desktop::Types::Display::ColorInfo hdrEnabled = Desktop::TestHooks::makeDisplayColorInfo(
        fixtureMonitor,
        {.activeColorSpace = Desktop::Types::Display::ColorSpace::Hdr10Pq,
         .wideColorGamutSupported = true,
         .hdrSupported = true,
         .hdrEnabled = true,
         .bitsPerColorChannel = std::numeric_limits<std::uint32_t>::max()});
    static_cast<void>(context.expectTrue("HDR fixture remains enabled", hdrEnabled.hdrEnabled));
    static_cast<void>(context.expectEq("HDR fixture remains PQ", Desktop::Types::Display::ColorSpace::Hdr10Pq, hdrEnabled.activeColorSpace));
    static_cast<void>(context.expectEq(
        "oversized channel precision saturates",
        std::numeric_limits<std::uint16_t>::max(),
        hdrEnabled.bitsPerColorChannel.value_or(0)));

    const Desktop::Types::Display::ColorInfo wideColor = Desktop::TestHooks::makeDisplayColorInfo(
        fixtureMonitor,
        {.activeColorSpace = Desktop::Types::Display::ColorSpace::WideColorGamut, .wideColorGamutSupported = true});
    static_cast<void>(
        context.expectEq("advanced-color SDR remains wide-gamut", Desktop::Types::Display::ColorSpace::WideColorGamut, wideColor.activeColorSpace));
    static_cast<void>(context.expectFalse("wide-gamut SDR is not inferred as HDR", wideColor.hdrEnabled));

    window.clearEvents();
    Desktop::TestHooks::simulateDisplayColorConfigurationChange();
    static_cast<void>(context.expectTrue("display-color transition pump succeeds", Desktop::Events::poll().status.ok()));
    static_cast<void>(context.expectTrue(
        "display-color transition reuses display-configuration event",
        consumeEventOfType<Desktop::Types::Events::DisplayConfigurationChanged>(window)));
#endif

    static_cast<void>(context.expectTrue("display-color Window fixture closes", window.close().ok()));
}

void testMonitors(TestSupport::Context &context)
{
    const auto monitors = Desktop::Display::getMonitors();
    static_cast<void>(context.expectTrue("monitor enumeration succeeds", monitors.ok()));
    static_cast<void>(context.expectFalse("monitor enumeration is nonempty", monitors->empty()));
    const auto primary = Desktop::Display::getPrimaryMonitor();
    static_cast<void>(context.expectTrue("primary monitor query succeeds", primary.ok()));
    if (!primary.ok())
    {
        return;
    }
    static_cast<void>(context.expectTrue("primary monitor id is valid", primary->id.isValid()));
    static_cast<void>(context.expectTrue("primary monitor is marked primary", primary->primary));
    static_cast<void>(context.expectTrue("monitor identity resolves", Desktop::Display::getMonitor(primary->id).ok()));
    const auto modes = Desktop::Display::getModes(primary->id);
    static_cast<void>(context.expectTrue("display-mode enumeration succeeds", modes.ok()));
    static_cast<void>(context.expectFalse("display-mode enumeration is nonempty", modes->empty()));
    static_cast<void>(context.expectTrue("current display mode succeeds", Desktop::Display::getCurrentMode(primary->id).ok()));
    const auto preferred = Desktop::Display::getPreferredMode(primary->id);
    static_cast<void>(context.expectTrue("DisplayConfig preferred mode succeeds", preferred.ok()));
    if (preferred.ok())
    {
        static_cast<void>(
            context.expectTrue("preferred mode has a physical resolution", preferred->resolution.width != 0 && preferred->resolution.height != 0));
    }
    static_cast<void>(context.expectEq(
        "invalid monitor lookup is rejected",
        ErrorCode::InvalidArgument,
        Desktop::Display::getMonitor(Desktop::Types::Display::MonitorId::invalid()).status.code));
}
