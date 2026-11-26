/// @file text_output_test.inl
/// @brief Terminal text-output tests.

#if TERMINAL_INTERNAL_TEST_HOOKS

/// ------------------------------------------------------------
/// Text and segmented output
/// ------------------------------------------------------------

/// @brief Verifies plain, formatted, buffered, styled, and line-oriented text output.
void testTextAndStyleOutput(TestSupport::Context &context)
{
    Hooks::reset();
    setupCapturedOutput(Terminal::Types::Output::Stream::Stdout);

    // Validate plain text options and output-buffer defaults.
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
        Terminal::writeLine("must-not-write", invalidLineEndingOptions).status().code()));
    static_cast<void>(
        context.expectTrue("invalid line ending writes nothing", Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout).empty()));

    Terminal::Types::Output::TextOptions invalidFlushOptions;
    invalidFlushOptions.flushMode = static_cast<IO::Types::FlushMode>(-1);
    static_cast<void>(context.expectEq(
        "invalid text flush mode is rejected before output",
        ErrorCode::InvalidArgument,
        Terminal::writeText("must-not-write", invalidFlushOptions).status().code()));
    static_cast<void>(
        context.expectTrue("invalid text flush mode writes nothing", Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout).empty()));
    static_cast<void>(context.expectEq(
        "invalid direct flush mode is rejected",
        ErrorCode::InvalidArgument,
        Terminal::flush(static_cast<IO::Types::FlushMode>(-1)).status().code()));

    Terminal::OutputBuffer lineEndingBuffer;
    static_cast<void>(
        context.expectEq("output buffer defaults to native line ending", Terminal::Types::Output::LineEnding::Native, lineEndingBuffer.lineEnding()));
    static_cast<void>(context.expectEq(
        "output buffer rejects invalid line ending without throwing",
        ErrorCode::InvalidArgument,
        lineEndingBuffer.setLineEnding(static_cast<Terminal::Types::Output::LineEnding>(-1)).status().code()));
    static_cast<void>(context.expectEq(
        "invalid line ending preserves previous setting",
        Terminal::Types::Output::LineEnding::Native,
        lineEndingBuffer.lineEnding()));

    // Apply styles or fall back when a capability is unavailable.
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

    auto basicColorCapabilities = terminalOutputCapabilities();
    basicColorCapabilities.style.rgbColor = false;
    setupCapturedOutput(Terminal::Types::Output::Stream::Stdout, basicColorCapabilities);
    Terminal::Types::Style::Request rgbStyle;
    rgbStyle.foreground = Terminal::rgbColor(200, 40, 180);
    Terminal::Types::Output::TextOptions rgbOptions;
    rgbOptions.style = rgbStyle;
    static_cast<void>(context.expectTrue("RGB style falls back to a basic color", Terminal::writeText("fallback", rgbOptions).ok()));
    static_cast<void>(context.expectEq(
        "RGB style uses the nearest basic color",
        std::string{"\x1b[35mfallback\x1b[0m"},
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    const std::array<Terminal::Types::Output::Segment, 1> rgbSegments{Terminal::styledTextSegment("segment", rgbStyle)};
    static_cast<void>(context.expectTrue(
        "RGB segment falls back to a basic color",
        Terminal::writeSegments(std::span<const Terminal::Types::Output::Segment>(rgbSegments)).ok()));
    static_cast<void>(context.expectEq(
        "RGB segment uses the nearest basic color",
        std::string{"\x1b[35msegment\x1b[0m"},
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    rgbOptions.styleMode = Terminal::Types::Style::Mode::Required;
    Hooks::setPreparedOutputCapabilitiesOverride(Terminal::Types::Output::Stream::Stdout, basicColorCapabilities);
    static_cast<void>(context.expectEq(
        "required RGB style fails when exact RGB is unavailable",
        ErrorCode::Unsupported,
        Terminal::writeText("must-not-write", rgbOptions).status().code()));
    static_cast<void>(context.expectTrue(
        "required unsupported RGB style writes nothing",
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout).empty()));

    // Check lazy preparation and automatic versus required style handling.
    Hooks::reset();
    setupCapturedOutput(Terminal::Types::Output::Stream::Stdout, redirectedOutputCapabilities());
    static_cast<void>(context.expectTrue("redirected styled output falls back to plain text", Terminal::writeText("redirected", styledOptions).ok()));
    static_cast<void>(context.expectEq(
        "redirected styled output capture is plain",
        std::string{"redirected"},
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));

    Hooks::reset();
    setupCapturedOutput(Terminal::Types::Output::Stream::Stdout, unpreparedTerminalOutputCapabilities());
    Hooks::setPreparedOutputCapabilitiesOverride(Terminal::Types::Output::Stream::Stdout, terminalOutputCapabilities());
    static_cast<void>(context.expectTrue("lazy preparation enables styled output", Terminal::writeText("lazy", styledOptions).ok()));
    static_cast<void>(context.expectEq(
        "lazy styled output capture",
        std::string{"\x1b[1;91mlazy\x1b[0m"},
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    Hooks::setOutputCapabilitiesOverride(Terminal::Types::Output::Stream::Stdout, unpreparedTerminalOutputCapabilities());
    Hooks::forceNextOutputPreparationFailure(IOErrorCode::NativeFailure);
    static_cast<void>(context.expectTrue("auto style fallback succeeds", Terminal::writeText("plain", styledOptions).ok()));
    static_cast<void>(
        context.expectEq("auto style fallback is plain", std::string{"plain"}, Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    styledOptions.styleMode = Terminal::Types::Style::Mode::Required;
    Hooks::forceNextOutputPreparationFailure(IOErrorCode::NativeFailure);
    static_cast<void>(
        context.expectEq("forced preparation failure propagates", ErrorCode::NativeFailure, Terminal::writeText("fail", styledOptions).status().code()));
    static_cast<void>(
        context.expectTrue("forced unsupported style writes nothing", Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout).empty()));

    // Check formatted writes and buffer rollback on failure.
    setupCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    static_cast<void>(context.expectTrue("resetStyle succeeds", Terminal::resetStyle().ok()));
    static_cast<void>(
        context.expectEq("resetStyle emits reset", std::string{"\x1b[0m"}, Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));

    Hooks::forceNextTextWriteFailure(IOErrorCode::PermissionDenied);
    static_cast<void>(context.expectEq("forced text write failure", ErrorCode::PermissionDenied, Terminal::writeText("blocked").status().code()));

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    static_cast<void>(context.expectTrue("formatted print succeeds", Terminal::print("value {}", 42).ok()));
    static_cast<void>(
        context.expectEq("formatted print capture", std::string{"value 42"}, Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    static_cast<void>(
        context.expectEq("formatted print failure returns status", ErrorCode::InvalidArgument, Terminal::print("{}", TerminalThrowingFormat{}).status().code()));
    static_cast<void>(
        context.expectTrue("formatted print failure writes nothing", Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout).empty()));

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    Terminal::Types::Output::LineOptions printLineOptions;
    printLineOptions.lineEnding = Terminal::Types::Output::LineEnding::Lf;
    static_cast<void>(context.expectTrue("formatted println succeeds", Terminal::println(printLineOptions, "line {}", 7).ok()));
    static_cast<void>(
        context.expectEq("formatted println capture", std::string{"line 7\n"}, Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    static_cast<void>(context.expectEq(
        "formatted println failure returns status",
        ErrorCode::InvalidArgument,
        Terminal::println(printLineOptions, "{}", TerminalThrowingFormat{}).status().code()));
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
        outputBuffer.print("{}", TerminalThrowingFormat{}).status().code()));
    static_cast<void>(context.expectEq("output buffer formatting failure rolls back partial record", beforeFormattingFailure, outputBuffer.text()));
    static_cast<void>(context.expectEq(
        "output buffer println failure returns status",
        ErrorCode::InvalidArgument,
        outputBuffer.println("{}", TerminalThrowingFormat{}).status().code()));
    static_cast<void>(
        context.expectEq("output buffer println failure rolls back formatted text and line ending", beforeFormattingFailure, outputBuffer.text()));
    static_cast<void>(context.expectEq(
        "output buffer oversized reserve is checked",
        ErrorCode::SizeLimitExceeded,
        outputBuffer.reserve(std::numeric_limits<std::size_t>::max()).status().code()));
    static_cast<void>(context.expectEq("output buffer reserve failure preserves contents", beforeFormattingFailure, outputBuffer.text()));

    // Flush buffered output and retry failed writes.
    static_cast<void>(context.expectTrue("output buffer flush succeeds", outputBuffer.flushTo().ok()));
    static_cast<void>(context.expectTrue("output buffer clears after flush", outputBuffer.empty()));
    static_cast<void>(context.expectEq(
        "output buffer flush capture",
        std::string{"alpha beta\n3 4\n"},
        Hooks::capturedOutputText(Terminal::Types::Output::Stream::Stdout)));

    static_cast<void>(context.expectTrue("output buffer retry append succeeds", outputBuffer.appendText("retry").ok()));
    Hooks::forceNextTextWriteFailure(IOErrorCode::WriteFailed);
    static_cast<void>(context.expectEq("output buffer failure propagates", ErrorCode::WriteFailed, outputBuffer.flushTo().status().code()));
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

    // Keep stdout and stderr state independent.
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
