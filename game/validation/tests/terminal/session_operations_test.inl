/// @file session_open_test.inl
/// @brief Focused Terminal Session test phases.

#if TERMINAL_INTERNAL_TEST_HOOKS

/// @brief Checks that closed Session operations report NotOpen.
void testClosedSessionErrors(TestSupport::Context &context)
{
    Terminal::Session closed;
    static_cast<void>(context.expectFalse("default session starts closed", closed.isOpen()));
    static_cast<void>(context.expectEq("closed session read reports NotOpen", ErrorCode::NotOpen, closed.readText().status().code()));
    static_cast<void>(
        context.expectEq("closed session input query reports NotOpen", ErrorCode::NotOpen, closed.getInputCapabilities().status().code()));
    static_cast<void>(context.expectEq("closed session write reports NotOpen", ErrorCode::NotOpen, closed.writeText("closed").status().code()));
    static_cast<void>(
        context.expectEq("closed session output query reports NotOpen", ErrorCode::NotOpen, closed.getOutputCapabilities().status().code()));
    static_cast<void>(context.expectEq("closed session prepare reports NotOpen", ErrorCode::NotOpen, closed.prepareOutput().status().code()));
    static_cast<void>(context.expectEq("closed session size reports NotOpen", ErrorCode::NotOpen, closed.getTerminalSize().status().code()));
    static_cast<void>(context.expectEq("closed session line write reports NotOpen", ErrorCode::NotOpen, closed.writeLine().status().code()));
    static_cast<void>(context.expectEq("closed session byte write reports NotOpen", ErrorCode::NotOpen, closed.writeBytes({}).status().code()));
    static_cast<void>(context.expectEq("closed session segment write reports NotOpen", ErrorCode::NotOpen, closed.writeSegments({}).status().code()));
    static_cast<void>(context.expectEq("closed session print reports NotOpen", ErrorCode::NotOpen, closed.print("{}", 1).status().code()));
    static_cast<void>(context.expectEq("closed session println reports NotOpen", ErrorCode::NotOpen, closed.println("{}", 1).status().code()));
    static_cast<void>(context.expectEq("closed session flush reports NotOpen", ErrorCode::NotOpen, closed.flush().status().code()));
    static_cast<void>(context.expectEq("closed session style reports NotOpen", ErrorCode::NotOpen, closed.resetStyle().status().code()));
    static_cast<void>(context.expectEq(
        "closed session cursor move reports NotOpen",
        ErrorCode::NotOpen,
        closed.moveCursor(Terminal::Types::Cursor::MoveDirection::Up).status().code()));
    static_cast<void>(
        context.expectEq("closed session cursor set reports NotOpen", ErrorCode::NotOpen, closed.setCursorPosition({}).status().code()));
    static_cast<void>(
        context.expectEq("closed session cursor query reports NotOpen", ErrorCode::NotOpen, closed.getCursorPosition().status().code()));
    static_cast<void>(
        context.expectEq("closed session cursor save reports NotOpen", ErrorCode::NotOpen, closed.saveCursorPosition().status().code()));
    static_cast<void>(
        context.expectEq("closed session cursor restore reports NotOpen", ErrorCode::NotOpen, closed.restoreCursorPosition().status().code()));
    static_cast<void>(
        context.expectEq("closed session visibility reports NotOpen", ErrorCode::NotOpen, closed.setCursorVisible(false).status().code()));
    static_cast<void>(context.expectEq("closed session clear reports NotOpen", ErrorCode::NotOpen, closed.clear().status().code()));
    static_cast<void>(context.expectEq(
        "closed session scroll reports NotOpen",
        ErrorCode::NotOpen,
        closed.scroll(Terminal::Types::Output::ScrollDirection::Up).status().code()));
    static_cast<void>(
        context.expectEq("closed session alternate enter reports NotOpen", ErrorCode::NotOpen, closed.enterAlternateScreen().status().code()));
    static_cast<void>(
        context.expectEq("closed session alternate leave reports NotOpen", ErrorCode::NotOpen, closed.leaveAlternateScreen().status().code()));
    static_cast<void>(context.expectEq("closed session title reports NotOpen", ErrorCode::NotOpen, closed.setTitle("closed").status().code()));
    static_cast<void>(context.expectEq("closed session bell reports NotOpen", ErrorCode::NotOpen, closed.ringBell().status().code()));
    static_cast<void>(context.expectTrue("closing a closed session is idempotent", closed.close().ok()));
}

/// @brief Checks bound-session capabilities and output operations.
void testBoundStreamsAndOutput(TestSupport::Context &context, Terminal::Session &session, const Terminal::Types::SessionOptions &streamOptions)
{
    static_cast<void>(context.expectTrue("stream session opens", session.open(streamOptions).ok()));
    static_cast<void>(context.expectTrue("opened session reports open", session.isOpen()));
    static_cast<void>(context.expectTrue(
        "stream session uses immediate managed input mode",
        Hooks::inputModeOverrideMatches(Terminal::Types::Input::Stream::Stdin, false, false, true)));
    static_cast<void>(context.expectTrue(
        "stream session enables resize records without mouse/Quick Edit",
        Hooks::inputManagedEventModeOverrideMatches(Terminal::Types::Input::Stream::Stdin, true, false, true)));

    const Terminal::Types::Input::CapabilitiesResult sessionInputCapabilities = session.getInputCapabilities();
    static_cast<void>(context.expectTrue("session input capability query succeeds", sessionInputCapabilities.ok()));
    if(sessionInputCapabilities.hasValue())
    {
        static_cast<void>(context.expectTrue("session reuses captured event capability", sessionInputCapabilities.value().supportsEventInput));
    }

    const Terminal::Types::Output::CapabilitiesResult sessionOutputCapabilities = session.getOutputCapabilities();
    static_cast<void>(context.expectTrue("session output capability query succeeds", sessionOutputCapabilities.ok()));
    if(sessionOutputCapabilities.hasValue())
    {
        static_cast<void>(context.expectTrue("session output capability is bound stdout", sessionOutputCapabilities.value().supportsUtf8Text));
    }
    static_cast<void>(context.expectTrue("session output preparation succeeds", session.prepareOutput().ok()));

    const Terminal::Types::SizeResult sessionSize = session.getTerminalSize();
    static_cast<void>(context.expectTrue("session size query succeeds", sessionSize.ok()));
    if(sessionSize.hasValue())
    {
        static_cast<void>(context.expectEq("session size query uses bound output", std::uint32_t{100}, sessionSize.value().columns));
    }

    const Terminal::Types::Cursor::PositionResult sessionPosition =
        session.getCursorPosition({.timeout = Terminal::kNoWait, .flushMode = IO::Types::FlushMode::None});
    static_cast<void>(context.expectTrue("session cursor position query succeeds", sessionPosition.ok()));
    if(sessionPosition.hasValue())
    {
        static_cast<void>(context.expectEq("session cursor query uses owned input", std::uint32_t{3}, sessionPosition.value().column));
    }

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    Terminal::Types::Output::LineOptions sessionLineOptions;
    sessionLineOptions.lineEnding = Terminal::Types::Output::LineEnding::Lf;
    static_cast<void>(context.expectTrue("session text write succeeds", session.writeText("session").ok()));
    static_cast<void>(context.expectTrue("session line write succeeds", session.writeLine("-line", sessionLineOptions).ok()));
    static_cast<void>(context.expectTrue("session print succeeds", session.print("-{}", 7).ok()));
    static_cast<void>(context.expectTrue("session println succeeds", session.println(sessionLineOptions, "-{}", 8).ok()));

    const std::string sessionBytesText = "!";
    const auto sessionBytes = session.writeBytes(bytesOf(sessionBytesText));
    static_cast<void>(context.expectTrue("session byte write succeeds", sessionBytes.ok()));
    if(sessionBytes.hasValue())
    {
        static_cast<void>(context.expectEq("session byte write count", std::size_t{1}, sessionBytes.value().bytesWritten));
    }

    const std::array<Terminal::Types::Output::Segment, 2> sessionSegments{Terminal::textSegment("seg"), Terminal::textSegment("ment")};
    static_cast<void>(context.expectTrue(
        "session segmented write succeeds",
        session.writeSegments(std::span<const Terminal::Types::Output::Segment>(sessionSegments)).ok()));
    static_cast<void>(context.expectTrue("direct global output remains available while session owns stdin", Terminal::writeText("-global").ok()));
    static_cast<void>(context.expectEq(
        "session output shares global serialization implementation",
        std::string{"session-line\n-7-8\n!segment-global"},
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    static_cast<void>(context.expectTrue("session reset style succeeds", session.resetStyle().ok()));
    static_cast<void>(context.expectTrue("session cursor move succeeds", session.moveCursor(Terminal::Types::Cursor::MoveDirection::Up, 2).ok()));
    static_cast<void>(context.expectTrue("session cursor set succeeds", session.setCursorPosition({.column = 4, .row = 2}).ok()));
    static_cast<void>(context.expectTrue("session cursor save succeeds", session.saveCursorPosition().ok()));
    static_cast<void>(context.expectTrue("session cursor restore succeeds", session.restoreCursorPosition().ok()));
    static_cast<void>(context.expectTrue("session clear succeeds", session.clear(Terminal::Types::Output::ClearTarget::LineAfterCursor).ok()));
    static_cast<void>(context.expectTrue("session scroll succeeds", session.scroll(Terminal::Types::Output::ScrollDirection::Up, 1).ok()));
    static_cast<void>(context.expectTrue("session title succeeds", session.setTitle("Session").ok()));
    static_cast<void>(context.expectTrue("session bell succeeds", session.ringBell().ok()));
    static_cast<void>(context.expectTrue("session flush succeeds", session.flush(IO::Types::FlushMode::Data).ok()));

    static_cast<void>(
        context.expectEq("same session re-open reports AlreadyOpen", ErrorCode::AlreadyOpen, session.open(streamOptions).status().code()));
}

#endif
