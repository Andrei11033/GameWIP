/// @file formatting_test.inl
/// @brief Terminal formatting and encoding tests.

/// ------------------------------------------------------------
/// Formatting and encoding
/// ------------------------------------------------------------

/// @brief Verifies formatter reentry without allowing a deadlock to hang the parent suite.
void testReentrantFormatting(TestSupport::Context &context, const std::filesystem::path &executablePath)
{
    // CTest can delay a fresh validation child on Windows; the dedicated timeout tests keep
    // short-deadline coverage separate from this normal-success path.
    constexpr auto normalChildTimeout = std::chrono::seconds{30};
    TestSupport::Types::Process::Options child;
    child.executablePath = std::filesystem::path(executablePath);
    child.arguments = {std::string(kReentrantFormatChildArgument)};
    child.waitTimeout = normalChildTimeout;
    child.captureOutput = true;

    // Verify global Terminal reentry from a formatter.
    const TestSupport::Types::Process::Result result = TestSupport::runChildProcess(child);
    static_cast<void>(context.expectTrue("reentrant formatter child infrastructure succeeds", result.ok()));
    static_cast<void>(context.expectEq("reentrant formatter child exits", TestSupport::Types::Process::Outcome::Exited, result.value().outcome));
    static_cast<void>(context.expectEq("reentrant formatter child returns zero", std::uint32_t{0}, result.value().exitCode));
    static_cast<void>(
        context.expectEq("reentrant formatter preserves nested and outer output", std::string{"innerouterinnerouter\n"}, result.value().outputBytes));

    // Verify reentry across Session and global output.
    child.arguments = {std::string(kSessionReentrantFormatChildArgument)};
    const TestSupport::Types::Process::Result sessionResult = TestSupport::runChildProcess(child);
    static_cast<void>(context.expectTrue("Session reentrant formatter child infrastructure succeeds", sessionResult.ok()));
    static_cast<void>(
        context.expectEq("Session reentrant formatter child exits", TestSupport::Types::Process::Outcome::Exited, sessionResult.value().outcome));
    static_cast<void>(context.expectEq("Session reentrant formatter child returns zero", std::uint32_t{0}, sessionResult.value().exitCode));
    static_cast<void>(context.expectEq(
        "Session formatter supports nested Session/global output and checked close",
        std::string{"innerouterinnerouter\nglobalouterouter"},
        sessionResult.value().outputBytes));
}

#if TERMINAL_INTERNAL_TEST_HOOKS
/// @brief Verifies that every Terminal Text lane rejects malformed UTF-8 before output while Bytes remain arbitrary.
void testUtf8OutputContracts(TestSupport::Context &context)
{
    Hooks::reset();
    setupCapturedOutput(Terminal::Types::Output::Stream::Stdout);

    const std::string malformed{"ok\xff", 3};
    const std::string incomplete{"ok\xe2\x82", 4};

    // Reject malformed text before writing.
    const auto malformedText = Terminal::writeText(malformed);
    static_cast<void>(context.expectEq("redirected text rejects malformed UTF-8", ErrorCode::EncodingFailed, malformedText.status().code()));
    static_cast<void>(context.expectTrue("malformed text emits no bytes", Hooks::capturedOutput(Terminal::Types::Output::Stream::Stdout).empty()));

    const auto incompleteLine = Terminal::writeLine(incomplete);
    static_cast<void>(context.expectEq("redirected line rejects incomplete UTF-8", ErrorCode::EncodingFailed, incompleteLine.status().code()));
    static_cast<void>(context.expectTrue("incomplete line emits no bytes", Hooks::capturedOutput(Terminal::Types::Output::Stream::Stdout).empty()));

    const auto malformedFormatted = Terminal::print("{}", malformed);
    static_cast<void>(context.expectEq("formatted output rejects malformed UTF-8", ErrorCode::EncodingFailed, malformedFormatted.status().code()));
    static_cast<void>(
        context.expectTrue("malformed formatted output emits no bytes", Hooks::capturedOutput(Terminal::Types::Output::Stream::Stdout).empty()));

    // Preserve buffered text when an append is invalid.
    Terminal::OutputBuffer buffer;
    static_cast<void>(context.expectTrue("buffer accepts trusted UTF-8 prefix", buffer.appendText("prefix").ok()));
    const std::string before{buffer.text()};
    static_cast<void>(context.expectEq("buffer rejects malformed append", ErrorCode::EncodingFailed, buffer.appendText(malformed).status().code()));
    static_cast<void>(context.expectEq("buffer preserves previous text after malformed append", before, std::string{buffer.text()}));
    static_cast<void>(context.expectEq("buffer rejects incomplete line append", ErrorCode::EncodingFailed, buffer.appendLine(incomplete).status().code()));
    static_cast<void>(context.expectEq("buffer preserves previous text after incomplete line append", before, std::string{buffer.text()}));
    static_cast<void>(
        context.expectEq("buffer rejects malformed formatted suffix", ErrorCode::EncodingFailed, buffer.print("{}", malformed).status().code()));
    static_cast<void>(context.expectEq("buffer rolls back malformed formatted suffix", before, std::string{buffer.text()}));

    // Keep arbitrary bytes separate from UTF-8 text segments.
    const std::array<Terminal::Types::Output::Segment, 2> invalidTextSegments{
        Terminal::textSegment("good"),
        Terminal::textSegment(malformed),
    };
    static_cast<void>(context.expectEq(
        "segmented text rejects malformed UTF-8 before output",
        ErrorCode::EncodingFailed,
        Terminal::writeSegments(invalidTextSegments).status().code()));
    static_cast<void>(
        context.expectTrue("invalid segmented text emits nothing", Hooks::capturedOutput(Terminal::Types::Output::Stream::Stdout).empty()));

    const std::array<std::byte, 3> arbitraryBytes{std::byte{0xff}, std::byte{0x00}, std::byte{0x80}};
    const std::array<Terminal::Types::Output::Segment, 1> byteSegments{Terminal::byteSegment(arbitraryBytes)};
    static_cast<void>(context.expectTrue("byte segment accepts arbitrary non-UTF-8 data", Terminal::writeSegments(byteSegments).ok()));
    static_cast<void>(context.expectEq(
        "byte segment preserves arbitrary byte count",
        arbitraryBytes.size(),
        Hooks::capturedOutput(Terminal::Types::Output::Stream::Stdout).size()));

    Hooks::reset();
}
#endif
