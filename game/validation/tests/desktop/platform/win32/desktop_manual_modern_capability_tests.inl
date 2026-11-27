/// @file desktop_manual_modern_capability_tests.inl
/// @brief Modern Windows capability manual validation.

/// ------------------------------------------------------------
/// Runtime-gated Windows capabilities
/// ------------------------------------------------------------

/// @brief Exercises runtime-gated backdrop effects and renderer-provided framebuffer alpha.
void testManualModernWindowsCapabilities(TestSupport::Context &context, const GameWIP::Test::DesktopTestOptions &options)
{
    if(!beginManualSuite(context, options, "Window modern Windows capabilities"))
    {
        return;
    }

    // Read runtime-gated capabilities and open the host Window.
    const auto capabilitiesResult = Desktop::getCapabilities();
    if(!capabilitiesResult.ok())
    {
        context.fail("Window modern capabilities", manualStatusDetails(capabilitiesResult.status()));
        return;
    }
    const Desktop::Types::Capabilities &capabilities = capabilitiesResult.value();
    Desktop::Window window;

    if(!openManualWindow(context, window, "GameWIP modern capability validation"))
    {
        return;
    }

    constexpr std::array effects{
        Desktop::Types::BackdropEffect::Automatic,
        Desktop::Types::BackdropEffect::MainWindow,
        Desktop::Types::BackdropEffect::TransientWindow,
        Desktop::Types::BackdropEffect::TabbedWindow};

    // Apply each backdrop effect only when the backend advertises support.
    if(capabilities.supports(Desktop::Types::Capability::SystemBackdrop))
    {
        for(std::size_t index = 0; index < effects.size(); ++index)
        {
            const GameWIP::Status::Types::Result<void> status = window.setBackdropEffect(effects[index]);
            static_cast<void>(context.expectTrue("supported backdrop effect applies", status.ok()));

            if(status.ok())
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
        for(const Desktop::Types::BackdropEffect effect : effects)
        {
            static_cast<void>(context.expectEq(
                "unsupported backdrop effect is rejected",
                ErrorCode::Unsupported,
                window.setBackdropEffect(effect).status().code()));
        }
        context.skip("system backdrop presentation", "runtime does not advertise SystemBackdrop");
    }

    static_cast<void>(window.close());

    // Check transparent framebuffer support separately from backdrop effects.
    Desktop::Types::Description alphaDescription;
    alphaDescription.transparentFramebuffer = true;
    Desktop::Window alpha;
    const GameWIP::Status::Types::Result<void> alphaStatus = alpha.open(alphaDescription);

    if(capabilities.supports(Desktop::Types::Capability::TransparentFramebuffer))
    {
        static_cast<void>(context.expectTrue("modern alpha Window opens", alphaStatus.ok()));

        if(alpha.isOpen())
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
        static_cast<void>(context.expectEq("unsupported modern alpha Window is rejected", ErrorCode::Unsupported, alphaStatus.status().code()));
    }
}
