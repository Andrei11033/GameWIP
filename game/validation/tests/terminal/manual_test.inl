/// @file manual_test.inl
/// @brief Focused terminal manual correctness suites.

/// @brief Displays representative UTF-8 text for visual verification.
void testManualUnicodeOutput(TestSupport::Context &context)
{
    constexpr std::string_view sample = "Unicode: cafe\xCC\x81 | \xCE\x95\xCE\xBB\xCE\xBB\xCE\xB7\xCE\xBD\xCE\xB9\xCE\xBA\xCE\xAC | "
                                        "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E | \xF0\x9F\x98\x80";
    if (!requireManualOperation(context, "manual Unicode output", "writeLine", Terminal::writeLine(sample)))
    {
        return;
    }

    recordManualCheck(
        context,
        "manual Unicode output",
        "Does the preceding Unicode line show accented text, Greek, Japanese, and an emoji without replacement characters?");
}

/// @brief Displays portable basic colors and verifies that they remain visually distinct.
void testManualBasicColorOutput(TestSupport::Context &context, const Terminal::Types::Style::Capabilities &capabilities)
{
    if (!capabilities.basicColor)
    {
        context.skip("manual basic-color output", "the terminal does not report basic-color support");
        return;
    }

    Terminal::Types::Output::LineOptions redOptions;
    redOptions.styleMode = Terminal::Types::Style::Mode::Required;
    redOptions.style.foreground = Terminal::basicColor(Terminal::Types::Style::BasicColor::BrightRed);

    Terminal::Types::Output::LineOptions greenOptions;
    greenOptions.styleMode = Terminal::Types::Style::Mode::Required;
    greenOptions.style.foreground = Terminal::basicColor(Terminal::Types::Style::BasicColor::BrightGreen);

    Terminal::Types::Output::LineOptions blueOptions;
    blueOptions.styleMode = Terminal::Types::Style::Mode::Required;
    blueOptions.style.foreground = Terminal::basicColor(Terminal::Types::Style::BasicColor::BrightBlue);

    if (!requireManualOperation(
            context,
            "manual basic-color output",
            "red basic-color writeLine",
            Terminal::writeLine("Basic color: bright red", redOptions)) ||
        !requireManualOperation(
            context,
            "manual basic-color output",
            "green basic-color writeLine",
            Terminal::writeLine("Basic color: bright green", greenOptions)) ||
        !requireManualOperation(
            context,
            "manual basic-color output",
            "blue basic-color writeLine",
            Terminal::writeLine("Basic color: bright blue", blueOptions)))
    {
        return;
    }

    recordManualCheck(
        context,
        "manual basic-color output",
        "Are the preceding basic-color lines visibly red, green, and blue according to the terminal palette?");
}

/// @brief Displays an RGB color when the terminal can honor exact RGB requests.
void testManualRgbColorOutput(TestSupport::Context &context, const Terminal::Types::Style::Capabilities &capabilities)
{
    if (!capabilities.rgbColor)
    {
        context.skip("manual RGB-color output", "the terminal does not report RGB-color support");
        return;
    }

    Terminal::Types::Output::LineOptions rgbOptions;
    rgbOptions.styleMode = Terminal::Types::Style::Mode::Required;
    rgbOptions.style.foreground = Terminal::rgbColor(40, 210, 120);

    if (!requireManualOperation(
            context,
            "manual RGB-color output",
            "RGB styled writeLine",
            Terminal::writeLine("RGB color: red=40 green=210 blue=120", rgbOptions)))
    {
        return;
    }

    recordManualCheck(context, "manual RGB-color output", "Does the preceding RGB line appear as the requested vivid green color?");
}

/// @brief Displays each text attribute independently when the terminal advertises it.
void testManualTextStyles(TestSupport::Context &context, const Terminal::Types::Style::Capabilities &capabilities)
{
    bool wroteAttribute = false;
    const auto testAttribute = [&](std::string_view label, bool supported, bool Terminal::Types::Style::Request::*attribute) -> bool
    {
        const std::string name = std::format("manual {} text style", label);
        if (!supported)
        {
            context.skip(name, "the terminal does not report this text-style capability");
            return true;
        }

        Terminal::Types::Output::LineOptions options;
        options.styleMode = Terminal::Types::Style::Mode::Required;
        options.style.*attribute = true;
        wroteAttribute = true;
        return requireManualOperation(
            context,
            name,
            std::format("{} styled writeLine", label),
            Terminal::writeLine(std::format("Text style: {}", label), options));
    };

    if (!testAttribute("bold", capabilities.bold, &Terminal::Types::Style::Request::bold) ||
        !testAttribute("dim", capabilities.dim, &Terminal::Types::Style::Request::dim) ||
        !testAttribute("italic", capabilities.italic, &Terminal::Types::Style::Request::italic) ||
        !testAttribute("underline", capabilities.underline, &Terminal::Types::Style::Request::underline) ||
        !testAttribute("inverse", capabilities.inverse, &Terminal::Types::Style::Request::inverse) ||
        !testAttribute("strikethrough", capabilities.strikethrough, &Terminal::Types::Style::Request::strikethrough))
    {
        return;
    }

    if (!wroteAttribute)
    {
        context.skip("manual text styles", "the terminal does not report any text-style capability");
        return;
    }

    recordManualCheck(context, "manual text styles", "Did each displayed text-style line visibly match its label?");
}

/// @brief Verifies that a styled write does not leak state into the following plain write.
void testManualStyleRestoration(TestSupport::Context &context, const Terminal::Types::Style::Capabilities &capabilities)
{
    if (!capabilities.basicColor)
    {
        context.skip("manual style restoration", "the terminal does not report basic-color support");
        return;
    }

    Terminal::Types::Output::LineOptions styledOptions;
    styledOptions.styleMode = Terminal::Types::Style::Mode::Required;
    styledOptions.style.foreground = Terminal::basicColor(Terminal::Types::Style::BasicColor::BrightMagenta);
    styledOptions.style.bold = capabilities.bold;
    styledOptions.style.underline = capabilities.underline;

    if (!requireManualOperation(
            context,
            "manual style restoration",
            "styled writeLine",
            Terminal::writeLine("Style restoration: styled magenta line", styledOptions)) ||
        !requireManualOperation(
            context,
            "manual style restoration",
            "plain writeLine",
            Terminal::writeLine("Style restoration: terminal defaults restored")))
    {
        return;
    }

    recordManualCheck(
        context,
        "manual style restoration",
        "Is only the first restoration line styled, with the second line using the terminal defaults?");
}

/// @brief Verifies visible cursor save/restore behavior without changing the final cursor position.
void testManualCursorBehavior(TestSupport::Context &context, const Terminal::Types::Output::Capabilities &capabilities)
{
    if (!capabilities.supportsCursorSaveRestore)
    {
        context.skip("manual cursor behavior", "the terminal does not report cursor save/restore support");
        return;
    }

    if (!requireManualOperation(context, "manual cursor behavior", "saveCursorPosition", Terminal::saveCursorPosition()) ||
        !requireManualOperation(
            context,
            "manual cursor behavior",
            "placeholder writeText",
            Terminal::writeText("Cursor: this placeholder should be replaced                 ")) ||
        !requireManualOperation(context, "manual cursor behavior", "restoreCursorPosition", Terminal::restoreCursorPosition()) ||
        !requireManualOperation(
            context,
            "manual cursor behavior",
            "replacement writeLine",
            Terminal::writeLine("Cursor: PASS - saved position restored                     ")))
    {
        return;
    }

    recordManualCheck(context, "manual cursor behavior", "Is there one Cursor: PASS line above, with no visible placeholder line?");
}

/// @brief Enters an alternate screen, checks its contents, and restores the main screen.
void testManualAlternateScreen(TestSupport::Context &context, const Terminal::Types::Output::Capabilities &capabilities)
{
    if (!capabilities.supportsAlternateScreen || !capabilities.supportsClear)
    {
        context.skip("manual alternate screen", "the terminal does not report alternate-screen and clear support");
        return;
    }

    Terminal::AlternateScreenScope alternateScreen = Terminal::scopedAlternateScreen();
    if (!requireManualOperation(context, "manual alternate screen", "enter alternate screen", alternateScreen.status()))
    {
        return;
    }
    if (!alternateScreen.active())
    {
        context.fail("manual alternate screen", "alternate-screen scope did not become active");
        return;
    }

    TestSupport::Types::Reporting::ManualAnswer answer = TestSupport::Types::Reporting::ManualAnswer::Skipped;
    const bool contentReady = requireManualOperation(context, "manual alternate screen", "clear alternate screen", Terminal::clear()) &&
                              requireManualOperation(
                                  context,
                                  "manual alternate screen",
                                  "alternate-screen writeLine",
                                  Terminal::writeLine("GameWIP Terminal alternate-screen check"));
    if (contentReady)
    {
        answer = TestSupport::promptManualCheck("Is this prompt displayed on a clean alternate screen?");
    }

    const bool restored = requireManualOperation(context, "manual alternate screen", "leave alternate screen", alternateScreen.leave());
    if (contentReady && restored)
    {
        recordManualAnswer(context, "manual alternate screen", answer);
    }
}

/// @brief Reads one exact line from the real terminal input stream.
void testManualInput(TestSupport::Context &context)
{
    constexpr std::string_view expected = "GameWIP-11";
    if (!requireManualOperation(
            context,
            "manual terminal input",
            "input prompt writeText",
            Terminal::writeText("Input: type GameWIP-11 and press Enter: ")))
    {
        return;
    }

    const Terminal::Types::Input::LineResult result = Terminal::readLine();
    if (!requireManualOperation(context, "manual terminal input", "readLine", result.status))
    {
        return;
    }
    if (result.outcome != Terminal::Types::Input::ReadOutcome::Completed)
    {
        context.fail("manual terminal input", "readLine did not complete normally");
        return;
    }
    if (result.line != expected)
    {
        context.fail("manual terminal input", std::format("expected '{}', received '{}'", expected, result.line));
        return;
    }

    context.pass("manual terminal input");
}

/// @brief Exercises wrapped managed-line editing, viewport scroll, and live resize on a real terminal.
void testManualWrappedLineEditing(TestSupport::Context &context)
{
    const Terminal::Types::SizeResult size = Terminal::getTerminalSize();
    const Terminal::Types::Cursor::PositionResult position = Terminal::getCursorPosition();
    if (!requireManualOperation(context, "manual wrapped line editing", "query terminal size", size.status) ||
        !requireManualOperation(context, "manual wrapped line editing", "query cursor position", position.status))
    {
        return;
    }

    // Place the prompt close enough to the viewport bottom that wrapping exercises visible scrolling.
    if (size.size.rows > 2 && position.position.row + 2 < size.size.rows)
    {
        const std::uint32_t blankLines = size.size.rows - position.position.row - 2;
        for (std::uint32_t line = 0; line < blankLines; ++line)
        {
            if (!requireManualOperation(context, "manual wrapped line editing", "position prompt near viewport bottom", Terminal::writeLine()))
            {
                return;
            }
        }
    }

    const std::uint32_t minimumCharacters = size.size.columns + 10;
    context.manual(
        std::format(
            "Wrapped input check: type at least {} ASCII characters. Before Enter, use Left, Home, End, Backspace, and Delete to edit both "
            "rows; resize the terminal narrower and wider while the line is active. Watch for stale or misplaced text.",
            minimumCharacters));
    if (!requireManualOperation(context, "manual wrapped line editing", "wrapped-input prompt", Terminal::writeText("Wrapped/resize input: ")))
    {
        return;
    }

    const Terminal::Types::Input::LineResult result = Terminal::readLine();
    if (!requireManualOperation(context, "manual wrapped line editing", "wrapped readLine", result.status))
    {
        return;
    }
    if (result.outcome != Terminal::Types::Input::ReadOutcome::Completed || result.line.size() < minimumCharacters)
    {
        context.fail("manual wrapped line editing", "the completed line was not long enough to prove wrapping");
        return;
    }

    recordManualCheck(
        context,
        "manual wrapped line editing",
        "Did wrapping, scrolling, navigation, editing, and redraw remain coherent before and after resize, with no stale text?");
}

/// @brief Verifies native structured key delivery through a real interactive terminal session.
void testManualEventInput(TestSupport::Context &context, const Terminal::Types::Input::Capabilities &inputCapabilities)
{
    if (!inputCapabilities.supportsEventInput)
    {
        context.skip("manual structured input", "the terminal does not report structured event support");
        return;
    }

    if (!requireManualOperation(
            context,
            "manual structured input",
            "event prompt writeText",
            Terminal::writeText("Event input: press the Left Arrow key: ")))
    {
        return;
    }

    Terminal::Session session;
    const IO::Types::Status openStatus = session.open();
    if (!requireManualOperation(context, "manual structured input", "open event session", openStatus))
    {
        return;
    }

    Terminal::Types::Input::EventOptions options;
    options.timeout = std::chrono::seconds{10};

    bool matched = false;
    for (int attempt = 0; attempt < 8 && !matched; ++attempt)
    {
        const Terminal::Types::Input::EventResult event = session.readEvent(options);
        if (!event.status.ok())
        {
            static_cast<void>(requireManualOperation(context, "manual structured input", "readEvent", event.status));
            break;
        }
        if (event.outcome != Terminal::Types::Input::ReadOutcome::Completed || !event.event.has_value())
        {
            context.fail("manual structured input", "readEvent did not produce a key event before the deadline");
            break;
        }

        const Terminal::Types::Events::Key *key = event.event->getIf<Terminal::Types::Events::Key>();
        if (key == nullptr || key->action == Terminal::Types::Events::KeyAction::Release)
        {
            continue;
        }

        const auto *named = std::get_if<Terminal::Types::Events::NamedKey>(&key->key);
        matched = named != nullptr && *named == Terminal::Types::Events::NamedKey::ArrowLeft;
    }

    const bool closed = requireManualOperation(context, "manual structured input", "close event session", session.close());
    if (closed)
    {
        static_cast<void>(Terminal::writeLine());
    }

    if (matched && closed)
    {
        context.pass("manual structured input");
    }
    else if (closed)
    {
        context.fail("manual structured input", "the observed key was not the portable Left Arrow event");
    }
}

/// @brief Verifies managed Session input restoration and cursor visibility restoration.
void testManualStateRestoration(
    TestSupport::Context &context,
    const Terminal::Types::Input::Capabilities &inputCapabilities,
    const Terminal::Types::Output::Capabilities &outputCapabilities)
{
    if (!inputCapabilities.supportsLineInput)
    {
        context.skip("manual session restoration", "the terminal does not report managed line input support");
    }
    else
    {
        Terminal::Session session;
        Terminal::Types::SessionOptions options;
        options.deliveryMode = Terminal::Types::Input::DeliveryMode::Stream;

        const IO::Types::Status openStatus = session.open(options);
        if (requireManualOperation(context, "manual session restoration", "open managed input session", openStatus))
        {
            if (!requireManualOperation(
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
            if (readSucceeded && (hiddenInput.outcome != Terminal::Types::Input::ReadOutcome::Completed || hiddenInput.line != "hidden"))
            {
                context.fail("manual session restoration", "managed hidden input did not produce the requested line");
                readSucceeded = false;
            }

            const bool closeSucceeded =
                requireManualOperation(context, "manual session restoration", "close and restore managed session", session.close());
            if (readSucceeded && closeSucceeded)
            {
                recordManualCheck(
                    context,
                    "manual session restoration",
                    "Did input behave normally during the Stream session, and is normal terminal input behavior still intact after close?");
            }
        }
    }

    if (!outputCapabilities.supportsCursorVisibility)
    {
        context.skip("manual cursor-visibility restoration", "the terminal does not report cursor visibility support");
        return;
    }

    Terminal::CursorHiddenScope hiddenCursor = Terminal::scopedCursorHidden();
    if (!requireManualOperation(context, "manual cursor-visibility restoration", "hide cursor", hiddenCursor.status()))
    {
        return;
    }
    if (!hiddenCursor.active())
    {
        context.fail("manual cursor-visibility restoration", "cursor-hidden scope did not become active");
        return;
    }

    const TestSupport::Types::Reporting::ManualAnswer hiddenAnswer = TestSupport::promptManualCheck("Is the terminal cursor currently hidden?");
    if (!requireManualOperation(context, "manual cursor-visibility restoration", "restore cursor visibility", hiddenCursor.restore()))
    {
        return;
    }
    recordManualAnswer(context, "manual cursor hidden state", hiddenAnswer);
    recordManualCheck(
        context,
        "manual cursor-visibility restoration",
        "Is the cursor visible again with the main screen, default style, and normal input behavior intact?");
}

/// @brief Runs the opt-in human checks for Terminal UI behavior.
void testManualUiChecks(TestSupport::Context &context, const TerminalTestOptions &options)
{
    if (!options.enableManualTests)
    {
        context.skip("Terminal manual tests", "disabled by TerminalTestOptions");
        return;
    }

    const Terminal::Types::Output::CapabilitiesResult output = Terminal::prepareOutput();
    const Terminal::Types::Input::CapabilitiesResult input = Terminal::getInputCapabilities();
    if (!requireManualOperation(context, "manual terminal capability setup", "prepare stdout", output.status) ||
        !requireManualOperation(context, "manual terminal capability setup", "query stdin", input.status))
    {
        return;
    }
    if (output.capabilities.kind != Terminal::Types::StreamKind::Terminal || input.capabilities.kind != Terminal::Types::StreamKind::Terminal)
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
