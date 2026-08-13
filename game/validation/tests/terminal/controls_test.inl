/// @file controls_test.inl
/// @brief Focused terminal controls correctness suites.

#if TERMINAL_INTERNAL_TEST_HOOKS
/// @brief Verifies cursor, clear, scroll, title, alternate-screen, and visibility controls.
void testControls(TestSupport::Context &context)
{
    Hooks::reset();
    setupCapturedOutput(Terminal::Types::Output::Stream::Stdout);

    static_cast<void>(context.expectTrue("move cursor succeeds", Terminal::moveCursor(Terminal::Types::Cursor::MoveDirection::Up, 2).ok()));
    static_cast<void>(context.expectTrue("set cursor succeeds", Terminal::setCursorPosition({.column = 4, .row = 2}).ok()));
    static_cast<void>(context.expectTrue("save cursor succeeds", Terminal::saveCursorPosition().ok()));
    static_cast<void>(context.expectTrue("restore cursor succeeds", Terminal::restoreCursorPosition().ok()));
    static_cast<void>(context.expectTrue("hide cursor succeeds", Terminal::setCursorVisible(false).ok()));
    static_cast<void>(context.expectTrue("show cursor succeeds", Terminal::setCursorVisible(true).ok()));
    static_cast<void>(context.expectTrue("clear succeeds", Terminal::clear(Terminal::Types::Output::ClearTarget::EntireScreenAndScrollback).ok()));
    static_cast<void>(context.expectTrue("scroll succeeds", Terminal::scroll(Terminal::Types::Output::ScrollDirection::Down, 3).ok()));
    static_cast<void>(context.expectTrue("enter alt succeeds", Terminal::enterAlternateScreen().ok()));
    static_cast<void>(context.expectTrue("leave alt succeeds", Terminal::leaveAlternateScreen().ok()));
    static_cast<void>(context.expectTrue(
        "title succeeds",
        Terminal::setTitle(
            "A\x1b"
            "B\a"
            "C")
            .ok()));
    static_cast<void>(context.expectTrue("bell succeeds", Terminal::ringBell().ok()));

    static_cast<void>(context.expectEq(
        "control sequence capture",
        std::string{"\x1b[2A\x1b[3;5H\x1b[s\x1b[u\x1b[?25l\x1b[?25h\x1b[3J\x1b[3T\x1b[?1049h\x1b[?1049l\x1b]0;A B C\a\a"},
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    {
        Terminal::CursorHiddenScope outer = Terminal::scopedCursorHidden();
        static_cast<void>(context.expectTrue("outer cursor scope active", outer.active()));
        {
            Terminal::CursorHiddenScope inner = Terminal::scopedCursorHidden();
            static_cast<void>(context.expectTrue("inner cursor scope active", inner.active()));
            static_cast<void>(context.expectTrue("inner cursor restore succeeds", inner.restore().ok()));
            static_cast<void>(context.expectEq(
                "inner cursor restore keeps cursor hidden",
                std::string{"\x1b[?25l"},
                Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));
        }
        static_cast<void>(context.expectTrue("outer cursor restore succeeds", outer.restore().ok()));
    }
    static_cast<void>(context.expectEq(
        "nested cursor scopes emit one hide and show",
        std::string{"\x1b[?25l\x1b[?25h"},
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    {
        Terminal::CursorHiddenScope scope = Terminal::scopedCursorHidden();
        Hooks::forceNextTextWriteFailure(ErrorCode::WriteFailed);
        static_cast<void>(context.expectEq("cursor restore failure propagates", ErrorCode::WriteFailed, scope.restore().code));
        static_cast<void>(context.expectTrue("failed cursor restore remains active", scope.active()));
        static_cast<void>(context.expectTrue("cursor restore retry succeeds", scope.restore().ok()));
        static_cast<void>(context.expectFalse("successful cursor restore becomes inactive", scope.active()));
    }

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    {
        Hooks::forceNextFlushFailure(ErrorCode::FlushFailed);
        Terminal::CursorHiddenScope scope = Terminal::scopedCursorHidden({.flushMode = IO::Types::FlushMode::Data});
        static_cast<void>(context.expectEq("cursor setup flush failure propagates", ErrorCode::FlushFailed, scope.status().code));
        static_cast<void>(context.expectTrue("cursor setup flush failure retains restoration ownership", scope.active()));
        static_cast<void>(context.expectTrue("cursor setup flush failure can still restore", scope.restore().ok()));
    }
    static_cast<void>(context.expectEq(
        "cursor setup flush failure does not leak hidden state",
        std::string{"\x1b[?25l\x1b[?25h"},
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    {
        Terminal::CursorHiddenScope scope = Terminal::scopedCursorHidden({.flushMode = IO::Types::FlushMode::Data});
        Hooks::forceNextFlushFailure(ErrorCode::FlushFailed);
        static_cast<void>(context.expectEq("cursor restore flush failure propagates", ErrorCode::FlushFailed, scope.restore().code));
        static_cast<void>(context.expectTrue("cursor restore flush failure remains retryable", scope.active()));
        static_cast<void>(context.expectTrue("cursor restore flush retry succeeds", scope.restore().ok()));
        static_cast<void>(context.expectFalse("cursor restore flush retry releases ownership", scope.active()));
    }
    static_cast<void>(context.expectEq(
        "cursor restore flush retry does not repeat the show transition",
        std::string{"\x1b[?25l\x1b[?25h"},
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    {
        Terminal::CursorHiddenScope failedDestructorScope = Terminal::scopedCursorHidden();
        Hooks::forceNextTextWriteFailure(ErrorCode::WriteFailed);
    }
    {
        Terminal::CursorHiddenScope laterScope = Terminal::scopedCursorHidden();
        static_cast<void>(context.expectTrue("scope nesting remains usable after destructor restoration failure", laterScope.restore().ok()));
    }
    static_cast<void>(context.expectEq(
        "failed scope destructor releases stale nesting ownership",
        std::string{"\x1b[?25l\x1b[?25l\x1b[?25h"},
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    {
        Terminal::AlternateScreenScope outer = Terminal::scopedAlternateScreen();
        static_cast<void>(context.expectTrue("outer alternate-screen scope active", outer.active()));
        {
            Terminal::AlternateScreenScope inner = Terminal::scopedAlternateScreen();
            static_cast<void>(context.expectTrue("inner alternate-screen scope active", inner.active()));
            static_cast<void>(context.expectTrue("inner alternate-screen leave succeeds", inner.leave().ok()));
            static_cast<void>(context.expectEq(
                "inner alternate-screen leave keeps mode active",
                std::string{"\x1b[?1049h"},
                Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));
        }
        static_cast<void>(context.expectTrue("outer alternate-screen leave succeeds", outer.leave().ok()));
    }
    static_cast<void>(context.expectEq(
        "nested alternate-screen scopes emit one enter and leave",
        std::string{"\x1b[?1049h\x1b[?1049l"},
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    Hooks::setOutputCapabilitiesOverride(Terminal::Types::Output::Stream::Stdout, redirectedOutputCapabilities());
    static_cast<void>(context.expectEq(
        "unsupported cursor movement fails",
        ErrorCode::Unsupported,
        Terminal::moveCursor(Terminal::Types::Cursor::MoveDirection::Down, 1).code));
    static_cast<void>(
        context.expectTrue("unsupported cursor movement writes nothing", Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout).empty()));

    Hooks::setOutputCapabilitiesOverride(Terminal::Types::Output::Stream::Stdout, unpreparedTerminalOutputCapabilities());
    Hooks::forceNextOutputPreparationFailure(ErrorCode::NativeFailure);
    static_cast<void>(context.expectEq(
        "control preparation failure propagates",
        ErrorCode::NativeFailure,
        Terminal::moveCursor(Terminal::Types::Cursor::MoveDirection::Down, 1).code));
    static_cast<void>(
        context.expectTrue("control preparation failure writes nothing", Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout).empty()));

    Hooks::setOutputCapabilitiesOverride(Terminal::Types::Output::Stream::Stdout, redirectedOutputCapabilities());
    static_cast<void>(context.expectTrue("zero move is no-op success", Terminal::moveCursor(Terminal::Types::Cursor::MoveDirection::Down, 0).ok()));
    static_cast<void>(context.expectEq(
        "zero move still validates direction",
        ErrorCode::InvalidArgument,
        Terminal::moveCursor(static_cast<Terminal::Types::Cursor::MoveDirection>(-1), 0).code));
    static_cast<void>(context.expectEq(
        "zero scroll still validates direction",
        ErrorCode::InvalidArgument,
        Terminal::scroll(static_cast<Terminal::Types::Output::ScrollDirection>(-1), 0).code));
    static_cast<void>(context.expectEq(
        "cursor movement rejects values beyond VT limit",
        ErrorCode::InvalidArgument,
        Terminal::moveCursor(Terminal::Types::Cursor::MoveDirection::Down, 32768).code));
    static_cast<void>(context.expectEq(
        "cursor position rejects values beyond VT limit",
        ErrorCode::InvalidArgument,
        Terminal::setCursorPosition({.column = 32767, .row = 0}).code));
    static_cast<void>(context.expectEq(
        "scroll rejects values beyond VT limit",
        ErrorCode::InvalidArgument,
        Terminal::scroll(Terminal::Types::Output::ScrollDirection::Down, 32768).code));

    const auto invalidOutputStream = static_cast<Terminal::Types::Output::Stream>(99);
    static_cast<void>(context.expectEq(
        "invalid output stream is rejected",
        ErrorCode::InvalidArgument,
        Terminal::writeText(invalidOutputStream, "must-not-write").code));
    static_cast<void>(context.expectEq(
        "invalid output capability stream is rejected",
        ErrorCode::InvalidArgument,
        Terminal::getOutputCapabilities(invalidOutputStream).status.code));

    const std::string oversizedTitle(255, 'x');
    static_cast<void>(context.expectEq("title rejects values beyond VT limit", ErrorCode::InvalidArgument, Terminal::setTitle(oversizedTitle).code));

    Hooks::forceNextFlushFailure(ErrorCode::FlushFailed);
    static_cast<void>(context.expectEq(
        "forced flush failure through control",
        ErrorCode::FlushFailed,
        Terminal::moveCursor(Terminal::Types::Cursor::MoveDirection::Down, 0, {.flushMode = IO::Types::FlushMode::Data}).code));

    Hooks::reset();
}
#endif
