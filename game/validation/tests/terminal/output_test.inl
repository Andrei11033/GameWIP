/// @file output_test.inl
/// @brief Focused terminal output correctness suites.

/// @brief Verifies formatter reentry without allowing a deadlock to hang the parent suite.
void testReentrantFormatting(TestSupport::Context &context, std::string_view executablePath)
{
    TestSupport::Types::ChildProcessOptions child;
    child.executablePath = std::filesystem::path(executablePath);
    child.arguments = {std::string(kReentrantFormatChildArgument)};
    child.timeout = std::chrono::milliseconds{5000};
    child.captureOutput = true;

    const TestSupport::Types::ChildProcessResult result = TestSupport::runChildProcess(child);
    static_cast<void>(context.expectTrue("reentrant formatter child infrastructure succeeds", result.status.ok()));
    static_cast<void>(context.expectEq("reentrant formatter child exits", TestSupport::Types::ChildProcessOutcome::Exited, result.outcome));
    static_cast<void>(context.expectEq("reentrant formatter child returns zero", std::uint32_t{0}, result.exitCode));
    static_cast<void>(
        context.expectEq("reentrant formatter preserves nested and outer output", std::string{"innerouterinnerouter\n"}, result.output));

    child.arguments = {std::string(kSessionReentrantFormatChildArgument)};
    const TestSupport::Types::ChildProcessResult sessionResult = TestSupport::runChildProcess(child);
    static_cast<void>(context.expectTrue("Session reentrant formatter child infrastructure succeeds", sessionResult.status.ok()));
    static_cast<void>(
        context.expectEq("Session reentrant formatter child exits", TestSupport::Types::ChildProcessOutcome::Exited, sessionResult.outcome));
    static_cast<void>(context.expectEq("Session reentrant formatter child returns zero", std::uint32_t{0}, sessionResult.exitCode));
    static_cast<void>(context.expectEq(
        "Session formatter supports nested Session/global output and checked close",
        std::string{"innerouterinnerouter\nglobalouterouter"},
        sessionResult.output));
}

#if TERMINAL_INTERNAL_TEST_HOOKS
/// @brief Verifies that every Terminal Text lane rejects malformed UTF-8 before output while Bytes remain arbitrary.
void testUtf8OutputContracts(TestSupport::Context &context)
{
    Hooks::reset();
    setupCapturedOutput(Terminal::Types::Output::Stream::Stdout);

    const std::string malformed{"ok\xff", 3};
    const std::string incomplete{"ok\xe2\x82", 4};

    const IO::Types::Status malformedText = Terminal::writeText(malformed);
    static_cast<void>(context.expectEq("redirected text rejects malformed UTF-8", ErrorCode::EncodingFailed, malformedText.code));
    static_cast<void>(context.expectTrue("malformed text emits no bytes", Hooks::capturedOutput(Terminal::Types::Output::Stream::Stdout).empty()));

    const IO::Types::Status incompleteLine = Terminal::writeLine(incomplete);
    static_cast<void>(context.expectEq("redirected line rejects incomplete UTF-8", ErrorCode::EncodingFailed, incompleteLine.code));
    static_cast<void>(context.expectTrue("incomplete line emits no bytes", Hooks::capturedOutput(Terminal::Types::Output::Stream::Stdout).empty()));

    const IO::Types::Status malformedFormatted = Terminal::print("{}", malformed);
    static_cast<void>(context.expectEq("formatted output rejects malformed UTF-8", ErrorCode::EncodingFailed, malformedFormatted.code));
    static_cast<void>(
        context.expectTrue("malformed formatted output emits no bytes", Hooks::capturedOutput(Terminal::Types::Output::Stream::Stdout).empty()));

    Terminal::OutputBuffer buffer;
    static_cast<void>(context.expectTrue("buffer accepts trusted UTF-8 prefix", buffer.appendText("prefix").ok()));
    const std::string before{buffer.text()};
    static_cast<void>(context.expectEq("buffer rejects malformed append", ErrorCode::EncodingFailed, buffer.appendText(malformed).code));
    static_cast<void>(context.expectEq("buffer preserves previous text after malformed append", before, std::string{buffer.text()}));
    static_cast<void>(context.expectEq("buffer rejects incomplete line append", ErrorCode::EncodingFailed, buffer.appendLine(incomplete).code));
    static_cast<void>(context.expectEq("buffer preserves previous text after incomplete line append", before, std::string{buffer.text()}));
    static_cast<void>(context.expectEq("buffer rejects malformed formatted suffix", ErrorCode::EncodingFailed, buffer.print("{}", malformed).code));
    static_cast<void>(context.expectEq("buffer rolls back malformed formatted suffix", before, std::string{buffer.text()}));

    const std::array<Terminal::Types::Output::Segment, 2> invalidTextSegments{
        Terminal::textSegment("good"),
        Terminal::textSegment(malformed),
    };
    static_cast<void>(context.expectEq(
        "segmented text rejects malformed UTF-8 before output",
        ErrorCode::EncodingFailed,
        Terminal::writeSegments(invalidTextSegments).code));
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

#if TERMINAL_INTERNAL_TEST_HOOKS
/// @brief Verifies plain, formatted, buffered, styled, and line-oriented text output.
void testTextAndStyleOutput(TestSupport::Context &context)
{
    Hooks::reset();
    setupCapturedOutput(Terminal::Types::Output::Stream::Stdout);

    Terminal::Types::Output::LineOptions plainOptions;
    plainOptions.lineEnding = Terminal::Types::Output::LineEnding::Lf;
    plainOptions.flushMode = IO::Types::FlushMode::Data;
    static_cast<void>(context.expectTrue("plain write succeeds", Terminal::writeLine("hello", plainOptions).ok()));
    static_cast<void>(
        context.expectEq("plain write capture", std::string{"hello\n"}, Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    Terminal::Types::Output::LineOptions invalidLineEndingOptions;
    invalidLineEndingOptions.lineEnding = static_cast<Terminal::Types::Output::LineEnding>(-1);
    static_cast<void>(context.expectEq(
        "invalid line ending is rejected before output",
        ErrorCode::InvalidArgument,
        Terminal::writeLine("must-not-write", invalidLineEndingOptions).code));
    static_cast<void>(
        context.expectTrue("invalid line ending writes nothing", Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout).empty()));

    Terminal::Types::Output::TextOptions invalidFlushOptions;
    invalidFlushOptions.flushMode = static_cast<IO::Types::FlushMode>(-1);
    static_cast<void>(context.expectEq(
        "invalid text flush mode is rejected before output",
        ErrorCode::InvalidArgument,
        Terminal::writeText("must-not-write", invalidFlushOptions).code));
    static_cast<void>(
        context.expectTrue("invalid text flush mode writes nothing", Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout).empty()));
    static_cast<void>(context.expectEq(
        "invalid direct flush mode is rejected",
        ErrorCode::InvalidArgument,
        Terminal::flush(static_cast<IO::Types::FlushMode>(-1)).code));

    Terminal::OutputBuffer lineEndingBuffer;
    static_cast<void>(
        context.expectEq("output buffer defaults to native line ending", Terminal::Types::Output::LineEnding::Native, lineEndingBuffer.lineEnding()));
    static_cast<void>(context.expectEq(
        "output buffer rejects invalid line ending without throwing",
        ErrorCode::InvalidArgument,
        lineEndingBuffer.setLineEnding(static_cast<Terminal::Types::Output::LineEnding>(-1)).code));
    static_cast<void>(context.expectEq(
        "invalid line ending preserves previous setting",
        Terminal::Types::Output::LineEnding::Native,
        lineEndingBuffer.lineEnding()));

    Terminal::Types::Style::Request style;
    style.foreground = Terminal::basicColor(Terminal::Types::Style::BasicColor::BrightRed);
    style.bold = true;

    Terminal::Types::Output::TextOptions styledOptions;
    styledOptions.style = style;
    styledOptions.styleMode = Terminal::Types::Style::Mode::Auto;
    static_cast<void>(context.expectTrue("styled write succeeds", Terminal::writeText("hot", styledOptions).ok()));
    static_cast<void>(context.expectEq(
        "styled write emits SGR and reset",
        std::string{"\x1b[1;91mhot\x1b[0m"},
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));
    static_cast<void>(
        context.expectEq("styled write uses one backend call", std::size_t{2}, Hooks::textWriteCallCount(Terminal::Types::Output::Stream::Stdout)));

    Hooks::reset();
    setupCapturedOutput(Terminal::Types::Output::Stream::Stdout, redirectedOutputCapabilities());
    static_cast<void>(context.expectTrue("redirected styled output falls back to plain text", Terminal::writeText("redirected", styledOptions).ok()));
    static_cast<void>(context.expectEq(
        "redirected styled output does not prepare",
        std::size_t{0},
        Hooks::outputPreparationCallCount(Terminal::Types::Output::Stream::Stdout)));
    static_cast<void>(context.expectEq(
        "redirected styled output capture is plain",
        std::string{"redirected"},
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));

    Hooks::reset();
    setupCapturedOutput(Terminal::Types::Output::Stream::Stdout, unpreparedTerminalOutputCapabilities());
    Hooks::setPreparedOutputCapabilitiesOverride(Terminal::Types::Output::Stream::Stdout, terminalOutputCapabilities());
    static_cast<void>(context.expectTrue("lazy preparation enables styled output", Terminal::writeText("lazy", styledOptions).ok()));
    static_cast<void>(
        context.expectEq("lazy preparation count", std::size_t{1}, Hooks::outputPreparationCallCount(Terminal::Types::Output::Stream::Stdout)));
    static_cast<void>(context.expectEq(
        "lazy styled output capture",
        std::string{"\x1b[1;91mlazy\x1b[0m"},
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    Hooks::setOutputCapabilitiesOverride(Terminal::Types::Output::Stream::Stdout, unpreparedTerminalOutputCapabilities());
    Hooks::forceNextOutputPreparationFailure(ErrorCode::NativeFailure);
    static_cast<void>(context.expectTrue("auto style fallback succeeds", Terminal::writeText("plain", styledOptions).ok()));
    static_cast<void>(
        context.expectEq("auto style fallback is plain", std::string{"plain"}, Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    styledOptions.styleMode = Terminal::Types::Style::Mode::Required;
    Hooks::forceNextOutputPreparationFailure(ErrorCode::NativeFailure);
    static_cast<void>(
        context.expectEq("forced preparation failure propagates", ErrorCode::NativeFailure, Terminal::writeText("fail", styledOptions).code));
    static_cast<void>(
        context.expectTrue("forced unsupported style writes nothing", Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout).empty()));

    setupCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    static_cast<void>(context.expectTrue("resetStyle succeeds", Terminal::resetStyle().ok()));
    static_cast<void>(
        context.expectEq("resetStyle emits reset", std::string{"\x1b[0m"}, Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));

    Hooks::forceNextTextWriteFailure(ErrorCode::PermissionDenied);
    static_cast<void>(context.expectEq("forced text write failure", ErrorCode::PermissionDenied, Terminal::writeText("blocked").code));

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    const std::size_t printWritesBefore = Hooks::textWriteCallCount(Terminal::Types::Output::Stream::Stdout);
    static_cast<void>(context.expectTrue("formatted print succeeds", Terminal::print("value {}", 42).ok()));
    static_cast<void>(
        context.expectEq("formatted print capture", std::string{"value 42"}, Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));
    static_cast<void>(context.expectEq(
        "formatted print uses one backend call",
        printWritesBefore + 1,
        Hooks::textWriteCallCount(Terminal::Types::Output::Stream::Stdout)));

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    static_cast<void>(
        context.expectEq("formatted print failure returns status", ErrorCode::InvalidArgument, Terminal::print("{}", TerminalThrowingFormat{}).code));
    static_cast<void>(
        context.expectTrue("formatted print failure writes nothing", Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout).empty()));

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    Terminal::Types::Output::LineOptions printLineOptions;
    printLineOptions.lineEnding = Terminal::Types::Output::LineEnding::Lf;
    const std::size_t printlnWritesBefore = Hooks::textWriteCallCount(Terminal::Types::Output::Stream::Stdout);
    static_cast<void>(context.expectTrue("formatted println succeeds", Terminal::println(printLineOptions, "line {}", 7).ok()));
    static_cast<void>(
        context.expectEq("formatted println capture", std::string{"line 7\n"}, Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));
    static_cast<void>(context.expectEq(
        "formatted println uses one backend call",
        printlnWritesBefore + 1,
        Hooks::textWriteCallCount(Terminal::Types::Output::Stream::Stdout)));

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    static_cast<void>(context.expectEq(
        "formatted println failure returns status",
        ErrorCode::InvalidArgument,
        Terminal::println(printLineOptions, "{}", TerminalThrowingFormat{}).code));
    static_cast<void>(
        context.expectTrue("formatted println failure writes nothing", Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout).empty()));

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    Terminal::OutputBuffer outputBuffer;
    static_cast<void>(
        context.expectTrue("output buffer sets LF line ending", outputBuffer.setLineEnding(Terminal::Types::Output::LineEnding::Lf).ok()));
    static_cast<void>(context.expectTrue("output buffer reserve succeeds", outputBuffer.reserve(64).ok()));
    static_cast<void>(context.expectTrue("output buffer appendText succeeds", outputBuffer.appendText("alpha").ok()));
    static_cast<void>(context.expectTrue("output buffer appendLine succeeds", outputBuffer.appendLine(" beta").ok()));
    static_cast<void>(context.expectTrue("output buffer print succeeds", outputBuffer.print("{}", 3).ok()));
    static_cast<void>(context.expectTrue("output buffer println succeeds", outputBuffer.println(" {}", 4).ok()));
    static_cast<void>(context.expectEq("output buffer text", std::string_view{"alpha beta\n3 4\n"}, outputBuffer.text()));

    const std::string beforeFormattingFailure(outputBuffer.text());
    static_cast<void>(context.expectEq(
        "output buffer formatting failure returns status",
        ErrorCode::InvalidArgument,
        outputBuffer.print("{}", TerminalThrowingFormat{}).code));
    static_cast<void>(context.expectEq("output buffer formatting failure rolls back partial record", beforeFormattingFailure, outputBuffer.text()));
    static_cast<void>(context.expectEq(
        "output buffer println failure returns status",
        ErrorCode::InvalidArgument,
        outputBuffer.println("{}", TerminalThrowingFormat{}).code));
    static_cast<void>(
        context.expectEq("output buffer println failure rolls back formatted text and line ending", beforeFormattingFailure, outputBuffer.text()));
    static_cast<void>(context.expectEq(
        "output buffer oversized reserve is checked",
        ErrorCode::SizeLimitExceeded,
        outputBuffer.reserve(std::numeric_limits<std::size_t>::max()).code));
    static_cast<void>(context.expectEq("output buffer reserve failure preserves contents", beforeFormattingFailure, outputBuffer.text()));

    static_cast<void>(context.expectTrue("output buffer flush succeeds", outputBuffer.flushTo().ok()));
    static_cast<void>(context.expectTrue("output buffer clears after flush", outputBuffer.empty()));
    static_cast<void>(context.expectEq(
        "output buffer flush capture",
        std::string{"alpha beta\n3 4\n"},
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));

    static_cast<void>(context.expectTrue("output buffer retry append succeeds", outputBuffer.appendText("retry").ok()));
    Hooks::forceNextTextWriteFailure(ErrorCode::WriteFailed);
    static_cast<void>(context.expectEq("output buffer failure propagates", ErrorCode::WriteFailed, outputBuffer.flushTo().code));
    static_cast<void>(context.expectEq("output buffer failure preserves text", std::string_view{"retry"}, outputBuffer.text()));
    static_cast<void>(
        context.expectTrue("output buffer explicit stream retry succeeds", outputBuffer.flushTo(Terminal::Types::Output::Stream::Stdout).ok()));
    static_cast<void>(context.expectTrue("output buffer explicit stream retry clears text", outputBuffer.empty()));

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    static_cast<void>(context.expectTrue(
        "explicit stream formatted print succeeds",
        Terminal::print(Terminal::Types::Output::Stream::Stdout, "writer {}", 5).ok()));
    static_cast<void>(context.expectEq(
        "explicit stream formatted print capture",
        std::string{"writer 5"},
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    static_cast<void>(context.expectTrue(
        "explicit stream formatted println succeeds",
        Terminal::println(Terminal::Types::Output::Stream::Stdout, printLineOptions, "writer line {}", 6).ok()));
    static_cast<void>(context.expectEq(
        "explicit stream formatted println capture",
        std::string{"writer line 6\n"},
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));

    Hooks::reset();
    setupCapturedOutput(Terminal::Types::Output::Stream::Stdout, redirectedOutputCapabilities());
    setupCapturedOutput(Terminal::Types::Output::Stream::Stderr, redirectedOutputCapabilities());
    static_cast<void>(context.expectTrue("stdout state write succeeds", Terminal::writeText("out").ok()));
    static_cast<void>(context.expectTrue("stderr state write succeeds", Terminal::writeText(Terminal::Types::Output::Stream::Stderr, "err").ok()));
    static_cast<void>(
        context.expectEq("stdout state remains independent", std::string{"out"}, Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));
    static_cast<void>(
        context.expectEq("stderr state remains independent", std::string{"err"}, Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stderr)));

    Hooks::reset();
}
#endif

#if TERMINAL_INTERNAL_TEST_HOOKS
/// @brief Verifies atomic segmented records, raw bytes, output capture, and flush behavior.
void testSegmentedAndByteOutput(TestSupport::Context &context)
{
    Hooks::reset();
    setupCapturedOutput(Terminal::Types::Output::Stream::Stdout);

    Terminal::Types::Style::Request style;
    style.bold = true;
    const std::string byteText = "c";
    const std::array<Terminal::Types::Output::Segment, 3> segments{
        Terminal::textSegment("a"),
        Terminal::styledTextSegment("b", style),
        Terminal::byteSegment(bytesOf(byteText))};

    Terminal::Types::Output::SegmentOptions options;
    options.appendLineEnding = true;
    options.lineEnding = Terminal::Types::Output::LineEnding::Lf;
    static_cast<void>(context.expectTrue(
        "segmented write succeeds",
        Terminal::writeSegments(std::span<const Terminal::Types::Output::Segment>(segments), options).ok()));
    static_cast<void>(context.expectEq(
        "segmented write preserves order",
        std::string{"a\x1b[1mb\x1b[0mc\n"},
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));
    static_cast<void>(context.expectEq(
        "segmented write containing bytes bypasses the text backend lane",
        std::size_t{0},
        Hooks::textWriteCallCount(Terminal::Types::Output::Stream::Stdout)));

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    const std::array<Terminal::Types::Output::Segment, 1> plainSegments{Terminal::textSegment("plain")};
    const std::size_t plainSegmentWritesBefore = Hooks::textWriteCallCount(Terminal::Types::Output::Stream::Stdout);
    Hooks::forceNextOutputCapabilityFailure(ErrorCode::StatFailed);
    static_cast<void>(context.expectTrue(
        "plain segmented write skips capability query",
        Terminal::writeSegments(std::span<const Terminal::Types::Output::Segment>(plainSegments)).ok()));
    static_cast<void>(
        context.expectEq("plain segmented write capture", std::string{"plain"}, Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));
    static_cast<void>(context.expectEq(
        "plain segmented write leaves capability failure pending",
        ErrorCode::StatFailed,
        Terminal::getOutputCapabilities().status.code));

    static_cast<void>(context.expectEq(
        "single plain segment uses one direct backend write",
        plainSegmentWritesBefore + 1,
        Hooks::textWriteCallCount(Terminal::Types::Output::Stream::Stdout)));

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    Terminal::Types::Output::SegmentOptions invalidLineEndingOptions;
    invalidLineEndingOptions.appendLineEnding = true;
    invalidLineEndingOptions.lineEnding = static_cast<Terminal::Types::Output::LineEnding>(-1);
    static_cast<void>(context.expectEq(
        "invalid segmented line ending is rejected before output",
        ErrorCode::InvalidArgument,
        Terminal::writeSegments(std::span<const Terminal::Types::Output::Segment>(plainSegments), invalidLineEndingOptions).code));
    static_cast<void>(context.expectTrue(
        "invalid segmented line ending writes nothing",
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout).empty()));

    Hooks::reset();
    setupCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    const std::array<Terminal::Types::Output::Segment, 1> styledSegments{Terminal::styledTextSegment("fallback", style)};
    Hooks::forceNextOutputCapabilityFailure(ErrorCode::StatFailed);
    Hooks::forceNextOutputPreparationFailure(ErrorCode::NativeFailure);
    static_cast<void>(context.expectTrue(
        "auto styled segment falls back after capability and preparation failure",
        Terminal::writeSegments(std::span<const Terminal::Types::Output::Segment>(styledSegments)).ok()));
    static_cast<void>(context.expectEq(
        "auto styled segment fallback is plain",
        std::string{"fallback"},
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    Terminal::Types::Output::SegmentOptions requiredOptions;
    requiredOptions.styleMode = Terminal::Types::Style::Mode::Required;
    Hooks::forceNextOutputCapabilityFailure(ErrorCode::StatFailed);
    Hooks::forceNextOutputPreparationFailure(ErrorCode::NativeFailure);
    static_cast<void>(context.expectEq(
        "required styled segment propagates preparation failure",
        ErrorCode::NativeFailure,
        Terminal::writeSegments(std::span<const Terminal::Types::Output::Segment>(styledSegments), requiredOptions).code));
    static_cast<void>(context.expectTrue(
        "required styled segment failure writes nothing",
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout).empty()));

    Hooks::reset();
    setupCapturedOutput(Terminal::Types::Output::Stream::Stdout, unpreparedTerminalOutputCapabilities());
    const std::array<Terminal::Types::Output::Segment, 2> unsupportedSegments{
        Terminal::textSegment("must-not-write"),
        Terminal::byteSegment(bytesOf(byteText))};
    static_cast<void>(context.expectEq(
        "unsupported byte segment rejects full batch",
        ErrorCode::Unsupported,
        Terminal::writeSegments(std::span<const Terminal::Types::Output::Segment>(unsupportedSegments)).code));
    static_cast<void>(
        context.expectTrue("unsupported segment batch emits nothing", Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout).empty()));
    static_cast<void>(context.expectEq(
        "unsupported segment batch makes no write",
        std::size_t{0},
        Hooks::textWriteCallCount(Terminal::Types::Output::Stream::Stdout)));

    Hooks::reset();
    setupCapturedOutput(Terminal::Types::Output::Stream::Stdout, redirectedOutputCapabilities());
    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    const std::string raw = "raw";
    const IO::Types::WriteResult rawWrite = Terminal::writeBytes(bytesOf(raw));
    static_cast<void>(context.expectTrue("byte write succeeds", rawWrite.status.ok()));
    static_cast<void>(context.expectEq("byte write count", raw.size(), rawWrite.bytesWritten));
    static_cast<void>(context.expectEq("byte write capture bytes", copyBytes(raw), Hooks::capturedOutput(Terminal::Types::Output::Stream::Stdout)));

    Hooks::forceNextByteWriteFailure(ErrorCode::BrokenPipe);
    const IO::Types::WriteResult failedWrite = Terminal::writeBytes(bytesOf(raw));
    static_cast<void>(context.expectEq("forced byte write failure", ErrorCode::BrokenPipe, failedWrite.status.code));
    static_cast<void>(context.expectEq("forced byte write reports zero", std::size_t{0}, failedWrite.bytesWritten));

    Hooks::reset();
}
#endif
