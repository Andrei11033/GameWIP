/// @file window_display_tests.inl
/// @brief Window display-query and color-information validation cases.

void testDisplayColorInformation(TestSupport::Context &context)
{
    namespace Feedback = Window::Display;

    Window::Window closed;
    static_cast<void>(context.expectEq("closed Window rejects display-color query", ErrorCode::NotOpen, Feedback::getColorInfo(closed).status.code));
    static_cast<void>(context.expectEq(
        "invalid monitor rejects display-color query",
        ErrorCode::InvalidArgument,
        Feedback::getColorInfo(Window::Types::Display::MonitorId{}).status.code));
    static_cast<void>(context.expectEq(
        "disconnected monitor rejects display-color query",
        ErrorCode::NotFound,
        Feedback::getColorInfo(Window::Types::Display::MonitorId{std::numeric_limits<std::uint64_t>::max()}).status.code));

    const Window::Types::Display::InfoResult primary = Window::Display::getPrimaryMonitor();
    static_cast<void>(context.expectTrue("display-color primary monitor resolves", primary.status.ok()));
    if (!primary.status.ok())
        return;

    const Window::Types::Display::ColorInfoResult primaryColor = Feedback::getColorInfo(primary.monitor.id);
    static_cast<void>(context.expectTrue("native display-color query succeeds", primaryColor.status.ok()));
    if (primaryColor.status.ok())
    {
        static_cast<void>(context.expectEq("display-color result retains monitor identity", primary.monitor.id, primaryColor.info.monitor));
        static_cast<void>(context.expectTrue(
            "display-color channel precision is plausible",
            primaryColor.info.bitsPerColorChannel == 0 || primaryColor.info.bitsPerColorChannel <= 64));
        static_cast<void>(context.expectTrue(
            "display-color luminance is nonnegative",
            primaryColor.info.minimumLuminanceNits >= 0.0F && primaryColor.info.maximumLuminanceNits >= 0.0F &&
                primaryColor.info.maximumFullFrameLuminanceNits >= 0.0F && primaryColor.info.sdrWhiteLevelNits >= 0.0F));
    }

    Window::Types::Description description;
    description.title = "Window display-color validation";
    description.clientSize = {240, 160};
    description.visible = false;
    Window::Window window;
    static_cast<void>(context.expectTrue("display-color Window fixture opens", window.open(description, 8).ok()));
    if (!window.isOpen())
        return;

    const Window::Types::Display::ColorInfoResult windowColor = Feedback::getColorInfo(window);
    static_cast<void>(context.expectTrue("Window display-color query succeeds", windowColor.status.ok()));
    if (windowColor.status.ok())
        static_cast<void>(context.expectEq("Window color query uses its cached monitor", window.currentMonitor(), windowColor.info.monitor));

    ErrorCode wrongThreadCode = ErrorCode::Success;
    std::thread worker(
        [&window, &wrongThreadCode]
        {
            wrongThreadCode = Feedback::getColorInfo(window).status.code;
        });
    worker.join();
    static_cast<void>(context.expectEq("wrong-thread Window color query is rejected", ErrorCode::ResourceBusy, wrongThreadCode));

#if WINDOW_INTERNAL_TEST_HOOKS
    Window::TestHooks::failNext(Window::TestHooks::FailurePoint::DisplayColorQuery);
    static_cast<void>(context.expectEq(
        "display-color native failure is translated",
        ErrorCode::StatFailed,
        Feedback::getColorInfo(window.currentMonitor()).status.code));

    Window::TestHooks::makeNextDisplayColorMetadataUnavailable();
    const Window::Types::Display::ColorInfoResult runtimeUnavailable = Feedback::getColorInfo(window.currentMonitor());
    static_cast<void>(context.expectTrue("runtime metadata absence still succeeds", runtimeUnavailable.status.ok()));
    static_cast<void>(context.expectEq(
        "runtime metadata absence remains unknown",
        Window::Types::Display::ColorSpace::Unknown,
        runtimeUnavailable.info.activeColorSpace));
    static_cast<void>(
        context.expectEq("runtime metadata absence keeps zero precision", std::uint16_t{0}, runtimeUnavailable.info.bitsPerColorChannel));

    const Window::Types::Display::MonitorId fixtureMonitor{42};
    const Window::Types::Display::ColorInfo unavailable = Window::TestHooks::makeDisplayColorInfo(fixtureMonitor, {});
    static_cast<void>(context.expectEq("unavailable color metadata preserves identity", fixtureMonitor, unavailable.monitor));
    static_cast<void>(
        context.expectEq("unavailable color metadata remains unknown", Window::Types::Display::ColorSpace::Unknown, unavailable.activeColorSpace));
    static_cast<void>(context.expectEq("unavailable color precision remains zero", std::uint16_t{0}, unavailable.bitsPerColorChannel));
    static_cast<void>(context.expectNear("unavailable SDR white remains zero", 0.0, static_cast<double>(unavailable.sdrWhiteLevelNits), 0.001));

    const Window::Types::Display::ColorInfo hdrDisabled = Window::TestHooks::makeDisplayColorInfo(
        fixtureMonitor,
        {.activeColorSpace = Window::Types::Display::ColorSpace::Srgb,
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
    static_cast<void>(context.expectEq("HDR-disabled fixture remains SDR", Window::Types::Display::ColorSpace::Srgb, hdrDisabled.activeColorSpace));
    static_cast<void>(context.expectEq("color precision converts to public width", std::uint16_t{10}, hdrDisabled.bitsPerColorChannel));
    static_cast<void>(
        context.expectNear("negative luminance becomes unavailable", 0.0, static_cast<double>(hdrDisabled.minimumLuminanceNits), 0.001));
    static_cast<void>(context.expectNear("peak luminance keeps nit units", 1000.0, static_cast<double>(hdrDisabled.maximumLuminanceNits), 0.001));
    static_cast<void>(context.expectNear(
        "non-finite full-frame luminance becomes unavailable",
        0.0,
        static_cast<double>(hdrDisabled.maximumFullFrameLuminanceNits),
        0.001));
    static_cast<void>(
        context.expectNear("SDR white converts from 80-nit thousandths", 200.0, static_cast<double>(hdrDisabled.sdrWhiteLevelNits), 0.001));

    const Window::Types::Display::ColorInfo hdrEnabled = Window::TestHooks::makeDisplayColorInfo(
        fixtureMonitor,
        {.activeColorSpace = Window::Types::Display::ColorSpace::Hdr10Pq,
         .wideColorGamutSupported = true,
         .hdrSupported = true,
         .hdrEnabled = true,
         .bitsPerColorChannel = std::numeric_limits<std::uint32_t>::max()});
    static_cast<void>(context.expectTrue("HDR fixture remains enabled", hdrEnabled.hdrEnabled));
    static_cast<void>(context.expectEq("HDR fixture remains PQ", Window::Types::Display::ColorSpace::Hdr10Pq, hdrEnabled.activeColorSpace));
    static_cast<void>(
        context.expectEq("oversized channel precision saturates", std::numeric_limits<std::uint16_t>::max(), hdrEnabled.bitsPerColorChannel));

    const Window::Types::Display::ColorInfo wideColor = Window::TestHooks::makeDisplayColorInfo(
        fixtureMonitor,
        {.activeColorSpace = Window::Types::Display::ColorSpace::WideColorGamut, .wideColorGamutSupported = true});
    static_cast<void>(
        context.expectEq("advanced-color SDR remains wide-gamut", Window::Types::Display::ColorSpace::WideColorGamut, wideColor.activeColorSpace));
    static_cast<void>(context.expectFalse("wide-gamut SDR is not inferred as HDR", wideColor.hdrEnabled));

    window.clearEvents();
    Window::TestHooks::simulateDisplayColorConfigurationChange();
    static_cast<void>(context.expectTrue("display-color transition pump succeeds", Window::Events::poll().status.ok()));
    static_cast<void>(context.expectTrue(
        "display-color transition reuses display-configuration event",
        consumeEventOfType<Window::Types::Events::DisplayConfigurationChanged>(window)));
#endif

    static_cast<void>(context.expectTrue("display-color Window fixture closes", window.close().ok()));
}

void testMonitors(TestSupport::Context &context)
{
    const Window::Types::Display::MonitorsResult monitors = Window::Display::getMonitors();
    static_cast<void>(context.expectTrue("monitor enumeration succeeds", monitors.status.ok()));
    static_cast<void>(context.expectFalse("monitor enumeration is nonempty", monitors.monitors.empty()));
    const Window::Types::Display::InfoResult primary = Window::Display::getPrimaryMonitor();
    static_cast<void>(context.expectTrue("primary monitor query succeeds", primary.status.ok()));
    if (!primary.status.ok())
        return;
    static_cast<void>(context.expectTrue("primary monitor id is valid", primary.monitor.id.isValid()));
    static_cast<void>(context.expectTrue("primary monitor is marked primary", primary.monitor.primary));
    static_cast<void>(context.expectTrue("monitor identity resolves", Window::Display::getMonitor(primary.monitor.id).status.ok()));
    const Window::Types::Display::ModesResult modes = Window::Display::getModes(primary.monitor.id);
    static_cast<void>(context.expectTrue("display-mode enumeration succeeds", modes.status.ok()));
    static_cast<void>(context.expectFalse("display-mode enumeration is nonempty", modes.modes.empty()));
    static_cast<void>(context.expectTrue("current display mode succeeds", Window::Display::getCurrentMode(primary.monitor.id).status.ok()));
    const Window::Types::Display::ModeResult preferred = Window::Display::getPreferredMode(primary.monitor.id);
    static_cast<void>(context.expectTrue("DisplayConfig preferred mode succeeds", preferred.status.ok()));
    if (preferred.status.ok())
    {
        static_cast<void>(context.expectTrue(
            "preferred mode has a physical resolution",
            preferred.mode.resolution.width != 0 && preferred.mode.resolution.height != 0));
    }
    static_cast<void>(
        context.expectEq("invalid monitor lookup is rejected", ErrorCode::InvalidArgument, Window::Display::getMonitor({}).status.code));
}
