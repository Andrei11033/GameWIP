/// @file input_test.inl
/// @brief Focused Terminal input tests.

#if TERMINAL_INTERNAL_TEST_HOOKS
/// @brief Verifies byte, text, and line reads across UTF-8, timeout, truncation, and EOF paths.
void testInputReads(TestSupport::Context &context)
{
    Hooks::reset();

    // Read bytes and distinguish end-of-stream.
    setupInput("abcd");
    std::array<std::byte, 4> buffer{};
    Terminal::Types::Input::ByteOptions byteOptions;
    byteOptions.timeout = Terminal::kNoWait;
    byteOptions.allowPartial = false;
    const Terminal::Types::Input::ByteResult bytes = Terminal::readBytes(std::span<std::byte>(buffer), byteOptions);
    static_cast<void>(context.expectTrue("byte read status", bytes.ok()));
    if(bytes.hasValue())
    {
        static_cast<void>(context.expectEq("byte read outcome", Terminal::Types::Input::ReadOutcome::Completed, bytes.value().outcome));
        static_cast<void>(context.expectEq("byte read count", buffer.size(), bytes.value().bytesRead));
    }
    static_cast<void>(context.expectEq("byte read contents", copyBytes("abcd"), std::vector<std::byte>(buffer.begin(), buffer.end())));

    const Terminal::Types::Input::ByteResult eof = Terminal::readBytes(std::span<std::byte>(buffer), byteOptions);
    static_cast<void>(context.expectTrue("byte EOF status", eof.ok()));
    if(eof.hasValue())
    {
        static_cast<void>(context.expectEq("byte EOF outcome", Terminal::Types::Input::ReadOutcome::EndOfStream, eof.value().outcome));
    }

    // Read lines while preserving or normalizing their endings.
    setupInput("one\r\ntwo\rc");
    Terminal::Types::Input::LineOptions keepOptions;
    keepOptions.lineEndingMode = Terminal::Types::Input::LineEndingMode::Keep;
    const Terminal::Types::Input::LineResult firstLine = Terminal::readLine(keepOptions);
    static_cast<void>(context.expectTrue("first line status", firstLine.ok()));
    if(firstLine.hasValue())
    {
        static_cast<void>(context.expectEq("first line text", std::string{"one\r\n"}, firstLine.value().line));
        static_cast<void>(
            context.expectEq("first line ending", Terminal::Types::Input::ConsumedLineEnding::CrLf, firstLine.value().consumedLineEnding));
    }

    Terminal::Types::Input::LineOptions normalizeOptions;
    normalizeOptions.lineEndingMode = Terminal::Types::Input::LineEndingMode::NormalizeToLf;
    const Terminal::Types::Input::LineResult secondLine = Terminal::readLine(normalizeOptions);
    static_cast<void>(context.expectTrue("second line status", secondLine.ok()));
    if(secondLine.hasValue())
    {
        static_cast<void>(context.expectEq("second line text", std::string{"two\n"}, secondLine.value().line));
        static_cast<void>(
            context.expectEq("second line ending", Terminal::Types::Input::ConsumedLineEnding::Cr, secondLine.value().consumedLineEnding));
    }

    const Terminal::Types::Input::LineResult finalLine = Terminal::readLine();
    static_cast<void>(context.expectTrue("final line status", finalLine.ok()));
    if(finalLine.hasValue())
    {
        static_cast<void>(context.expectEq("final line text", std::string{"c"}, finalLine.value().line));
        static_cast<void>(context.expectEq("final line outcome", Terminal::Types::Input::ReadOutcome::EndOfStream, finalLine.value().outcome));
    }

    // Reject invalid limits without consuming the pending input.
    setupInput("kept\n");
    Terminal::Types::Input::LineOptions invalidLineModeOptions;
    invalidLineModeOptions.lineEndingMode = static_cast<Terminal::Types::Input::LineEndingMode>(-1);
    static_cast<void>(context.expectEq(
        "invalid line read mode is rejected",
        ErrorCode::InvalidArgument,
        Terminal::readLine(invalidLineModeOptions).status().code()));
    const auto retainedLine = Terminal::readLine();
    static_cast<void>(context.expectTrue("invalid line read mode preserves readable input", retainedLine.ok()));
    if(retainedLine.hasValue())
    {
        static_cast<void>(context.expectEq("invalid line read mode consumes no input", std::string{"kept"}, retainedLine.value().line));
    }

    setupInput("line\n");
    Terminal::Types::Input::LineOptions zeroLineLimitOptions;
    zeroLineLimitOptions.maxReturnedBytes = 0;
    static_cast<void>(
        context.expectEq("zero line limit is rejected", ErrorCode::InvalidArgument, Terminal::readLine(zeroLineLimitOptions).status().code()));
    const auto retainedZeroLimitLine = Terminal::readLine();
    static_cast<void>(context.expectTrue("zero line limit preserves readable input", retainedZeroLimitLine.ok()));
    if(retainedZeroLimitLine.hasValue())
    {
        static_cast<void>(context.expectEq("zero line limit consumes no input", std::string{"line"}, retainedZeroLimitLine.value().line));
    }

    setupInput("text");
    Terminal::Types::Input::TextOptions zeroTextLimitOptions;
    zeroTextLimitOptions.maxReturnedBytes = 0;
    static_cast<void>(
        context.expectEq("zero text limit is rejected", ErrorCode::InvalidArgument, Terminal::readText(zeroTextLimitOptions).status().code()));
    const auto retainedZeroLimitText = Terminal::readText();
    static_cast<void>(context.expectTrue("zero text limit preserves readable input", retainedZeroLimitText.ok()));
    if(retainedZeroLimitText.hasValue())
    {
        static_cast<void>(context.expectEq("zero text limit consumes no input", std::string{"text"}, retainedZeroLimitText.value().text));
    }

    // Consume complete line endings when returned text is truncated.
    setupInput("abc\nnext\n");
    Terminal::Types::Input::LineOptions keepLimitedLfOptions;
    keepLimitedLfOptions.maxReturnedBytes = 3;
    keepLimitedLfOptions.lineEndingMode = Terminal::Types::Input::LineEndingMode::Keep;
    const Terminal::Types::Input::LineResult keepLimitedLf = Terminal::readLine(keepLimitedLfOptions);
    static_cast<void>(context.expectTrue("limited LF keep status", keepLimitedLf.ok()));
    if(keepLimitedLf.hasValue())
    {
        static_cast<void>(context.expectEq("limited LF keep text", std::string{"abc"}, keepLimitedLf.value().line));
        static_cast<void>(context.expectEq("limited LF keep size", std::size_t{3}, keepLimitedLf.value().line.size()));
        static_cast<void>(context.expectTrue("limited LF keep reports truncation", keepLimitedLf.value().wasTruncated));
        static_cast<void>(context.expectEq(
            "limited LF keep ending consumed",
            Terminal::Types::Input::ConsumedLineEnding::Lf,
            keepLimitedLf.value().consumedLineEnding));
    }
    const Terminal::Types::Input::LineResult afterLimitedLf = Terminal::readLine();
    static_cast<void>(context.expectTrue("after limited LF status", afterLimitedLf.ok()));
    if(afterLimitedLf.hasValue())
    {
        static_cast<void>(context.expectEq("after limited LF text", std::string{"next"}, afterLimitedLf.value().line));
    }

    setupInput("abc\r\nnext\n");
    Terminal::Types::Input::LineOptions keepLimitedCrLfOptions;
    keepLimitedCrLfOptions.maxReturnedBytes = 4;
    keepLimitedCrLfOptions.lineEndingMode = Terminal::Types::Input::LineEndingMode::Keep;
    const Terminal::Types::Input::LineResult keepLimitedCrLf = Terminal::readLine(keepLimitedCrLfOptions);
    static_cast<void>(context.expectTrue("limited CRLF keep status", keepLimitedCrLf.ok()));
    if(keepLimitedCrLf.hasValue())
    {
        static_cast<void>(context.expectEq("limited CRLF keep text avoids partial ending", std::string{"abc"}, keepLimitedCrLf.value().line));
        static_cast<void>(context.expectTrue("limited CRLF keep reports truncation", keepLimitedCrLf.value().wasTruncated));
        static_cast<void>(context.expectEq(
            "limited CRLF keep ending consumed",
            Terminal::Types::Input::ConsumedLineEnding::CrLf,
            keepLimitedCrLf.value().consumedLineEnding));
    }
    const Terminal::Types::Input::LineResult afterLimitedCrLf = Terminal::readLine();
    static_cast<void>(context.expectTrue("after limited CRLF status", afterLimitedCrLf.ok()));
    if(afterLimitedCrLf.hasValue())
    {
        static_cast<void>(context.expectEq("after limited CRLF text", std::string{"next"}, afterLimitedCrLf.value().line));
    }

    setupInput("abcd\r\nnext\n");
    Terminal::Types::Input::LineOptions normalizeLimitedOptions;
    normalizeLimitedOptions.maxReturnedBytes = 4;
    normalizeLimitedOptions.lineEndingMode = Terminal::Types::Input::LineEndingMode::NormalizeToLf;
    const Terminal::Types::Input::LineResult normalizeLimited = Terminal::readLine(normalizeLimitedOptions);
    static_cast<void>(context.expectTrue("limited normalize status", normalizeLimited.ok()));
    if(normalizeLimited.hasValue())
    {
        static_cast<void>(context.expectEq("limited normalize text", std::string{"abcd"}, normalizeLimited.value().line));
        static_cast<void>(context.expectEq("limited normalize size", std::size_t{4}, normalizeLimited.value().line.size()));
        static_cast<void>(context.expectTrue("limited normalize reports truncation", normalizeLimited.value().wasTruncated));
        static_cast<void>(context.expectEq(
            "limited normalize ending consumed",
            Terminal::Types::Input::ConsumedLineEnding::CrLf,
            normalizeLimited.value().consumedLineEnding));
    }
    const Terminal::Types::Input::LineResult afterLimitedNormalize = Terminal::readLine();
    static_cast<void>(context.expectTrue("after limited normalize status", afterLimitedNormalize.ok()));
    if(afterLimitedNormalize.hasValue())
    {
        static_cast<void>(context.expectEq("after limited normalize text", std::string{"next"}, afterLimitedNormalize.value().line));
    }

    std::string longCrLfLine(4095, 'x');
    longCrLfLine.append("\r\nnext\n");
    setupInput(longCrLfLine);
    const Terminal::Types::Input::LineResult longLine = Terminal::readLine();
    static_cast<void>(context.expectTrue("long line status", longLine.ok()));
    if(longLine.hasValue())
    {
        static_cast<void>(context.expectEq("long line size", std::size_t{4095}, longLine.value().line.size()));
        static_cast<void>(context.expectEq(
            "long line detects CRLF across read chunks",
            Terminal::Types::Input::ConsumedLineEnding::CrLf,
            longLine.value().consumedLineEnding));
    }
    const auto afterLongLine = Terminal::readLine();
    static_cast<void>(context.expectTrue("line after chunk-boundary CRLF status", afterLongLine.ok()));
    if(afterLongLine.hasValue())
    {
        static_cast<void>(context.expectEq("line after chunk-boundary CRLF", std::string{"next"}, afterLongLine.value().line));
    }

    // Preserve UTF-8 boundaries and report malformed text.
    setupInput("\xc3\xa9z");
    Terminal::Types::Input::TextOptions textOptions;
    textOptions.maxReturnedBytes = 2;
    const Terminal::Types::Input::TextResult utf8First = Terminal::readText(textOptions);
    static_cast<void>(context.expectTrue("UTF-8 text status", utf8First.ok()));
    if(utf8First.hasValue())
    {
        static_cast<void>(context.expectEq("UTF-8 text preserves boundary", std::string{"\xc3\xa9"}, utf8First.value().text));
        static_cast<void>(context.expectTrue("UTF-8 text reports truncation", utf8First.value().wasTruncated));
    }

    textOptions.maxReturnedBytes = 8;
    const Terminal::Types::Input::TextResult utf8Second = Terminal::readText(textOptions);
    static_cast<void>(context.expectTrue("pending UTF-8 text status", utf8Second.ok()));
    if(utf8Second.hasValue())
    {
        static_cast<void>(context.expectEq("pending UTF-8 text", std::string{"z"}, utf8Second.value().text));
    }

    setupInput("\xc3(");
    const Terminal::Types::Input::TextResult invalidText = Terminal::readText(textOptions);
    static_cast<void>(context.expectEq("invalid UTF-8 text fails", ErrorCode::EncodingFailed, invalidText.status().code()));

    // Distinguish would-block, timeout, and native read failure outcomes.
    setupInput("", false);
    const Terminal::Types::Input::ByteResult wouldBlock = Terminal::readBytes(std::span<std::byte>(buffer), byteOptions);
    static_cast<void>(context.expectTrue("would-block status", wouldBlock.ok()));
    if(wouldBlock.hasValue())
    {
        static_cast<void>(context.expectEq("would-block outcome", Terminal::Types::Input::ReadOutcome::WouldBlock, wouldBlock.value().outcome));
    }

    Terminal::Types::Input::TextOptions timeoutOptions;
    timeoutOptions.timeout = std::chrono::milliseconds{1};
    const Terminal::Types::Input::TextResult timedOut = Terminal::readText(timeoutOptions);
    static_cast<void>(context.expectTrue("timed-out status", timedOut.ok()));
    if(timedOut.hasValue())
    {
        static_cast<void>(context.expectEq("timed-out outcome", Terminal::Types::Input::ReadOutcome::TimedOut, timedOut.value().outcome));
    }

    Hooks::forceNextReadFailure(IOErrorCode::PermissionDenied);
    const Terminal::Types::Input::TextResult failedRead = Terminal::readText(textOptions);
    static_cast<void>(context.expectEq("forced read failure", ErrorCode::PermissionDenied, failedRead.status().code()));

    Hooks::reset();
}
#endif
