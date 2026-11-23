/// @file segment_output_test.inl
/// @brief Terminal segmented and byte-output tests.

#if TERMINAL_INTERNAL_TEST_HOOKS

/// ------------------------------------------------------------
/// Segmented and byte output
/// ------------------------------------------------------------

/// @brief Verifies atomic segmented records, raw bytes, output capture, and flush behavior.
void testSegmentedAndByteOutput(TestSupport::Context &context)
{
    Hooks::reset();
    setupCapturedOutput(Terminal::Types::Output::Stream::Stdout);

    // Preserve segment order and line-ending policy.
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

    Hooks::clearCapturedOutput(Terminal::Types::Output::Stream::Stdout);
    const std::array<Terminal::Types::Output::Segment, 1> plainSegments{Terminal::textSegment("plain")};
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

    // Reject unsupported and invalid segment batches before output.
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

    // Preserve raw bytes and report their write status.
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
