/// @file manual_input_test.inl
/// @brief Manual Terminal input tests.

/// ------------------------------------------------------------
/// Interactive input
/// ------------------------------------------------------------

/// @brief Reads one exact line from the real terminal input stream.
void testManualInput(TestSupport::Context &context)
{
    constexpr std::string_view expected = "GameWIP-11";

    if(!requireManualOperation(
           context,
           "manual terminal input",
           "input prompt writeText",
           Terminal::writeText("Input: type GameWIP-11 and press Enter: ")))
    {
        return;
    }

    const Terminal::Types::Input::LineResult result = Terminal::readLine();

    if(!requireManualOperation(context, "manual terminal input", "readLine", result.status()))
    {
        return;
    }

    if(result.value().outcome != Terminal::Types::Input::ReadOutcome::Completed)
    {
        context.fail("manual terminal input", "readLine did not complete normally");
        return;
    }

    if(result.value().line != expected)
    {
        context.fail("manual terminal input", std::format("expected '{}', received '{}'", expected, result.value().line));
        return;
    }

    context.pass("manual terminal input");
}

/// @brief Exercises wrapped managed-line editing, viewport scroll, and live resize on a real terminal.
void testManualWrappedLineEditing(TestSupport::Context &context)
{
    const Terminal::Types::SizeResult size = Terminal::getTerminalSize();
    const Terminal::Types::Cursor::PositionResult position = Terminal::getCursorPosition();

    if(!requireManualOperation(context, "manual wrapped line editing", "query terminal size", size.status()) ||
       !requireManualOperation(context, "manual wrapped line editing", "query cursor position", position.status()))
    {
        return;
    }

    // Place the prompt close enough to the viewport bottom that wrapping exercises visible scrolling.
    if(size.value().rows > 2 && position.value().row + 2 < size.value().rows)
    {
        const std::uint32_t blankLines = size.value().rows - position.value().row - 2;

        for(std::uint32_t line = 0; line < blankLines; ++line)
        {
            if(!requireManualOperation(context, "manual wrapped line editing", "position prompt near viewport bottom", Terminal::writeLine()))
            {
                return;
            }
        }
    }

    const std::uint32_t minimumCharacters = size.value().columns + 10;
    context.manual(
        std::format(
            "Wrapped input check: type at least {} ASCII characters. Before Enter, use Left, Home, End, Backspace, and Delete to edit both "
            "rows; resize the terminal narrower and wider while the line is active. Watch for stale or misplaced text.",
            minimumCharacters));

    if(!requireManualOperation(context, "manual wrapped line editing", "wrapped-input prompt", Terminal::writeText("Wrapped/resize input: ")))
    {
        return;
    }

    const Terminal::Types::Input::LineResult result = Terminal::readLine();

    if(!requireManualOperation(context, "manual wrapped line editing", "wrapped readLine", result.status()))
    {
        return;
    }

    if(result.value().outcome != Terminal::Types::Input::ReadOutcome::Completed || result.value().line.size() < minimumCharacters)
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
    if(!inputCapabilities.supportsEventInput)
    {
        context.skip("manual structured input", "the terminal does not report structured event support");
        return;
    }

    // Open a session after the user is ready to provide an event.
    if(!requireManualOperation(
           context,
           "manual structured input",
           "event prompt writeText",
           Terminal::writeText("Event input: press the Left Arrow key: ")))
    {
        return;
    }

    Terminal::Session session;
    const auto openStatus = session.open();

    if(!requireManualOperation(context, "manual structured input", "open event session", openStatus))
    {
        return;
    }

    Terminal::Types::Input::EventOptions options;
    options.timeout = std::chrono::seconds{10};

    // Ignore releases and unrelated events until Left Arrow is observed or the deadline expires.
    bool matched = false;

    for(int attempt = 0; attempt < 8 && !matched; ++attempt)
    {
        const Terminal::Types::Input::EventResult event = session.readEvent(options);

        if(!event.ok())
        {
            static_cast<void>(requireManualOperation(context, "manual structured input", "readEvent", event.status()));
            break;
        }

        if(event.value().outcome != Terminal::Types::Input::ReadOutcome::Completed || !event.value().event.has_value())
        {
            context.fail("manual structured input", "readEvent did not produce a key event before the deadline");
            break;
        }

        const Terminal::Types::Events::Key *key = event.value().event->getIf<Terminal::Types::Events::Key>();

        if(key == nullptr || key->action == Terminal::Types::Events::KeyAction::Release)
        {
            continue;
        }

        const auto *named = std::get_if<Terminal::Types::Events::NamedKey>(&key->key);
        matched = named != nullptr && *named == Terminal::Types::Events::NamedKey::ArrowLeft;
    }

    const bool closed = requireManualOperation(context, "manual structured input", "close event session", session.close());

    if(closed)
    {
        static_cast<void>(Terminal::writeLine());
    }

    if(matched && closed)
    {
        context.pass("manual structured input");
    }
    else if(closed)
    {
        context.fail("manual structured input", "the observed key was not the portable Left Arrow event");
    }
}
