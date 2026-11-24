/// @file desktop_values_tests.inl
/// @brief Desktop value and description validation tests.

/// ------------------------------------------------------------
/// Values and descriptions
/// ------------------------------------------------------------

void testPassiveValuesAndClosedState(TestSupport::Context &context)
{
    // Validate result access and portable value contracts.
    Desktop::Types::Result<Desktop::Types::LogicalSize> value{IO::successStatus(), Desktop::Types::LogicalSize{12, 24}};
    static_cast<void>(context.expectTrue("successful populated result is usable", value.ok()));
    static_cast<void>(context.expectEq("mutable arrow accesses the contained value", std::uint32_t{12}, value->width));
    value->height = 48;
    const auto &constantValue = value;
    static_cast<void>(context.expectEq("const dereference observes the same value", std::uint32_t{48}, (*constantValue).height));
    static_cast<void>(context.expectEq("const arrow accesses the contained value", std::uint32_t{48}, constantValue->height));
    value.status = IO::makeStatus(ErrorCode::NativeFailure);
    static_cast<void>(context.expectFalse("failed status makes a populated result unusable", value.ok()));
    static_cast<void>(context.expectEq("failed result uses the explicit fallback", Desktop::Types::LogicalSize{1, 2}, value.value_or({1, 2})));
    value = {IO::successStatus(), std::nullopt};
    static_cast<void>(context.expectFalse("success status without a value is unusable", value.ok()));

    static_cast<void>(context.expectFalse("default window id is invalid", Desktop::Types::WindowId{}.isValid()));
    static_cast<void>(context.expectTrue("nonzero window id is valid", Desktop::Types::WindowId{4}.isValid()));
    static_cast<void>(context.expectFalse("default monitor id is invalid", Desktop::Types::Display::MonitorId::invalid().isValid()));
    static_cast<void>(context.expectTrue(
        "geometry values compare structurally",
        Desktop::Types::LogicalRect{{-4, 8}, {10, 12}} == Desktop::Types::LogicalRect{{-4, 8}, {10, 12}}));
    static_cast<void>(
        context.expectEq("hit-mask words round each row independently", std::size_t{4}, Desktop::Renderer::requiredPointerHitMaskWords({33, 2})));
    static_cast<void>(context.expectEq("empty hit-mask extent is invalid", std::size_t{0}, Desktop::Renderer::requiredPointerHitMaskWords({0, 8})));
#if DESKTOP_INTERNAL_TEST_HOOKS
    static_cast<void>(context.expectEq(
        "60000/1001 refresh rounds to millihertz",
        std::uint32_t{59'940},
        Desktop::Internal::TestHooks::refreshRateMillihertz(60'000, 1'001)));
    static_cast<void>(
        context.expectEq("unknown rational refresh remains zero", std::uint32_t{0}, Desktop::Internal::TestHooks::refreshRateMillihertz(60'000, 0)));
    static_cast<void>(context.expectEq(
        "rational refresh conversion saturates",
        std::numeric_limits<std::uint32_t>::max(),
        Desktop::Internal::TestHooks::refreshRateMillihertz(std::numeric_limits<std::uint32_t>::max(), 1)));
    const Desktop::Types::Display::Mode exactMode{{1920, 1080}, 60'000, 32, false};
    static_cast<void>(context.expectTrue(
        "exclusive comparator accepts an exact native mode",
        Desktop::Internal::TestHooks::exactNativeDisplayModeMatches(exactMode, 1920, 1080, 60, 32, false)));
    Desktop::Types::Display::Mode fractionalMode = exactMode;
    fractionalMode.refreshRateMillihertz = 59'940;
    static_cast<void>(context.expectFalse(
        "exclusive comparator rejects a nearest integer-Hz mode",
        Desktop::Internal::TestHooks::exactNativeDisplayModeMatches(fractionalMode, 1920, 1080, 60, 32, false)));
    const Desktop::Internal::TestHooks::DpiTransitionResult preserveLogical =
        Desktop::Internal::TestHooks::calculateDpiTransition({800, 600}, {800, 600}, 144, Desktop::Types::DpiResizePolicy::PreserveLogicalClientSize);
    static_cast<void>(
        context.expectEq("logical-size DPI policy preserves logical extent", Desktop::Types::LogicalSize{800, 600}, preserveLogical.logicalSize));
    static_cast<void>(
        context.expectEq("logical-size DPI policy scales framebuffer", Desktop::Types::PixelSize{1200, 900}, preserveLogical.framebufferSize));
    const Desktop::Internal::TestHooks::DpiTransitionResult preservePhysical = Desktop::Internal::TestHooks::calculateDpiTransition(
        {800, 600},
        {800, 600},
        144,
        Desktop::Types::DpiResizePolicy::PreservePhysicalClientSize);
    static_cast<void>(context.expectEq(
        "physical-size DPI policy recalculates logical extent",
        Desktop::Types::LogicalSize{533, 400},
        preservePhysical.logicalSize));
    static_cast<void>(
        context.expectEq("physical-size DPI policy preserves framebuffer", Desktop::Types::PixelSize{800, 600}, preservePhysical.framebufferSize));
#endif

    // Check capability reporting and event pumping without open windows.
    const auto capabilities = Desktop::getCapabilities();
    static_cast<void>(context.expectTrue("capability query succeeds", capabilities.status.ok()));
    if(capabilities.ok())
    {
        static_cast<void>(context.expectTrue("Win32 supports multiple windows", capabilities->supports(Desktop::Types::Capability::MultipleWindows)));
        static_cast<void>(
            context.expectTrue("Win32 supports custom native cursors", capabilities->supports(Desktop::Types::Capability::CustomCursor)));
    }

    static_cast<void>(context.expectFalse("Count is not a capability", capabilities->supports(Desktop::Types::Capability::Count)));

    const Desktop::Types::Events::PumpResult idlePoll = Desktop::Events::poll();
    static_cast<void>(context.expectTrue("poll with no windows is a successful no-op", idlePoll.status.ok()));
    const Desktop::Types::Events::PumpResult badWait = Desktop::Events::wait(std::chrono::milliseconds{-2});
    static_cast<void>(context.expectEq("timeout below forever sentinel is invalid", ErrorCode::InvalidArgument, badWait.status.code));

    // Verify default and closed Window state.
    Desktop::Window closed;
    static_cast<void>(context.expectFalse("default Window is closed", closed.isOpen()));
    static_cast<void>(context.expectEq("default lifetime is Closed", Desktop::Types::LifetimeState::Closed, closed.lifetimeState()));
    static_cast<void>(context.expectEq("closed id is invalid", Desktop::Types::WindowId{}, closed.id()));
    static_cast<void>(context.expectEq("closed title is empty", std::string_view{}, closed.title()));
    static_cast<void>(context.expectEq("closed operation reports NotOpen", ErrorCode::NotOpen, closed.setTitle("unused").code));
    static_cast<void>(context.expectTrue("repeated close on closed Window succeeds", closed.close().ok()));
}

void testDescriptionValidation(TestSupport::Context &context)
{
    const auto expectInvalid = [&context](std::string_view name, const Desktop::Types::Description &description)
    {
        Desktop::Window window;
        const IO::Types::Status status = window.open(description);
        static_cast<void>(context.expectEq(name, ErrorCode::InvalidArgument, status.code));
        static_cast<void>(context.expectFalse("invalid description leaves Window closed", window.isOpen()));
    };

    // Reject invalid scalar and text values.
    Desktop::Types::Description description;
    description.windowed.clientSize = {};
    expectInvalid("zero client size is invalid", description);

    description = {};
    description.title.assign("bad\0title", 9);
    expectInvalid("embedded title NUL is invalid", description);

    description = {};
    description.title.assign("\xC0\xAF", 2);
    expectInvalid("overlong UTF-8 is invalid", description);

    description = {};
    description.opacity = 1.1F;
    expectInvalid("opacity above one is invalid", description);

    // Reject inconsistent creation options and native-range overflow.
    description = {};
    description.resizing.limits.minimum = Desktop::Types::LogicalSize{900, 700};
    description.resizing.limits.maximum = Desktop::Types::LogicalSize{800, 600};
    expectInvalid("inverted size limits are invalid", description);

    description = {};
    description.resizing.aspectRatio = Desktop::Types::AspectRatio{16, 0};
    expectInvalid("zero aspect denominator is invalid", description);

    description = {};
    description.pointerInputMode = Desktop::Types::PointerInputMode::AcceptRegions;
    expectInvalid("initial region mode without layout is invalid", description);

    description = {};
    description.cursorMode = static_cast<Desktop::Types::CursorMode>(99);
    expectInvalid("unknown enum is invalid", description);

    description = {};
    description.dpiResizePolicy = static_cast<Desktop::Types::DpiResizePolicy>(99);
    expectInvalid("unknown DPI resize policy is invalid", description);

    description = {};
    description.resizing.enabled = false;
    description.resizing.controls.maximizable = true;
    expectInvalid("maximize requires resize at creation", description);

    description = {};
    description.windowed.placement.monitor = Desktop::Types::Display::MonitorId{std::numeric_limits<std::uint64_t>::max()};
    expectInvalid("unknown placement monitor is invalid", description);

    description = {};
    description.initialWindowMode =
        Desktop::Types::WindowModes::borderlessFullscreen(Desktop::Types::Display::MonitorId{std::numeric_limits<std::uint64_t>::max()});
    expectInvalid("unknown mode monitor is invalid", description);

    description = {};
    description.owner = {std::numeric_limits<std::uint64_t>::max()};
    expectInvalid("unknown owner is invalid", description);

    description = {};
    description.activation.requestFocus = true;
    expectInvalid("hidden initial focus request is invalid", description);

    description = {};
    description.presentation = Desktop::Types::PresentationState::Minimized;
    expectInvalid("hidden non-normal presentation is invalid", description);

    description = {};
    description.activation.visible = true;
    description.activation.requestFocus = true;
    description.activation.focusable = false;
    expectInvalid("focus request on non-focusable Window is invalid", description);

    description = {};
    description.windowed.clientSize.width = std::numeric_limits<std::uint32_t>::max();
    expectInvalid("client extent outside native signed range is invalid", description);

    Desktop::Window window;
    static_cast<void>(
        context.expectEq("zero internal queue capacity is invalid", ErrorCode::InvalidArgument, window.open({}, Desktop::Events::owned(0)).code));
    const std::span<Desktop::Types::Event> empty;
    static_cast<void>(
        context.expectEq("empty external queue is invalid", ErrorCode::InvalidArgument, window.open({}, Desktop::Events::borrowed(empty)).code));
}
