/// @file session_concurrency_test.inl
/// @brief Focused Terminal Session test phases.

#if TERMINAL_INTERNAL_TEST_HOOKS

/// @brief Checks concurrent Session reads, writes, and close coordination.
void testSessionConcurrency(TestSupport::Context &context, const Terminal::Types::SessionOptions &streamOptions)
{
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

    // Closing waits for a formatter that owns an active operation.

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

    // A blocked read keeps input ownership while output remains usable.

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
}

#endif
