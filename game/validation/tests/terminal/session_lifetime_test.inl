/// @file session_lifetime_test.inl
/// @brief Focused Terminal Session test phases.

#if TERMINAL_INTERNAL_TEST_HOOKS

/// @brief Checks move ownership preserves pending Session cleanup.
void testSessionMoveOwnership(TestSupport::Context &context, Terminal::Session &competing, const Terminal::Types::SessionOptions &streamOptions)
{
    Terminal::Session movable;
    static_cast<void>(context.expectTrue("movable session opens", movable.open(streamOptions).ok()));
    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    static_cast<void>(context.expectTrue("movable session hides cursor", movable.setCursorVisible(false).ok()));
    static_cast<void>(context.expectTrue("movable session enters alternate screen", movable.enterAlternateScreen().ok()));
    Terminal::Session moved(std::move(movable));
    // NOLINTNEXTLINE(bugprone-use-after-move) -- Session explicitly specifies a closed, queryable moved-from state.
    static_cast<void>(context.expectFalse("moved-from session becomes closed", movable.isOpen()));
    static_cast<void>(context.expectTrue("move construction preserves open ownership", moved.isOpen()));
    static_cast<void>(
        context.expectEq("moved session still blocks competitors", ErrorCode::ResourceBusy, competing.open(streamOptions).status().code()));
    static_cast<void>(context.expectTrue("moved session closes", moved.close().ok()));
    static_cast<void>(context.expectEq(
        "move construction preserves pending output cleanup",
        std::string{"\x1b[?25l\x1b[?1049h\x1b[?1049l\x1b[?25h"},
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));
}

/// @brief Checks destructor cleanup restores input and output state.
void testSessionDestructorCleanup(TestSupport::Context &context, const Terminal::Types::SessionOptions &streamOptions)
{
    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    {
        Terminal::Session scoped;
        static_cast<void>(context.expectTrue("destructor fixture opens", scoped.open(streamOptions).ok()));
        static_cast<void>(context.expectTrue("destructor fixture hides cursor", scoped.setCursorVisible(false).ok()));
        static_cast<void>(context.expectTrue("destructor fixture enters alternate screen", scoped.enterAlternateScreen().ok()));
        static_cast<void>(context.expectTrue(
            "destructor fixture owns immediate stream mode",
            Hooks::inputModeOverrideMatches(Terminal::Types::Input::Stream::Stdin, false, false, true)));
    }
    static_cast<void>(context.expectTrue(
        "session destructor restores mode",
        Hooks::inputModeOverrideMatches(Terminal::Types::Input::Stream::Stdin, true, true, true)));
    static_cast<void>(context.expectEq(
        "session destructor restores pending output state",
        std::string{"\x1b[?25l\x1b[?1049h\x1b[?1049l\x1b[?25h"},
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    {
        Terminal::Session failedDestructor;
        static_cast<void>(context.expectTrue("failed destructor fixture opens", failedDestructor.open(streamOptions).ok()));
        static_cast<void>(context.expectTrue("failed destructor fixture hides cursor", failedDestructor.setCursorVisible(false).ok()));
        static_cast<void>(context.expectTrue("failed destructor fixture enters alternate screen", failedDestructor.enterAlternateScreen().ok()));
        Hooks::forceNextTextWriteFailure(IOErrorCode::WriteFailed);
    }
    static_cast<void>(context.expectTrue(
        "failed Session destructor still restores input mode",
        Hooks::inputModeOverrideMatches(Terminal::Types::Input::Stream::Stdin, true, true, true)));
    static_cast<void>(context.expectEq(
        "failed Session destructor does not retry out of reverse order",
        std::string{"\x1b[?25l\x1b[?1049h\x1b[?25h"},
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));
    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    {
        Terminal::AlternateScreenScope laterAlternateScope = Terminal::scopedAlternateScreen();
        static_cast<void>(
            context.expectTrue("alternate scope nesting remains usable after Session destructor failure", laterAlternateScope.leave().ok()));
    }
    static_cast<void>(context.expectEq(
        "Session destructor failure releases stale alternate nesting ownership",
        std::string{"\x1b[?1049h\x1b[?1049l"},
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));
}

#endif
