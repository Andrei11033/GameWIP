/// @file session_output_test.inl
/// @brief Focused Terminal Session test phases.

#if TERMINAL_INTERNAL_TEST_HOOKS

/// @brief Checks output-state restoration and retry behavior.
void testSessionOutputStateRestoration(
    TestSupport::Context &context,
    Terminal::Session &competing,
    const Terminal::Types::SessionOptions &streamOptions)
{
    // Repeated controls and explicit inverses update cleanup ownership once.

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    Terminal::Session duplicateOutputState;
    static_cast<void>(context.expectTrue("duplicate output-state fixture opens", duplicateOutputState.open(streamOptions).ok()));
    static_cast<void>(context.expectTrue("duplicate output-state fixture hides cursor", duplicateOutputState.setCursorVisible(false).ok()));
    static_cast<void>(context.expectTrue("duplicate cursor hide is idempotent", duplicateOutputState.setCursorVisible(false).ok()));
    static_cast<void>(context.expectTrue("duplicate output-state fixture enters alternate screen", duplicateOutputState.enterAlternateScreen().ok()));
    static_cast<void>(context.expectTrue("duplicate alternate enter is idempotent", duplicateOutputState.enterAlternateScreen().ok()));
    static_cast<void>(context.expectTrue("explicit cursor show removes obligation", duplicateOutputState.setCursorVisible(true).ok()));
    static_cast<void>(context.expectTrue("explicit alternate leave removes obligation", duplicateOutputState.leaveAlternateScreen().ok()));
    static_cast<void>(context.expectTrue("closing after explicit inverses succeeds", duplicateOutputState.close().ok()));
    static_cast<void>(context.expectEq(
        "duplicate state changes emit once and explicit inverses are not repeated by close",
        std::string{"\x1b[?25l\x1b[?1049h\x1b[?25h\x1b[?1049l"},
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));

    // Setup flush failure retains the emitted cleanup obligation.

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    Terminal::Session setupFlushFailure;
    static_cast<void>(context.expectTrue("setup flush failure fixture opens", setupFlushFailure.open(streamOptions).ok()));
    Hooks::forceNextFlushFailure(IOErrorCode::FlushFailed);
    static_cast<void>(context.expectEq(
        "Session cursor setup flush failure propagates",
        ErrorCode::FlushFailed,
        setupFlushFailure.setCursorVisible(false, {.flushMode = IO::Types::FlushMode::Data}).status().code()));
    static_cast<void>(context.expectTrue("Session retains cleanup after setup flush failure", setupFlushFailure.close().ok()));
    static_cast<void>(context.expectEq(
        "Session setup flush failure does not lose cleanup ownership",
        std::string{"\x1b[?25l\x1b[?25h"},
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));

    // A failed top restoration keeps older cleanup pending.

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    Terminal::Session outputRestoreFailure;
    static_cast<void>(context.expectTrue("output restoration failure fixture opens", outputRestoreFailure.open(streamOptions).ok()));
    static_cast<void>(context.expectTrue("output restoration fixture hides cursor", outputRestoreFailure.setCursorVisible(false).ok()));
    static_cast<void>(context.expectTrue("output restoration fixture enters alternate screen", outputRestoreFailure.enterAlternateScreen().ok()));
    Hooks::forceNextTextWriteFailure(IOErrorCode::WriteFailed);
    static_cast<void>(
        context.expectEq("output restoration failure propagates from close", ErrorCode::WriteFailed, outputRestoreFailure.close().status().code()));
    static_cast<void>(context.expectTrue("output restoration failure leaves session open", outputRestoreFailure.isOpen()));
    static_cast<void>(context.expectEq(
        "failed top restoration does not run older obligations",
        std::string{"\x1b[?25l\x1b[?1049h"},
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));
    static_cast<void>(context.expectEq(
        "output restoration failure retains input ownership",
        ErrorCode::ResourceBusy,
        competing.open(streamOptions).status().code()));
    static_cast<void>(context.expectTrue("output restoration close retry succeeds", outputRestoreFailure.close().ok()));
    static_cast<void>(context.expectEq(
        "output restoration retry preserves reverse order",
        std::string{"\x1b[?25l\x1b[?1049h\x1b[?1049l\x1b[?25h"},
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));

    // An emitted transition is not repeated after its flush fails.

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    Terminal::Session flushRestoreFailure;
    static_cast<void>(context.expectTrue("restoration flush failure fixture opens", flushRestoreFailure.open(streamOptions).ok()));
    const Terminal::Types::Output::ControlOptions flushedControl{.flushMode = IO::Types::FlushMode::Data};
    static_cast<void>(context.expectTrue("restoration flush fixture hides cursor", flushRestoreFailure.setCursorVisible(false, flushedControl).ok()));
    Hooks::forceNextFlushFailure(IOErrorCode::FlushFailed);
    static_cast<void>(
        context.expectEq("close restoration flush failure propagates", ErrorCode::FlushFailed, flushRestoreFailure.close().status().code()));
    static_cast<void>(context.expectTrue("restoration flush failure leaves session open", flushRestoreFailure.isOpen()));
    static_cast<void>(context.expectEq(
        "restoration transition was emitted before flush failure",
        std::string{"\x1b[?25l\x1b[?25h"},
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));
    static_cast<void>(context.expectTrue("restoration flush retry closes session", flushRestoreFailure.close().ok()));
    static_cast<void>(context.expectEq(
        "restoration flush retry does not repeat completed transition",
        std::string{"\x1b[?25l\x1b[?25h"},
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));
}

/// @brief Checks that a Session binds output to the requested stream.
void testSessionOutputBinding(TestSupport::Context &context, const Terminal::Types::SessionOptions &streamOptions)
{
    setupCapturedOutput(Terminal::Types::Output::Stream::Stderr);
    Hooks::setTerminalSizeOverride(Terminal::Types::Output::Stream::Stderr, {.columns = 72, .rows = 20});
    Hooks::setCursorPositionOverride(Terminal::Types::Output::Stream::Stderr, {.column = 11, .row = 6});
    Terminal::Types::SessionOptions stderrOptions = streamOptions;
    stderrOptions.output = Terminal::Types::Output::Stream::Stderr;
    Terminal::Session stderrSession;
    static_cast<void>(context.expectTrue("stderr-bound session opens", stderrSession.open(stderrOptions).ok()));
    static_cast<void>(context.expectTrue("stderr-bound session write succeeds", stderrSession.writeText("bound-stderr").ok()));
    static_cast<void>(context.expectEq(
        "session output is bound to requested stream",
        std::string{"bound-stderr"},
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stderr)));
    const auto stderrSize = stderrSession.getTerminalSize();
    static_cast<void>(context.expectTrue("stderr-bound size query succeeds", stderrSize.ok()));
    if(stderrSize.hasValue())
    {
        static_cast<void>(context.expectEq("stderr-bound size query", std::uint32_t{72}, stderrSize.value().columns));
    }

    const auto stderrPosition = stderrSession.getCursorPosition({.timeout = Terminal::kNoWait, .flushMode = IO::Types::FlushMode::None});
    static_cast<void>(context.expectTrue("stderr-bound cursor query succeeds", stderrPosition.ok()));
    if(stderrPosition.hasValue())
    {
        static_cast<void>(context.expectEq("stderr-bound cursor query", std::uint32_t{11}, stderrPosition.value().column));
    }
    static_cast<void>(context.expectTrue("stderr-bound session closes", stderrSession.close().ok()));
}

#endif
