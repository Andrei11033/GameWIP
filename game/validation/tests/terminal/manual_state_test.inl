/// @file manual_state_test.inl
/// @brief Manual Terminal state-restoration tests.

/// ------------------------------------------------------------
/// State restoration and suite selection
/// ------------------------------------------------------------

/// @brief Verifies managed Session input restoration and cursor visibility restoration.
void testManualStateRestoration(
    TestSupport::Context &context,
    const Terminal::Types::Input::Capabilities &inputCapabilities,
    const Terminal::Types::Output::Capabilities &outputCapabilities)
{
    if(!inputCapabilities.supportsLineInput)
    {
        context.skip("manual session restoration", "the terminal does not report managed line input support");
    }
    else
    {
        Terminal::Session session;
        Terminal::Types::SessionOptions options;
        options.deliveryMode = Terminal::Types::Input::DeliveryMode::Stream;

        const IO::Types::Status openStatus = session.open(options);

        if(requireManualOperation(context, "manual session restoration", "open managed input session", openStatus))
        {
            if(!requireManualOperation(
                   context,
                   "manual session restoration",
                   "session-input prompt writeText",
                   Terminal::writeText("State restoration: type hidden and press Enter: ")))
            {
                static_cast<void>(session.close());
                return;
            }
            const Terminal::Types::Input::LineResult hiddenInput = session.readLine();
            bool readSucceeded = requireManualOperation(context, "manual session restoration", "read managed session input", hiddenInput.status);

            if(readSucceeded && (hiddenInput.outcome != Terminal::Types::Input::ReadOutcome::Completed || hiddenInput.line != "hidden"))
            {
                context.fail("manual session restoration", "managed hidden input did not produce the requested line");
                readSucceeded = false;
            }

            const bool closeSucceeded =
                requireManualOperation(context, "manual session restoration", "close and restore managed session", session.close());

            if(readSucceeded && closeSucceeded)
            {
                recordManualCheck(
                    context,
                    "manual session restoration",
                    "Did input behave normally during the Stream session, and is normal terminal input behavior still intact after close?");
            }
        }
    }

    if(!outputCapabilities.supportsCursorVisibility)
    {
        context.skip("manual cursor-visibility restoration", "the terminal does not report cursor visibility support");
        return;
    }

    Terminal::CursorHiddenScope hiddenCursor = Terminal::scopedCursorHidden();

    if(!requireManualOperation(context, "manual cursor-visibility restoration", "hide cursor", hiddenCursor.status()))
    {
        return;
    }

    if(!hiddenCursor.active())
    {
        context.fail("manual cursor-visibility restoration", "cursor-hidden scope did not become active");
        return;
    }

    std::string skipReason;
    const TestSupport::Types::Reporting::ManualAnswer hiddenAnswer =
        TestSupport::promptManualCheck("Is the terminal cursor currently hidden?", skipReason);

    if(!requireManualOperation(context, "manual cursor-visibility restoration", "restore cursor visibility", hiddenCursor.restore()))
    {
        return;
    }
    recordManualAnswer(context, "manual cursor hidden state", hiddenAnswer, skipReason);
    recordManualCheck(
        context,
        "manual cursor-visibility restoration",
        "Is the cursor visible again with the main screen, default style, and normal input behavior intact?");
}

/// @brief Runs the opt-in human checks for Terminal UI behavior.
void testManualUiChecks(TestSupport::Context &context, const TerminalTestOptions &options)
{
    if(!options.enableManualTests)
    {
        context.skip("Terminal manual tests", "disabled by TerminalTestOptions");
        return;
    }

    const Terminal::Types::Output::CapabilitiesResult output = Terminal::prepareOutput();
    const Terminal::Types::Input::CapabilitiesResult input = Terminal::getInputCapabilities();

    if(!requireManualOperation(context, "manual terminal capability setup", "prepare stdout", output.status) ||
       !requireManualOperation(context, "manual terminal capability setup", "query stdin", input.status))
    {
        return;
    }

    if(output.capabilities.kind != Terminal::Types::StreamKind::Terminal || input.capabilities.kind != Terminal::Types::StreamKind::Terminal)
    {
        context.skip("Terminal manual tests", "requires real terminal stdin and stdout");
        return;
    }

    context.manual("Terminal UI checks: verify each observation and answer yes, no, or skip when prompted.");
    testManualUnicodeOutput(context);
    testManualBasicColorOutput(context, output.capabilities.style);
    testManualRgbColorOutput(context, output.capabilities.style);
    testManualTextStyles(context, output.capabilities.style);
    testManualStyleRestoration(context, output.capabilities.style);
    testManualCursorBehavior(context, output.capabilities);
    testManualAlternateScreen(context, output.capabilities);
    testManualInput(context);
    testManualWrappedLineEditing(context);
    testManualEventInput(context, input.capabilities);
    testManualStateRestoration(context, input.capabilities, output.capabilities);
}
