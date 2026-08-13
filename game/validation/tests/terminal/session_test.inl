/// @file session_test.inl
/// @brief Focused terminal session correctness suites.

#if TERMINAL_INTERNAL_TEST_HOOKS
/// @brief Verifies persistent Session lifecycle, ownership, cancellation, and exact restoration.
void testSessions(TestSupport::Context &context)
{
    Hooks::reset();
    Hooks::setInputCapabilitiesOverride(Terminal::Types::Input::Stream::Stdin, terminalInputCapabilities());
    Hooks::setInputModeOverride(Terminal::Types::Input::Stream::Stdin, true, true, true);
    setupCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    Hooks::setTerminalSizeOverride(Terminal::Types::Output::Stream::Stdout, {.columns = 100, .rows = 30});
    Hooks::setCursorPositionOverride(Terminal::Types::Output::Stream::Stdout, {.column = 3, .row = 4});

    Terminal::Session closed;
    static_cast<void>(context.expectFalse("default session starts closed", closed.isOpen()));
    static_cast<void>(context.expectEq("closed session read reports NotOpen", ErrorCode::NotOpen, closed.readText().status.code));
    static_cast<void>(context.expectEq("closed session input query reports NotOpen", ErrorCode::NotOpen, closed.getInputCapabilities().status.code));
    static_cast<void>(context.expectEq("closed session write reports NotOpen", ErrorCode::NotOpen, closed.writeText("closed").code));
    static_cast<void>(
        context.expectEq("closed session output query reports NotOpen", ErrorCode::NotOpen, closed.getOutputCapabilities().status.code));
    static_cast<void>(context.expectEq("closed session prepare reports NotOpen", ErrorCode::NotOpen, closed.prepareOutput().status.code));
    static_cast<void>(context.expectEq("closed session size reports NotOpen", ErrorCode::NotOpen, closed.getTerminalSize().status.code));
    static_cast<void>(context.expectEq("closed session line write reports NotOpen", ErrorCode::NotOpen, closed.writeLine().code));
    static_cast<void>(context.expectEq("closed session byte write reports NotOpen", ErrorCode::NotOpen, closed.writeBytes({}).status.code));
    static_cast<void>(context.expectEq("closed session segment write reports NotOpen", ErrorCode::NotOpen, closed.writeSegments({}).code));
    static_cast<void>(context.expectEq("closed session print reports NotOpen", ErrorCode::NotOpen, closed.print("{}", 1).code));
    static_cast<void>(context.expectEq("closed session println reports NotOpen", ErrorCode::NotOpen, closed.println("{}", 1).code));
    static_cast<void>(context.expectEq("closed session flush reports NotOpen", ErrorCode::NotOpen, closed.flush().code));
    static_cast<void>(context.expectEq("closed session style reports NotOpen", ErrorCode::NotOpen, closed.resetStyle().code));
    static_cast<void>(context.expectEq(
        "closed session cursor move reports NotOpen",
        ErrorCode::NotOpen,
        closed.moveCursor(Terminal::Types::Cursor::MoveDirection::Up).code));
    static_cast<void>(context.expectEq("closed session cursor set reports NotOpen", ErrorCode::NotOpen, closed.setCursorPosition({}).code));
    static_cast<void>(context.expectEq("closed session cursor query reports NotOpen", ErrorCode::NotOpen, closed.getCursorPosition().status.code));
    static_cast<void>(context.expectEq("closed session cursor save reports NotOpen", ErrorCode::NotOpen, closed.saveCursorPosition().code));
    static_cast<void>(context.expectEq("closed session cursor restore reports NotOpen", ErrorCode::NotOpen, closed.restoreCursorPosition().code));
    static_cast<void>(context.expectEq("closed session visibility reports NotOpen", ErrorCode::NotOpen, closed.setCursorVisible(false).code));
    static_cast<void>(context.expectEq("closed session clear reports NotOpen", ErrorCode::NotOpen, closed.clear().code));
    static_cast<void>(context.expectEq(
        "closed session scroll reports NotOpen",
        ErrorCode::NotOpen,
        closed.scroll(Terminal::Types::Output::ScrollDirection::Up).code));
    static_cast<void>(context.expectEq("closed session alternate enter reports NotOpen", ErrorCode::NotOpen, closed.enterAlternateScreen().code));
    static_cast<void>(context.expectEq("closed session alternate leave reports NotOpen", ErrorCode::NotOpen, closed.leaveAlternateScreen().code));
    static_cast<void>(context.expectEq("closed session title reports NotOpen", ErrorCode::NotOpen, closed.setTitle("closed").code));
    static_cast<void>(context.expectEq("closed session bell reports NotOpen", ErrorCode::NotOpen, closed.ringBell().code));
    static_cast<void>(context.expectTrue("closing a closed session is idempotent", closed.close().ok()));

    Terminal::Types::SessionOptions streamOptions;
    streamOptions.deliveryMode = Terminal::Types::Input::DeliveryMode::Stream;

    Terminal::Session session;
    static_cast<void>(context.expectTrue("stream session opens", session.open(streamOptions).ok()));
    static_cast<void>(context.expectTrue("opened session reports open", session.isOpen()));
    static_cast<void>(context.expectTrue(
        "stream session uses immediate managed input mode",
        Hooks::inputModeOverrideMatches(Terminal::Types::Input::Stream::Stdin, false, false, true)));
    static_cast<void>(context.expectTrue(
        "stream session enables resize records without mouse/Quick Edit",
        Hooks::inputManagedEventModeOverrideMatches(Terminal::Types::Input::Stream::Stdin, true, false, true)));

    const Terminal::Types::Input::CapabilitiesResult sessionInputCapabilities = session.getInputCapabilities();
    static_cast<void>(context.expectTrue("session input capability query succeeds", sessionInputCapabilities.status.ok()));
    static_cast<void>(context.expectTrue("session reuses captured event capability", sessionInputCapabilities.capabilities.supportsEventInput));

    const Terminal::Types::Output::CapabilitiesResult sessionOutputCapabilities = session.getOutputCapabilities();
    static_cast<void>(context.expectTrue("session output capability query succeeds", sessionOutputCapabilities.status.ok()));
    static_cast<void>(context.expectTrue("session output capability is bound stdout", sessionOutputCapabilities.capabilities.supportsUtf8Text));
    static_cast<void>(context.expectTrue("session output preparation succeeds", session.prepareOutput().status.ok()));

    const Terminal::Types::SizeResult sessionSize = session.getTerminalSize();
    static_cast<void>(context.expectTrue("session size query succeeds", sessionSize.status.ok()));
    static_cast<void>(context.expectEq("session size query uses bound output", std::uint32_t{100}, sessionSize.size.columns));

    const Terminal::Types::Cursor::PositionResult sessionPosition =
        session.getCursorPosition({.timeout = Terminal::kNoWait, .flushMode = IO::Types::FlushMode::None});
    static_cast<void>(context.expectTrue("session cursor position query succeeds", sessionPosition.status.ok()));
    static_cast<void>(context.expectEq("session cursor query uses owned input", std::uint32_t{3}, sessionPosition.position.column));

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    Terminal::Types::Output::LineOptions sessionLineOptions;
    sessionLineOptions.lineEnding = Terminal::Types::Output::LineEnding::Lf;
    static_cast<void>(context.expectTrue("session text write succeeds", session.writeText("session").ok()));
    static_cast<void>(context.expectTrue("session line write succeeds", session.writeLine("-line", sessionLineOptions).ok()));
    static_cast<void>(context.expectTrue("session print succeeds", session.print("-{}", 7).ok()));
    static_cast<void>(context.expectTrue("session println succeeds", session.println(sessionLineOptions, "-{}", 8).ok()));

    const std::string sessionBytesText = "!";
    const IO::Types::WriteResult sessionBytes = session.writeBytes(bytesOf(sessionBytesText));
    static_cast<void>(context.expectTrue("session byte write succeeds", sessionBytes.status.ok()));
    static_cast<void>(context.expectEq("session byte write count", std::size_t{1}, sessionBytes.bytesWritten));

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

    static_cast<void>(context.expectEq("same session re-open reports AlreadyOpen", ErrorCode::AlreadyOpen, session.open(streamOptions).code));

    Terminal::Session competing;
    static_cast<void>(context.expectEq("competing session reports ResourceBusy", ErrorCode::ResourceBusy, competing.open(streamOptions).code));
    static_cast<void>(context.expectEq("direct read conflicts with session ownership", ErrorCode::ResourceBusy, Terminal::readText().status.code));

    Hooks::setInputBytes(Terminal::Types::Input::Stream::Stdin, "session");
    const Terminal::Types::Input::TextResult sessionText = session.readText();
    static_cast<void>(context.expectTrue("session text read succeeds", sessionText.status.ok()));
    static_cast<void>(context.expectEq("session text read payload", std::string{"session"}, sessionText.text));

    Hooks::setInputBytes(Terminal::Types::Input::Stream::Stdin, "preserved");
    static_cast<void>(context.expectEq(
        "stream session rejects event consumer",
        ErrorCode::Unsupported,
        session.readEvent({.timeout = Terminal::kNoWait}).status.code));
    static_cast<void>(context.expectEq("incompatible event read consumes nothing", std::string{"preserved"}, session.readText().text));

    Hooks::setInputBytes(Terminal::Types::Input::Stream::Stdin, "deadline");
    Terminal::Types::Input::TextOptions negativeTimeout;
    negativeTimeout.timeout = std::chrono::milliseconds{-1};
    static_cast<void>(
        context.expectEq("negative read deadline is rejected", ErrorCode::InvalidArgument, session.readText(negativeTimeout).status.code));
    static_cast<void>(context.expectEq("negative deadline consumes nothing", std::string{"deadline"}, session.readText().text));

    Hooks::setInputBytes(Terminal::Types::Input::Stream::Stdin, "cancelled");
    std::stop_source stopSource;
    stopSource.request_stop();
    Terminal::Types::Input::TextOptions cancelledOptions;
    cancelledOptions.stopToken = stopSource.get_token();
    const Terminal::Types::Input::TextResult cancelled = session.readText(cancelledOptions);
    static_cast<void>(context.expectTrue("pre-cancelled read keeps success status", cancelled.status.ok()));
    static_cast<void>(context.expectEq("pre-cancelled read outcome", Terminal::Types::Input::ReadOutcome::Cancelled, cancelled.outcome));
    static_cast<void>(context.expectEq("pre-cancelled read consumes nothing", std::string{"cancelled"}, session.readText().text));

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    static_cast<void>(context.expectTrue("session owns hidden cursor state", session.setCursorVisible(false).ok()));
    static_cast<void>(context.expectTrue("session owns alternate-screen state", session.enterAlternateScreen().ok()));

    Hooks::forceNextInputModeFailure(ErrorCode::NativeFailure);
    static_cast<void>(context.expectEq("session input restoration failure propagates", ErrorCode::NativeFailure, session.close().code));
    static_cast<void>(context.expectTrue("failed close leaves session open", session.isOpen()));
    static_cast<void>(context.expectEq(
        "session restores persistent output in reverse order before input",
        std::string{"\x1b[?25l\x1b[?1049h\x1b[?1049l\x1b[?25h"},
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));
    static_cast<void>(context.expectEq("failed close retains ownership", ErrorCode::ResourceBusy, competing.open(streamOptions).code));
    static_cast<void>(context.expectTrue("session close retry succeeds", session.close().ok()));
    static_cast<void>(context.expectFalse("successful close clears open state", session.isOpen()));
    static_cast<void>(context.expectTrue(
        "successful close restores exact input mode",
        Hooks::inputModeOverrideMatches(Terminal::Types::Input::Stream::Stdin, true, true, true)));
    static_cast<void>(context.expectTrue(
        "successful close restores exact managed-event flags",
        Hooks::inputManagedEventModeOverrideMatches(Terminal::Types::Input::Stream::Stdin, false, false, false)));
    static_cast<void>(context.expectEq("session output after close reports NotOpen", ErrorCode::NotOpen, session.writeText("closed").code));

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

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    Terminal::Session setupFlushFailure;
    static_cast<void>(context.expectTrue("setup flush failure fixture opens", setupFlushFailure.open(streamOptions).ok()));
    Hooks::forceNextFlushFailure(ErrorCode::FlushFailed);
    static_cast<void>(context.expectEq(
        "Session cursor setup flush failure propagates",
        ErrorCode::FlushFailed,
        setupFlushFailure.setCursorVisible(false, {.flushMode = IO::Types::FlushMode::Data}).code));
    static_cast<void>(context.expectTrue("Session retains cleanup after setup flush failure", setupFlushFailure.close().ok()));
    static_cast<void>(context.expectEq(
        "Session setup flush failure does not lose cleanup ownership",
        std::string{"\x1b[?25l\x1b[?25h"},
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    Terminal::Session outputRestoreFailure;
    static_cast<void>(context.expectTrue("output restoration failure fixture opens", outputRestoreFailure.open(streamOptions).ok()));
    static_cast<void>(context.expectTrue("output restoration fixture hides cursor", outputRestoreFailure.setCursorVisible(false).ok()));
    static_cast<void>(context.expectTrue("output restoration fixture enters alternate screen", outputRestoreFailure.enterAlternateScreen().ok()));
    Hooks::forceNextTextWriteFailure(ErrorCode::WriteFailed);
    static_cast<void>(
        context.expectEq("output restoration failure propagates from close", ErrorCode::WriteFailed, outputRestoreFailure.close().code));
    static_cast<void>(context.expectTrue("output restoration failure leaves session open", outputRestoreFailure.isOpen()));
    static_cast<void>(context.expectEq(
        "failed top restoration does not run older obligations",
        std::string{"\x1b[?25l\x1b[?1049h"},
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));
    static_cast<void>(
        context.expectEq("output restoration failure retains input ownership", ErrorCode::ResourceBusy, competing.open(streamOptions).code));
    static_cast<void>(context.expectTrue("output restoration close retry succeeds", outputRestoreFailure.close().ok()));
    static_cast<void>(context.expectEq(
        "output restoration retry preserves reverse order",
        std::string{"\x1b[?25l\x1b[?1049h\x1b[?1049l\x1b[?25h"},
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    Terminal::Session flushRestoreFailure;
    static_cast<void>(context.expectTrue("restoration flush failure fixture opens", flushRestoreFailure.open(streamOptions).ok()));
    const Terminal::Types::Output::ControlOptions flushedControl{.flushMode = IO::Types::FlushMode::Data};
    static_cast<void>(context.expectTrue("restoration flush fixture hides cursor", flushRestoreFailure.setCursorVisible(false, flushedControl).ok()));
    Hooks::forceNextFlushFailure(ErrorCode::FlushFailed);
    static_cast<void>(context.expectEq("close restoration flush failure propagates", ErrorCode::FlushFailed, flushRestoreFailure.close().code));
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
    static_cast<void>(context.expectEq("stderr-bound size query", std::uint32_t{72}, stderrSession.getTerminalSize().size.columns));
    static_cast<void>(context.expectEq(
        "stderr-bound cursor query",
        std::uint32_t{11},
        stderrSession.getCursorPosition({.timeout = Terminal::kNoWait, .flushMode = IO::Types::FlushMode::None}).position.column));
    static_cast<void>(context.expectTrue("stderr-bound session closes", stderrSession.close().ok()));

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    Hooks::setInputBytes(Terminal::Types::Input::Stream::Stdin, "blocked-read");
    Terminal::Session concurrencySession;
    static_cast<void>(context.expectTrue("concurrency fixture opens", concurrencySession.open(streamOptions).ok()));

    Hooks::blockNextTextWrite();
    IO::Types::Status sessionSerializedWrite;
    IO::Types::Status globalSerializedWrite;
    std::jthread sessionWriter(
        [&]
        {
            sessionSerializedWrite = concurrencySession.writeText("session-first");
        });
    const bool writeReachedBlock = Hooks::waitUntilTextWriteBlocked();
    static_cast<void>(context.expectTrue("session write reaches deterministic backend gate", writeReachedBlock));
    std::jthread globalWriter(
        [&]
        {
            globalSerializedWrite = Terminal::writeText("-global-second");
        });
    Hooks::releaseBlockedTextWrite();
    sessionWriter.join();
    globalWriter.join();
    static_cast<void>(context.expectTrue("serialized session write succeeds", sessionSerializedWrite.ok()));
    static_cast<void>(context.expectTrue("serialized global write succeeds", globalSerializedWrite.ok()));
    static_cast<void>(context.expectEq(
        "Session and global writes use one stream serialization domain",
        std::string{"session-first-global-second"},
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    std::latch formatterEntered{1};
    std::latch releaseFormatter{1};
    IO::Types::Status blockedFormatStatus;
    std::jthread formatterThread(
        [&]
        {
            blockedFormatStatus =
                concurrencySession.print("{}", TerminalSessionBlockingFormat{.entered = &formatterEntered, .release = &releaseFormatter});
        });
    formatterEntered.wait();

    std::atomic_bool formattingCloseFinished = false;
    std::latch formattingCloseStarted{1};
    IO::Types::Status formattingCloseStatus;
    std::jthread formattingCloser(
        [&]
        {
            formattingCloseStarted.count_down();
            formattingCloseStatus = concurrencySession.close();
            formattingCloseFinished.store(true, std::memory_order_release);
        });
    formattingCloseStarted.wait();
    static_cast<void>(context.expectFalse(
        "close waits while a Session formatter owns an active operation",
        formattingCloseFinished.load(std::memory_order_acquire)));
    releaseFormatter.count_down();
    formatterThread.join();
    formattingCloser.join();
    static_cast<void>(context.expectTrue("blocked Session formatting completes", blockedFormatStatus.ok()));
    static_cast<void>(context.expectTrue("close completes after Session formatting", formattingCloseStatus.ok()));
    static_cast<void>(context.expectEq(
        "blocked Session formatter emits its complete outer record",
        std::string{"blocked-format"},
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));
    static_cast<void>(context.expectTrue("concurrency fixture reopens after formatting close", concurrencySession.open(streamOptions).ok()));

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    Hooks::blockNextRead();
    Terminal::Types::Input::TextResult blockedRead;
    std::jthread reader(
        [&]
        {
            blockedRead = concurrencySession.readText();
        });
    const bool readReachedBlock = Hooks::waitUntilReadBlocked();
    static_cast<void>(context.expectTrue("blocking read reaches deterministic backend gate", readReachedBlock));
    static_cast<void>(
        context.expectTrue("session output remains usable during blocked session input", concurrencySession.writeText("during-read").ok()));
    static_cast<void>(context.expectEq(
        "output during blocked read is emitted immediately",
        std::string{"during-read"},
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));

    std::atomic_bool closeFinished = false;
    std::latch closeStarted{1};
    IO::Types::Status concurrentCloseStatus;
    std::jthread closer(
        [&]
        {
            closeStarted.count_down();
            concurrentCloseStatus = concurrencySession.close();
            closeFinished.store(true, std::memory_order_release);
        });
    closeStarted.wait();
    static_cast<void>(
        context.expectFalse("close waits while a session read owns shared lifecycle access", closeFinished.load(std::memory_order_acquire)));
    Hooks::releaseBlockedRead();
    reader.join();
    closer.join();
    static_cast<void>(context.expectTrue("blocked read completes after release", blockedRead.status.ok()));
    static_cast<void>(context.expectEq("blocked read payload", std::string{"blocked-read"}, blockedRead.text));
    static_cast<void>(context.expectTrue("close completes after active read", concurrentCloseStatus.ok()));
    static_cast<void>(context.expectFalse("concurrency fixture is closed", concurrencySession.isOpen()));

    Terminal::Session movable;
    static_cast<void>(context.expectTrue("movable session opens", movable.open(streamOptions).ok()));
    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    static_cast<void>(context.expectTrue("movable session hides cursor", movable.setCursorVisible(false).ok()));
    static_cast<void>(context.expectTrue("movable session enters alternate screen", movable.enterAlternateScreen().ok()));
    Terminal::Session moved(std::move(movable));
    // NOLINTNEXTLINE(bugprone-use-after-move) -- Session explicitly specifies a closed, queryable moved-from state.
    static_cast<void>(context.expectFalse("moved-from session becomes closed", movable.isOpen()));
    static_cast<void>(context.expectTrue("move construction preserves open ownership", moved.isOpen()));
    static_cast<void>(context.expectEq("moved session still blocks competitors", ErrorCode::ResourceBusy, competing.open(streamOptions).code));
    static_cast<void>(context.expectTrue("moved session closes", moved.close().ok()));
    static_cast<void>(context.expectEq(
        "move construction preserves pending output cleanup",
        std::string{"\x1b[?25l\x1b[?1049h\x1b[?1049l\x1b[?25h"},
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));

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
        Hooks::forceNextTextWriteFailure(ErrorCode::WriteFailed);
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

    Terminal::Session events;
    static_cast<void>(context.expectTrue("default event session opens with event-capable hook", events.open().ok()));
    static_cast<void>(context.expectEq("event session rejects stream reads", ErrorCode::Unsupported, events.readText().status.code));

    std::stop_source eventStopSource;
    eventStopSource.request_stop();
    Terminal::Types::Input::EventOptions eventCancelledOptions;
    eventCancelledOptions.stopToken = eventStopSource.get_token();
    const Terminal::Types::Input::EventResult cancelledEvent = events.readEvent(eventCancelledOptions);
    static_cast<void>(context.expectTrue("event cancellation keeps success status", cancelledEvent.status.ok()));
    static_cast<void>(context.expectEq("event cancellation outcome", Terminal::Types::Input::ReadOutcome::Cancelled, cancelledEvent.outcome));
    static_cast<void>(context.expectTrue("event session closes", events.close().ok()));

    Terminal::Types::SessionOptions reportControlOptions = streamOptions;
    reportControlOptions.controlKeyMode = Terminal::Types::Input::ControlKeyMode::ReportAsInput;
    Terminal::Session reportControl;
    static_cast<void>(context.expectTrue("report-as-input session opens", reportControl.open(reportControlOptions).ok()));
    static_cast<void>(context.expectTrue(
        "report-as-input disables native control processing",
        Hooks::inputModeOverrideMatches(Terminal::Types::Input::Stream::Stdin, false, false, false)));
    static_cast<void>(context.expectTrue("report-as-input session closes", reportControl.close().ok()));

    Terminal::Types::SessionOptions invalidOptions = streamOptions;
    invalidOptions.deliveryMode = static_cast<Terminal::Types::Input::DeliveryMode>(99);
    static_cast<void>(context.expectEq("invalid session delivery mode is rejected", ErrorCode::InvalidArgument, competing.open(invalidOptions).code));

    Hooks::reset();
}
#endif
