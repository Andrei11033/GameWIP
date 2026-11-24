/// @file desktop_manual_lifecycle_tests.inl
/// @brief Visible Window lifecycle and ownership manual validation.

/// ------------------------------------------------------------
/// Visible lifecycle and ownership
/// ------------------------------------------------------------

/// @brief Exercises the core real visible-window lifecycle required before submission.
void testManualVisibleLifecycle(TestSupport::Context &context, const GameWIP::Test::DesktopTestOptions &options)
{
    if(!beginManualSuite(context, options, "Window visible lifecycle"))
    {
        return;
    }

    // Open a visible Window and verify system-frame lifecycle actions.
    context.manual(
        "Window manual tests require a normal interactive Windows desktop. Answer yes, no, or skip at each prompt. "
        "Restore any changed desktop state before the suite ends.");

    Desktop::Types::Description description;
    description.title = "GameWIP Window manual validation";
    description.windowed.clientSize = {960, 540};
    description.activation.visible = true;
    description.activation.requestFocus = true;

    Desktop::Window window;
    const IO::Types::Status openStatus = window.open(description);

    if(!openStatus.ok())
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

    // Confirm close requests remain explicit until the owner closes the Window.
    recordManualCheck(
        context,
        window,
        "system close request",
        "Click the Window close button once. Does the Window remain alive while recording a close request?");
    static_cast<void>(context.expectTrue("system close request becomes sticky", window.hasCloseRequest()));

    if(window.hasCloseRequest())
    {
        static_cast<void>(context.expectTrue("system close request clears", window.clearCloseRequest().ok()));
    }
    static_cast<void>(context.expectTrue("cleared close request leaves Window open", window.isOpen()));

    recordManualCheck(
        context,
        window,
        "second system close request",
        "Click the Window close button again. Does the Window again remain alive pending explicit close?");
    static_cast<void>(context.expectTrue("second system close request becomes sticky", window.hasCloseRequest()));
    static_cast<void>(context.expectTrue("manual Window explicit close succeeds", window.close().ok()));
    static_cast<void>(context.expectFalse("manual Window is closed", window.isOpen()));

    // Reopen in a hidden state and verify cleanup after reuse.
    Desktop::Window reopened;
    description.activation.visible = false;
    description.activation.requestFocus = false;
    static_cast<void>(context.expectTrue("manual Window reopens after explicit close", reopened.open(description).ok()));
    static_cast<void>(context.expectTrue("reopened manual Window closes cleanly", reopened.close().ok()));
}

/// @brief Exercises visible multi-window ownership, routing, activation, and native relationship behavior.
void testManualMultipleWindows(TestSupport::Context &context, const GameWIP::Test::DesktopTestOptions &options)
{
    if(!beginManualSuite(context, options, "Window multiple-window scenarios"))
    {
        return;
    }

    // Open an owner and a visible owned tool Window.
    Desktop::Window owner;

    if(!openManualWindow(context, owner, "GameWIP manual owner Window"))
    {
        return;
    }

    Desktop::Types::Description childDescription;
    childDescription.owner = owner.id();
    Desktop::Window child;

    if(!openManualWindow(context, child, "GameWIP manual owned tool Window", childDescription))
    {
        return;
    }

    const auto paintOwnedWindow = [&]
    {
        paintManualValidationSurface(child);
    };

    // Verify independent queues and native ownership transitions.
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

    static_cast<void>(context.expectTrue("owned Window owner removal succeeds", child.setOwner(std::nullopt).ok()));
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

    // Check show-without-activation and explicit focus behavior.
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
