/// @file desktop_manual_chrome_tests.inl
/// @brief Window chrome and layered-pointer manual validation.

/// ------------------------------------------------------------
/// Chrome and pointer behavior
/// ------------------------------------------------------------

/// @brief Exercises custom non-client hit testing and runtime layout replacement.
void testManualCustomChrome(TestSupport::Context &context, const GameWIP::Test::DesktopTestOptions &options)
{
    if(!beginManualSuite(context, options, "Window custom chrome"))
    {
        return;
    }

    const auto capabilitiesResult = Desktop::getCapabilities();
    const Desktop::Types::Capabilities capabilities = capabilitiesResult.value_or({});

    if(!capabilities.supports(Desktop::Types::Capability::CustomChrome))
    {
        context.skip("Window custom chrome", "backend does not advertise CustomChrome");
        return;
    }
    Desktop::Types::Description description;
    description.decoration = Desktop::Types::DecorationMode::Custom;
    Desktop::Window window;

    if(!openManualWindow(context, window, "GameWIP custom chrome validation", description))
    {
        return;
    }

    const std::array draggable{Desktop::Types::LogicalRect{{0, 0}, {760, 48}}};
    Desktop::Types::CustomChromeLayout layout;
    layout.draggableRegions = draggable;
    layout.systemMenuRegion = Desktop::Types::LogicalRect{{0, 0}, {48, 48}};
    layout.minimizeButtonRegion = Desktop::Types::LogicalRect{{800, 0}, {48, 48}};
    layout.maximizeButtonRegion = Desktop::Types::LogicalRect{{848, 0}, {48, 48}};
    layout.closeButtonRegion = Desktop::Types::LogicalRect{{896, 0}, {64, 48}};

    if(!requireManualStatus(context, "custom chrome layout applies", window.setCustomChromeLayout(layout)))
    {
        return;
    }

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
    if(!beginManualSuite(context, options, "Window layered and pointer behavior"))
    {
        return;
    }

    const auto capabilitiesResult = Desktop::getCapabilities();
    const Desktop::Types::Capabilities capabilities = capabilitiesResult.value_or({});
    Desktop::Window window;

    if(!openManualWindow(context, window, "GameWIP opacity and pointer validation"))
    {
        return;
    }

    if(capabilities.supports(Desktop::Types::Capability::Opacity))
    {
        context.manual("Place another high-contrast application beneath the validation Window as the opacity and input reference.");

        for(const float opacity : {1.0F, 0.5F, 0.0F})
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

    if(capabilities.supports(Desktop::Types::Capability::PointerClickThrough))
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

    for(const Desktop::Types::PointerInputMode mode :
        {Desktop::Types::PointerInputMode::AcceptRegions, Desktop::Types::PointerInputMode::IgnoreRegions})
    {
        const Desktop::Types::PointerInputLayout regionLayout{.mode = mode, .regions = region};
        const IO::Types::Status status = window.setPointerInputLayout(regionLayout);

        if(capabilities.supports(Desktop::Types::Capability::PointerRegions))
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

    if(capabilities.supports(Desktop::Types::Capability::TransparentFramebuffer))
    {
        static_cast<void>(context.expectTrue("transparent framebuffer Window opens", alphaStatus.ok()));

        if(alpha.isOpen())
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
