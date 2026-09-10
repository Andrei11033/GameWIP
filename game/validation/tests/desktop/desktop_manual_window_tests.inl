/// @file desktop_manual_window_tests.inl
/// @brief Visible window lifecycle, chrome, and pointer manual validation cases.

/// @brief Exercises the core real visible-window lifecycle required before submission.
void testManualVisibleLifecycle(TestSupport::Context &context, const GameWIP::Test::DesktopTestOptions &options)
{
    if (!beginManualSuite(context, options, "Window visible lifecycle"))
        return;

    context.manual(
        "Window manual tests require a normal interactive Windows desktop. Answer yes, no, or skip at each prompt. "
        "Restore any changed desktop state before the suite ends.");

    Desktop::Types::Description description;
    description.title = "GameWIP Window manual validation";
    description.clientSize = {960, 540};
    description.visible = true;
    description.requestFocus = true;

    Desktop::Window window;
    const IO::Types::Status openStatus = window.open(description);
    if (!openStatus.ok())
    {
        context.fail("manual Window opens", openStatus.message);
        return;
    }
    context.pass("manual Window opens");

    recordManualCheck(
        context,
        window,
        "visible Window create, show, and focus",
        "Is the GameWIP validation Window visible, correctly sized, and focused or requesting attention as allowed by Windows?");
    recordManualCheck(
        context,
        window,
        "visible Window move and resize",
        "Move and resize the Window with the system frame. Do movement and edge/corner resizing behave normally?");

    static_cast<void>(context.expectTrue("manual Window maximize request succeeds", window.maximize().ok()));
    recordManualCheck(context, window, "visible Window maximize", "Did the Window maximize correctly?");
    static_cast<void>(context.expectTrue("manual Window restore request succeeds", window.restore().ok()));
    static_cast<void>(context.expectTrue("manual Window minimize request succeeds", window.minimize().ok()));
    recordManualCheck(context, window, "visible Window minimize", "Did the Window minimize correctly? Restore it from the taskbar before answering.");
    static_cast<void>(context.expectTrue("manual Window restore after minimize succeeds", window.restore().ok()));
    static_cast<void>(window.requestFocus());

    recordManualCheck(
        context,
        window,
        "system close request",
        "Click the Window close button once. Does the Window remain alive while recording a close request?");
    static_cast<void>(context.expectTrue("system close request becomes sticky", window.hasCloseRequest()));
    if (window.hasCloseRequest())
        static_cast<void>(context.expectTrue("system close request clears", window.clearCloseRequest().ok()));
    static_cast<void>(context.expectTrue("cleared close request leaves Window open", window.isOpen()));

    recordManualCheck(
        context,
        window,
        "second system close request",
        "Click the Window close button again. Does the Window again remain alive pending explicit close?");
    static_cast<void>(context.expectTrue("second system close request becomes sticky", window.hasCloseRequest()));
    static_cast<void>(context.expectTrue("manual Window explicit close succeeds", window.close().ok()));
    static_cast<void>(context.expectFalse("manual Window is closed", window.isOpen()));

    Desktop::Window reopened;
    description.visible = false;
    description.requestFocus = false;
    static_cast<void>(context.expectTrue("manual Window reopens after explicit close", reopened.open(description).ok()));
    static_cast<void>(context.expectTrue("reopened manual Window closes cleanly", reopened.close().ok()));
}

/// @brief Exercises visible multi-window ownership, routing, activation, and native relationship behavior.
void testManualMultipleWindows(TestSupport::Context &context, const GameWIP::Test::DesktopTestOptions &options)
{
    if (!beginManualSuite(context, options, "Window multiple-window scenarios"))
        return;

    Desktop::Window owner;
    if (!openManualWindow(context, owner, "GameWIP manual owner Window"))
        return;

    Desktop::Types::Description childDescription;
    childDescription.owner = owner.id();
    Desktop::Window child;
    if (!openManualWindow(context, child, "GameWIP manual owned tool Window", childDescription))
        return;
    const auto paintOwnedWindow = [&]
    {
        paintManualValidationSurface(child);
    };

    recordManualCheck(
        context,
        owner,
        "two independent queues",
        "Move, resize, and focus both Windows. Do both remain responsive and receive behavior independently?",
        paintOwnedWindow);
    recordManualCheck(
        context,
        owner,
        "owned Window native behavior",
        "Activate, minimize, and restore the owner and owned Window. Is their native z-order/minimization relationship stable?",
        paintOwnedWindow);

    static_cast<void>(context.expectTrue("owned Window owner removal succeeds", child.setOwner({}).ok()));
    recordManualCheck(
        context,
        owner,
        "owned Window owner removal",
        "After owner removal, does the tool Window behave as an independent taskbar Window?",
        paintOwnedWindow);
    static_cast<void>(context.expectTrue("owned Window owner restoration succeeds", child.setOwner(owner.id()).ok()));
    recordManualCheck(
        context,
        owner,
        "owned Window owner restoration",
        "After owner restoration, is the independent taskbar entry removed and native ownership restored?",
        paintOwnedWindow);

    static_cast<void>(context.expectTrue("owned Window closes", child.close().ok()));
    recordManualCheck(context, owner, "remaining Window after peer close", "Does the owner remain fully operational after the other Window closes?");

    static_cast<void>(context.expectTrue("manual owner hides", owner.hide().ok()));
    recordManualCheck(
        context,
        owner,
        "show-without-activation preparation",
        "Answer yes, then immediately focus another desktop application; the Window will be shown after a five-second preparation interval.");
    pumpManualPreparation(std::chrono::seconds{5});
    static_cast<void>(context.expectTrue("manual owner shows without activation", owner.show().ok()));
    recordManualCheck(context, owner, "show without activation", "Did show() leave the other application active instead of stealing focus?");
    const IO::Types::Status focusStatus = owner.requestFocus();
    recordManualCheck(
        context,
        owner,
        "explicit focus policy",
        focusStatus.ok() ? "After requestFocus(), did Windows either focus the Window or provide its normal attention indication?"
                         : "Windows rejected requestFocus(). Was the existing foreground application left stable?");
    static_cast<void>(context.expectTrue("manual owner closes", owner.close().ok()));

    context.pass("cross-thread destruction is covered by Window threading contracts");
    context.pass("unexpected native destruction is covered by Window exceptional lifetime");
    context.pass("owner-thread exit cleanup is covered by Window exceptional lifetime");
}

/// @brief Exercises custom non-client hit testing and runtime layout replacement.
void testManualCustomChrome(TestSupport::Context &context, const GameWIP::Test::DesktopTestOptions &options)
{
    if (!beginManualSuite(context, options, "Window custom chrome"))
        return;

    const Desktop::Types::Capabilities capabilities = Desktop::getCapabilities().capabilities;
    if (!capabilities.supports(Desktop::Types::Capability::CustomChrome))
    {
        context.skip("Window custom chrome", "backend does not advertise CustomChrome");
        return;
    }

    Desktop::Types::Description description;
    description.decoration = Desktop::Types::DecorationMode::Custom;
    Desktop::Window window;
    if (!openManualWindow(context, window, "GameWIP custom chrome validation", description))
        return;

    const std::array draggable{Desktop::Types::LogicalRect{{0, 0}, {760, 48}}};
    Desktop::Types::CustomChromeLayout layout;
    layout.draggableRegions = draggable;
    layout.systemMenuRegion = Desktop::Types::LogicalRect{{0, 0}, {48, 48}};
    layout.minimizeButtonRegion = Desktop::Types::LogicalRect{{800, 0}, {48, 48}};
    layout.maximizeButtonRegion = Desktop::Types::LogicalRect{{848, 0}, {48, 48}};
    layout.closeButtonRegion = Desktop::Types::LogicalRect{{896, 0}, {64, 48}};
    if (!requireManualStatus(context, "custom chrome layout applies", window.setCustomChromeLayout(layout)))
        return;

    recordManualCheck(
        context,
        window,
        "custom chrome controls and resize",
        "Using the top 48 logical pixels, test dragging and the system/minimize/maximize/close regions without accepting close. Also test every "
        "resize edge/corner and a snap layout. Do all native behaviors work?",
        {},
        ManualSurfaceLayout::CustomChromePrimary);

    const std::array replacement{Desktop::Types::LogicalRect{{0, 48}, {640, 40}}};
    layout.draggableRegions = replacement;
    layout.systemMenuRegion.reset();
    layout.minimizeButtonRegion.reset();
    layout.maximizeButtonRegion.reset();
    layout.closeButtonRegion.reset();
    static_cast<void>(context.expectTrue("replacement custom chrome layout applies", window.setCustomChromeLayout(layout).ok()));
    recordManualCheck(
        context,
        window,
        "custom chrome replacement",
        "Does only the replacement strip at logical y=48..87 drag, with every old top-strip region inactive immediately?",
        {},
        ManualSurfaceLayout::CustomChromeReplacement);
    recordManualCheck(
        context,
        window,
        "custom chrome DPI scales",
        "If 100%, 125%, 150%, or 200% displays are available, move the Window among them. Do chrome regions remain aligned? Skip if the topology "
        "is unavailable.",
        {},
        ManualSurfaceLayout::CustomChromeReplacement);

    static_cast<void>(context.expectTrue("custom chrome layout clears", window.clearCustomChromeLayout().ok()));
    static_cast<void>(context.expectTrue("custom chrome Window closes", window.close().ok()));
}

/// @brief Exercises opacity, compositor transparency, cross-application routing, and mask-visible behavior.
void testManualLayeredAndPointer(TestSupport::Context &context, const GameWIP::Test::DesktopTestOptions &options)
{
    if (!beginManualSuite(context, options, "Window layered and pointer behavior"))
        return;

    const Desktop::Types::Capabilities capabilities = Desktop::getCapabilities().capabilities;
    Desktop::Window window;
    if (!openManualWindow(context, window, "GameWIP opacity and pointer validation"))
        return;

    if (capabilities.supports(Desktop::Types::Capability::Opacity))
    {
        context.manual("Place another high-contrast application beneath the validation Window as the opacity and input reference.");
        for (const float opacity : {1.0F, 0.5F, 0.0F})
        {
            static_cast<void>(context.expectTrue("opacity transition succeeds", window.setOpacity(opacity).ok()));
            recordManualCheck(
                context,
                window,
                "opacity presentation and input",
                std::format(
                    "The diagnostics show cached opacity {:.1f}. Over the reference application, is the visual blend correct? At 0.0 the "
                    "validation "
                    "Window should be invisible but still block clicks; at 1.0 it should be opaque.",
                    opacity));
        }
        static_cast<void>(window.setOpacity(1.0F));
    }
    else
    {
        context.skip("opacity presentation and input", "backend does not advertise Opacity");
    }

    if (capabilities.supports(Desktop::Types::Capability::PointerClickThrough))
    {
        context.manual("Place another interactive application beneath the validation Window before continuing.");
        static_cast<void>(context.expectTrue(
            "whole-Window click-through applies",
            window.setPointerInputLayout({.mode = Desktop::Types::PointerInputMode::ClickThrough}).ok()));
        recordManualCheck(
            context,
            window,
            "cross-application click-through",
            "The diagnostics show pointerMode=1. Do clicks through both client and system-frame areas reach the different "
            "application underneath?");
        static_cast<void>(context.expectTrue("normal pointer routing restores", window.setPointerInputLayout({}).ok()));
        recordManualCheck(
            context,
            window,
            "normal pointer routing restoration",
            "The diagnostics show pointerMode=0. Do the validation Window title bar, resize border, and client now intercept input again?");
    }
    else
    {
        context.skip("cross-application click-through", "backend does not advertise PointerClickThrough");
    }

    const std::array region{Desktop::Types::LogicalRect{{20, 20}, {120, 80}}};
    for (const Desktop::Types::PointerInputMode mode :
         {Desktop::Types::PointerInputMode::AcceptRegions, Desktop::Types::PointerInputMode::IgnoreRegions})
    {
        Desktop::Types::PointerInputLayout regionLayout{.mode = mode, .regions = region};
        const IO::Types::Status status = window.setPointerInputLayout(regionLayout);
        if (capabilities.supports(Desktop::Types::Capability::PointerRegions))
        {
            static_cast<void>(context.expectTrue("supported pointer-region layout applies", status.ok()));
            recordManualCheck(
                context,
                window,
                "cross-application pointer regions",
                "Does the configured rectangle route input according to the requested region mode against another application?");
        }
        else
        {
            static_cast<void>(context.expectEq("unsupported pointer-region layout is rejected", ErrorCode::Unsupported, status.code));
        }
    }
    static_cast<void>(window.setPointerInputLayout({}));

    context.pass("pointer-mask first/last pixels, clearing, movement, resize invalidation, and stale generations are covered deterministically");
    static_cast<void>(context.expectTrue("layered pointer Window closes", window.close().ok()));

    Desktop::Types::Description alphaDescription;
    alphaDescription.transparentFramebuffer = true;
    Desktop::Window alpha;
    const IO::Types::Status alphaStatus = alpha.open(alphaDescription);
    if (capabilities.supports(Desktop::Types::Capability::TransparentFramebuffer))
    {
        static_cast<void>(context.expectTrue("transparent framebuffer Window opens", alphaStatus.ok()));
        if (alpha.isOpen())
        {
            static_cast<void>(alpha.show());
            context.skip(
                "transparent framebuffer presentation",
                "GameWIPTests has no renderer-provided alpha surface; run this observation in a renderer-backed host");
            static_cast<void>(alpha.close());
        }
    }
    else
    {
        static_cast<void>(context.expectEq("unsupported transparent framebuffer is rejected", ErrorCode::Unsupported, alphaStatus.code));
    }
}

/// @brief Exercises mixed-DPI policies, coordinate conversion, and physical monitor geometry.
void testManualDpiAndCoordinates(TestSupport::Context &context, const GameWIP::Test::DesktopTestOptions &options)
{
    if (!beginManualSuite(context, options, "Window DPI and coordinates"))
        return;

    Desktop::Window window;
    Desktop::Types::Description description;
    description.dpiResizePolicy = Desktop::Types::DpiResizePolicy::PreserveLogicalClientSize;
    if (!openManualWindow(context, window, "GameWIP mixed-DPI validation", description))
        return;

    recordManualCheck(
        context,
        window,
        "mixed-DPI logical-size policy",
        "With differently scaled monitors arranged on either side of primary when available, move the Window across boundaries. Does logical "
        "size remain stable while framebuffer pixels change?");

    static_cast<void>(context.expectTrue(
        "physical-size DPI policy applies",
        window.setDpiResizePolicy(Desktop::Types::DpiResizePolicy::PreservePhysicalClientSize).ok()));
    recordManualCheck(
        context,
        window,
        "mixed-DPI physical-size policy",
        "Move across the same DPI boundaries. Do framebuffer pixels remain stable while logical size changes?");

    const Desktop::Types::LogicalSize size = window.clientSize();
    const std::array points{
        Desktop::Types::LogicalPosition{0, 0},
        Desktop::Types::LogicalPosition{static_cast<std::int32_t>(size.width - 1), 0},
        Desktop::Types::LogicalPosition{0, static_cast<std::int32_t>(size.height - 1)},
        Desktop::Types::LogicalPosition{static_cast<std::int32_t>(size.width - 1), static_cast<std::int32_t>(size.height - 1)}};
    bool conversionsRoundTrip = true;
    for (const Desktop::Types::LogicalPosition point : points)
    {
        const Desktop::Types::ScreenPositionResult screen = window.clientToScreen(point);
        if (!screen.status.ok())
        {
            conversionsRoundTrip = false;
            break;
        }
        const Desktop::Types::LogicalPositionResult logical = window.screenToClient(screen.position);
        conversionsRoundTrip = conversionsRoundTrip && logical.status.ok() && logical.position == point;
    }
    static_cast<void>(context.expectTrue("client/screen edge conversions round-trip", conversionsRoundTrip));
    recordManualCheck(
        context,
        window,
        "mixed-DPI coordinate rounding",
        "At each available DPI scale, do client/screen edge coordinates follow the expected integral-pixel rounding without visible drift?");

    const Desktop::Types::Display::MonitorsResult monitors = Desktop::Display::getMonitors();
    bool monitorRectsValid = monitors.status.ok() && !monitors.monitors.empty();
    for (const Desktop::Types::Display::Info &monitor : monitors.monitors)
    {
        monitorRectsValid = monitorRectsValid && monitor.bounds.size.width > 0 && monitor.bounds.size.height > 0 && monitor.workArea.size.width > 0 &&
                            monitor.workArea.size.height > 0;
    }
    static_cast<void>(context.expectTrue("physical monitor bounds and work areas are valid", monitorRectsValid));
    recordManualCheck(
        context,
        window,
        "physical virtual-screen monitor rectangles",
        "Do reported monitor bounds and work areas match the Windows virtual-screen arrangement, including negative origins, without independent "
        "scaling?");
    static_cast<void>(window.close());
}

/// @brief Exercises standard shapes, focus-sensitive cursor modes, and logical warping.
void testManualCursor(TestSupport::Context &context, const GameWIP::Test::DesktopTestOptions &options)
{
    if (!beginManualSuite(context, options, "Window cursor behavior"))
        return;

    const Desktop::Types::Capabilities capabilities = Desktop::getCapabilities().capabilities;
    Desktop::Window window;
    if (!openManualWindow(context, window, "GameWIP cursor validation"))
        return;

    constexpr std::array shapes{
        Desktop::Types::CursorShape::Arrow,
        Desktop::Types::CursorShape::Text,
        Desktop::Types::CursorShape::Crosshair,
        Desktop::Types::CursorShape::Hand,
        Desktop::Types::CursorShape::Help,
        Desktop::Types::CursorShape::Wait,
        Desktop::Types::CursorShape::Progress,
        Desktop::Types::CursorShape::Move,
        Desktop::Types::CursorShape::ResizeAll,
        Desktop::Types::CursorShape::ResizeHorizontal,
        Desktop::Types::CursorShape::ResizeVertical,
        Desktop::Types::CursorShape::ResizeDiagonalNorthWestSouthEast,
        Desktop::Types::CursorShape::ResizeDiagonalNorthEastSouthWest,
        Desktop::Types::CursorShape::NotAllowed};
    constexpr std::array shapeNames{
        std::string_view{"Arrow"},
        std::string_view{"Text"},
        std::string_view{"Crosshair"},
        std::string_view{"Hand"},
        std::string_view{"Help"},
        std::string_view{"Wait"},
        std::string_view{"Progress"},
        std::string_view{"Move"},
        std::string_view{"ResizeAll"},
        std::string_view{"ResizeHorizontal"},
        std::string_view{"ResizeVertical"},
        std::string_view{"ResizeDiagonalNorthWestSouthEast"},
        std::string_view{"ResizeDiagonalNorthEastSouthWest"},
        std::string_view{"NotAllowed"}};
    for (std::size_t index = 0; index < shapes.size(); ++index)
    {
        static_cast<void>(context.expectTrue("standard cursor shape applies", window.setCursorShape(shapes[index]).ok()));
        const Desktop::Types::LogicalSize size = window.clientSize();
        static_cast<void>(context.expectTrue(
            "cursor moves into the visible validation surface",
            window.setCursorPosition({static_cast<std::int32_t>(size.width / 2), static_cast<std::int32_t>(size.height / 2)}).ok()));
        recordManualCheck(
            context,
            window,
            "standard cursor shape",
            std::format(
                "The diagnostics show cursorShape={}. Does the '{}' system cursor display correctly over the validation client?",
                static_cast<int>(shapes[index]),
                shapeNames[index]));
    }

    for (const auto &[mode, capability, name] : std::array{
             std::tuple{Desktop::Types::CursorMode::Hidden, Desktop::Types::Capability::Count, std::string_view{"hidden"}},
             std::tuple{Desktop::Types::CursorMode::Confined, Desktop::Types::Capability::CursorConfinement, std::string_view{"confined"}},
             std::tuple{
                 Desktop::Types::CursorMode::HiddenConfined,
                 Desktop::Types::Capability::CursorConfinement,
                 std::string_view{"hidden-confined"}},
             std::tuple{Desktop::Types::CursorMode::Relative, Desktop::Types::Capability::RelativeCursor, std::string_view{"relative"}}})
    {
        if (capability != Desktop::Types::Capability::Count && !capabilities.supports(capability))
        {
            context.skip(std::format("{} cursor mode", name), "backend does not advertise the required capability");
            continue;
        }
        static_cast<void>(context.expectTrue("cursor mode applies", window.setCursorMode(mode).ok()));
        recordManualCheck(
            context,
            window,
            std::format("{} cursor mode", name),
            std::format(
                "Exercise {} mode while focused, then alt-tab, minimize, hide, restore, and refocus. Is the system cursor always released and "
                "reacquired correctly?",
                name));
        static_cast<void>(window.setCursorMode(Desktop::Types::CursorMode::Normal));
    }

    if (capabilities.supports(Desktop::Types::Capability::CursorWarping))
    {
        const Desktop::Types::LogicalSize size = window.clientSize();
        for (const Desktop::Types::LogicalPosition point :
             {Desktop::Types::LogicalPosition{0, 0},
              Desktop::Types::LogicalPosition{static_cast<std::int32_t>(size.width - 1), static_cast<std::int32_t>(size.height - 1)}})
        {
            static_cast<void>(context.expectTrue("cursor warp succeeds", window.setCursorPosition(point).ok()));
            const Desktop::Types::LogicalPositionResult actual = window.cursorPosition();
            static_cast<void>(context.expectTrue("cursor warp query succeeds", actual.status.ok()));
            if (actual.status.ok())
                static_cast<void>(context.expectEq("cursor warp reaches requested logical point", point, actual.position));
        }
        recordManualCheck(
            context,
            window,
            "cursor corner warping",
            "Did cursor warping reach both logical client corners correctly at the available DPI scales?");
    }
    else
    {
        context.skip("cursor corner warping", "backend does not advertise CursorWarping");
    }
    static_cast<void>(window.setCursorMode(Desktop::Types::CursorMode::Normal));
    static_cast<void>(window.close());
}
