/// @file line_input_test.inl
/// @brief Focused Terminal line-input tests.

#if TERMINAL_INTERNAL_TEST_HOOKS
/// @brief Verifies the managed Unicode line editor over deterministic structured events.
void testManagedLineEditing(TestSupport::Context &context)
{
    // Build the key events used by each editor scenario.
    const auto characterEvent =
        [](char32_t scalar, Terminal::Types::Events::KeyAction action = Terminal::Types::Events::KeyAction::Press, std::uint32_t repeat = 1)
    {
        Terminal::Types::Events::Key key;
        key.key = Terminal::Types::Events::CharacterKey{.value = scalar};
        key.action = action;
        key.location = Terminal::Types::Events::KeyLocation::Standard;
        key.repeatCount = repeat;
        return Terminal::Types::Event{.data = key};
    };

    const auto namedEvent = [](Terminal::Types::Events::NamedKey named,
                               Terminal::Types::Events::KeyAction action = Terminal::Types::Events::KeyAction::Press,
                               std::uint32_t repeat = 1)
    {
        Terminal::Types::Events::Key key;
        key.key = named;
        key.action = action;
        key.location = Terminal::Types::Events::KeyLocation::Standard;
        key.repeatCount = repeat;
        return Terminal::Types::Event{.data = key};
    };

    // Run an edit without echo through a managed input session.
    const auto runLine = [&context](std::span<const Terminal::Types::Event> events, const Terminal::Types::Input::LineOptions &options)
    {
        Hooks::reset();
        Hooks::setInputCapabilitiesOverride(Terminal::Types::Input::Stream::Stdin, terminalInputCapabilities());
        Hooks::setInputModeOverride(Terminal::Types::Input::Stream::Stdin, true, true, true);
        Hooks::setInputEvents(Terminal::Types::Input::Stream::Stdin, events);

        Terminal::Types::SessionOptions sessionOptions;
        sessionOptions.deliveryMode = Terminal::Types::Input::DeliveryMode::Stream;

        Terminal::Session session;
        const auto openStatus = session.open(sessionOptions);
        static_cast<void>(context.expectTrue("managed line session opens", openStatus.ok()));

        if(!openStatus.ok())
        {
            return Terminal::Types::Input::LineResult::failure(openStatus.status());
        }

        Terminal::Types::Input::LineResult result = session.readLine(options);
        const auto closeStatus = session.close();
        static_cast<void>(context.expectTrue("managed line session closes", closeStatus.ok()));
        return result;
    };

    struct EchoRunResult
    {
        Terminal::Types::Input::LineResult line = Terminal::Types::Input::LineResult::success({});

        // Observed cursor rendering
        std::vector<Terminal::Types::Cursor::Position> cursorSets;
        Terminal::Types::Cursor::Position viewportOrigin{};
    };

    // Capture cursor redraws for echoed input.
    const auto runEchoLine =
        [&context](std::span<const Terminal::Types::Event> events, Terminal::Types::Size size, Terminal::Types::Cursor::Position position)
    {
        Hooks::reset();
        Hooks::setInputCapabilitiesOverride(Terminal::Types::Input::Stream::Stdin, terminalInputCapabilities());
        Hooks::setInputModeOverride(Terminal::Types::Input::Stream::Stdin, true, true, true);
        Hooks::setInputEvents(Terminal::Types::Input::Stream::Stdin, events);
        setupCapturedOutput(Terminal::Types::Output::Stream::Stdout);
        Hooks::enableCursorRenderingSimulation(Terminal::Types::Output::Stream::Stdout, size, position);

        Terminal::Types::SessionOptions sessionOptions;
        sessionOptions.deliveryMode = Terminal::Types::Input::DeliveryMode::Stream;

        Terminal::Session session;
        const auto openStatus = session.open(sessionOptions);
        static_cast<void>(context.expectTrue("managed echo session opens", openStatus.ok()));

        if(!openStatus.ok())
        {
            return EchoRunResult{.line = Terminal::Types::Input::LineResult::failure(openStatus.status())};
        }

        Terminal::Types::Input::LineOptions echoOptions;
        echoOptions.echo = true;
        EchoRunResult result;
        result.line = session.readLine(echoOptions);
        result.cursorSets = Hooks::cursorRenderingSetHistory(Terminal::Types::Output::Stream::Stdout);
        result.viewportOrigin = Hooks::cursorRenderingViewportOrigin(Terminal::Types::Output::Stream::Stdout);
        static_cast<void>(context.expectTrue("managed echo session closes", session.close().ok()));
        return result;
    };

    Terminal::Types::Input::LineOptions options;
    options.echo = false;

    // Check insertion, cursor movement, and deletion.
    {
        const std::array<Terminal::Types::Event, 9> events{
            characterEvent(U'a'),
            characterEvent(U'b'),
            characterEvent(U'c'),
            namedEvent(Terminal::Types::Events::NamedKey::ArrowLeft),
            characterEvent(U'X'),
            namedEvent(Terminal::Types::Events::NamedKey::Home),
            namedEvent(Terminal::Types::Events::NamedKey::Delete),
            namedEvent(Terminal::Types::Events::NamedKey::End),
            namedEvent(Terminal::Types::Events::NamedKey::Enter),
        };

        const Terminal::Types::Input::LineResult result = runLine(events, options);
        static_cast<void>(context.expectTrue("managed edit status", result.ok()));
        if(!result.hasValue())
        {
            return;
        }
        static_cast<void>(context.expectEq("managed edit outcome", Terminal::Types::Input::ReadOutcome::Completed, result.value().outcome));
        static_cast<void>(context.expectEq("managed edit text", std::string{"bXc"}, result.value().line));
    }

    // Backspace and Delete operate on complete grapheme clusters.
    {
        const std::array<Terminal::Types::Event, 5> events{
            characterEvent(U'a'),
            characterEvent(static_cast<char32_t>(0x0308)),
            namedEvent(Terminal::Types::Events::NamedKey::Backspace),
            characterEvent(U'\u03bb'),
            namedEvent(Terminal::Types::Events::NamedKey::Enter),
        };

        const Terminal::Types::Input::LineResult result = runLine(events, options);
        static_cast<void>(context.expectTrue("grapheme backspace status", result.ok()));
        if(!result.hasValue())
        {
            return;
        }
        static_cast<void>(context.expectEq("grapheme backspace removes whole cluster", std::string{"\xce\xbb"}, result.value().line));
    }

    {
        const std::array<Terminal::Types::Event, 6> events{
            characterEvent(static_cast<char32_t>(0x1f469)),
            characterEvent(static_cast<char32_t>(0x1f4bb)),
            namedEvent(Terminal::Types::Events::NamedKey::ArrowLeft),
            characterEvent(static_cast<char32_t>(0x200d)),
            namedEvent(Terminal::Types::Events::NamedKey::Backspace),
            namedEvent(Terminal::Types::Events::NamedKey::Enter),
        };

        const Terminal::Types::Input::LineResult result = runLine(events, options);
        static_cast<void>(context.expectTrue("middle grapheme merge status", result.ok()));
        if(!result.hasValue())
        {
            return;
        }
        static_cast<void>(context.expectEq("middle grapheme merge keeps caret on a cluster boundary", std::string{}, result.value().line));
    }

    // Respect repeat counts and preserve Unicode paste boundaries.
    {
        const std::array<Terminal::Types::Event, 3> events{
            characterEvent(U'z', Terminal::Types::Events::KeyAction::Repeat, 3),
            namedEvent(Terminal::Types::Events::NamedKey::Backspace, Terminal::Types::Events::KeyAction::Repeat, 2),
            namedEvent(Terminal::Types::Events::NamedKey::Enter),
        };

        const Terminal::Types::Input::LineResult result = runLine(events, options);
        static_cast<void>(context.expectTrue("repeat edit status", result.ok()));
        if(!result.hasValue())
        {
            return;
        }
        static_cast<void>(context.expectEq("repeat edit count", std::string{"z"}, result.value().line));
    }

    {
        const std::array<Terminal::Types::Event, 2> events{
            Terminal::Types::Event{.data = Terminal::Types::Events::Paste{.text = std::string{"\xc3\xa9\xf0\x9f\x99\x82"}}},
            namedEvent(Terminal::Types::Events::NamedKey::Enter),
        };

        Terminal::Types::Input::LineOptions bounded = options;
        bounded.maxReturnedBytes = 3;
        const Terminal::Types::Input::LineResult result = runLine(events, bounded);
        static_cast<void>(context.expectTrue("bounded paste status", result.ok()));
        if(!result.hasValue())
        {
            return;
        }
        static_cast<void>(context.expectEq("bounded paste preserves scalar boundary", std::string{"\xc3\xa9"}, result.value().line));
        static_cast<void>(context.expectTrue("bounded paste reports truncation", result.value().wasTruncated));
    }

    // Redraw echoed input as lines wrap and the viewport changes.
    {
        const std::array<Terminal::Types::Event, 11> events{
            characterEvent(U'a'),
            characterEvent(U'b'),
            characterEvent(U'c'),
            characterEvent(U'd'),
            namedEvent(Terminal::Types::Events::NamedKey::ArrowLeft),
            characterEvent(U'X'),
            namedEvent(Terminal::Types::Events::NamedKey::Home),
            namedEvent(Terminal::Types::Events::NamedKey::Delete),
            namedEvent(Terminal::Types::Events::NamedKey::End),
            characterEvent(U'Y'),
            namedEvent(Terminal::Types::Events::NamedKey::Enter),
        };

        const EchoRunResult result = runEchoLine(events, {.columns = 5, .rows = 3}, {.column = 3, .row = 2});
        static_cast<void>(context.expectTrue("wrapped edit status", result.line.ok()));
        if(!result.line.hasValue())
        {
            return;
        }
        static_cast<void>(context.expectEq("wrapped left/home edit text", std::string{"bcXdY"}, result.line.value().line));
        static_cast<void>(context.expectTrue("wrapped edit performs cursor redraw", !result.cursorSets.empty()));

        if(!result.cursorSets.empty())
        {
            static_cast<void>(context.expectEq("wrapped redraw rebuilds stable origin column", std::uint32_t{3}, result.cursorSets.front().column));
            static_cast<void>(context.expectEq("wrapped redraw rebuilds stable origin row", std::uint32_t{2}, result.cursorSets.front().row));
        }
        static_cast<void>(context.expectTrue("wrapped typing simulates viewport scroll", result.viewportOrigin.row > 0));
    }

    {
        const std::array<Terminal::Types::Event, 8> events{
            characterEvent(U'a'),
            characterEvent(U'b'),
            characterEvent(U'c'),
            characterEvent(U'd'),
            namedEvent(Terminal::Types::Events::NamedKey::Backspace),
            namedEvent(Terminal::Types::Events::NamedKey::ArrowLeft),
            namedEvent(Terminal::Types::Events::NamedKey::Delete),
            namedEvent(Terminal::Types::Events::NamedKey::Enter),
        };

        const EchoRunResult result = runEchoLine(events, {.columns = 4, .rows = 3}, {.column = 3, .row = 1});
        static_cast<void>(context.expectTrue("wrapped backspace/delete status", result.line.ok()));
        if(!result.line.hasValue())
        {
            return;
        }
        static_cast<void>(context.expectEq("wrapped backspace/delete text", std::string{"ab"}, result.line.value().line));
        static_cast<void>(context.expectTrue("wrapped backspace/delete redraws from tracked anchor", !result.cursorSets.empty()));
    }

    {
        const std::array<Terminal::Types::Event, 9> events{
            characterEvent(U'a'),
            characterEvent(U'b'),
            characterEvent(U'c'),
            characterEvent(U'd'),
            Terminal::Types::Event{.data = Terminal::Types::Events::Resize{.size = {.columns = 4, .rows = 3}}},
            namedEvent(Terminal::Types::Events::NamedKey::Home),
            characterEvent(U'Z'),
            namedEvent(Terminal::Types::Events::NamedKey::End),
            namedEvent(Terminal::Types::Events::NamedKey::Enter),
        };

        const EchoRunResult result = runEchoLine(events, {.columns = 5, .rows = 3}, {.column = 3, .row = 1});
        static_cast<void>(context.expectTrue("resize redraw status", result.line.ok()));
        if(!result.line.hasValue())
        {
            return;
        }
        static_cast<void>(context.expectEq("resize redraw preserves editable text", std::string{"Zabcd"}, result.line.value().line));
        static_cast<void>(context.expectTrue("resize redraw performs cursor positioning", !result.cursorSets.empty()));

        if(!result.cursorSets.empty())
        {
            static_cast<void>(context.expectEq("resize redraw rebuilds reflowed origin column", std::uint32_t{0}, result.cursorSets.front().column));
            static_cast<void>(context.expectEq("resize redraw rebuilds reflowed origin row", std::uint32_t{2}, result.cursorSets.front().row));
        }
    }

    // Preserve edited text when stream input would block.
    {
        Hooks::reset();
        Hooks::setInputCapabilitiesOverride(Terminal::Types::Input::Stream::Stdin, terminalInputCapabilities());
        Hooks::setInputModeOverride(Terminal::Types::Input::Stream::Stdin, true, true, true);
        const std::array<Terminal::Types::Event, 1> events{characterEvent(U'p')};
        Hooks::setInputEvents(Terminal::Types::Input::Stream::Stdin, events, false);

        Terminal::Types::SessionOptions sessionOptions;
        sessionOptions.deliveryMode = Terminal::Types::Input::DeliveryMode::Stream;
        Terminal::Session session;
        static_cast<void>(context.expectTrue("partial line session opens", session.open(sessionOptions).ok()));

        Terminal::Types::Input::LineOptions partialOptions;
        partialOptions.echo = false;
        partialOptions.timeout = Terminal::kNoWait;
        const Terminal::Types::Input::LineResult partial = session.readLine(partialOptions);
        static_cast<void>(context.expectTrue("partial line status", partial.ok()));
        if(!partial.hasValue())
        {
            return;
        }
        static_cast<void>(context.expectEq("partial line would-block", Terminal::Types::Input::ReadOutcome::WouldBlock, partial.value().outcome));
        static_cast<void>(context.expectEq("partial line preserves text", std::string{"p"}, partial.value().line));
        static_cast<void>(context.expectTrue("partial line session closes", session.close().ok()));
    }

    Hooks::reset();
}
#endif
