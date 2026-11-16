/// @file input_test.inl
/// @brief Focused Terminal input tests.

#if TERMINAL_INTERNAL_TEST_HOOKS
/// @brief Verifies byte, text, and line reads across UTF-8, timeout, truncation, and EOF paths.
void testInputReads(TestSupport::Context &context)
{
    Hooks::reset();

    setupInput("abcd");
    std::array<std::byte, 4> buffer{};
    Terminal::Types::Input::ByteOptions byteOptions;
    byteOptions.timeout = Terminal::kNoWait;
    byteOptions.allowPartial = false;
    const Terminal::Types::Input::ByteResult bytes = Terminal::readBytes(std::span<std::byte>(buffer), byteOptions);
    static_cast<void>(context.expectTrue("byte read status", bytes.status.ok()));
    static_cast<void>(context.expectEq("byte read outcome", Terminal::Types::Input::ReadOutcome::Completed, bytes.outcome));
    static_cast<void>(context.expectEq("byte read count", buffer.size(), bytes.bytesRead));
    static_cast<void>(context.expectEq("byte read contents", copyBytes("abcd"), std::vector<std::byte>(buffer.begin(), buffer.end())));

    const Terminal::Types::Input::ByteResult eof = Terminal::readBytes(std::span<std::byte>(buffer), byteOptions);
    static_cast<void>(context.expectTrue("byte EOF status", eof.status.ok()));
    static_cast<void>(context.expectEq("byte EOF outcome", Terminal::Types::Input::ReadOutcome::EndOfStream, eof.outcome));

    setupInput("one\r\ntwo\rc");
    Terminal::Types::Input::LineOptions keepOptions;
    keepOptions.lineEndingMode = Terminal::Types::Input::LineEndingMode::Keep;
    const Terminal::Types::Input::LineResult firstLine = Terminal::readLine(keepOptions);
    static_cast<void>(context.expectTrue("first line status", firstLine.status.ok()));
    static_cast<void>(context.expectEq("first line text", std::string{"one\r\n"}, firstLine.line));
    static_cast<void>(context.expectEq("first line ending", Terminal::Types::Input::ConsumedLineEnding::CrLf, firstLine.consumedLineEnding));

    Terminal::Types::Input::LineOptions normalizeOptions;
    normalizeOptions.lineEndingMode = Terminal::Types::Input::LineEndingMode::NormalizeToLf;
    const Terminal::Types::Input::LineResult secondLine = Terminal::readLine(normalizeOptions);
    static_cast<void>(context.expectTrue("second line status", secondLine.status.ok()));
    static_cast<void>(context.expectEq("second line text", std::string{"two\n"}, secondLine.line));
    static_cast<void>(context.expectEq("second line ending", Terminal::Types::Input::ConsumedLineEnding::Cr, secondLine.consumedLineEnding));

    const Terminal::Types::Input::LineResult finalLine = Terminal::readLine();
    static_cast<void>(context.expectTrue("final line status", finalLine.status.ok()));
    static_cast<void>(context.expectEq("final line text", std::string{"c"}, finalLine.line));
    static_cast<void>(context.expectEq("final line outcome", Terminal::Types::Input::ReadOutcome::EndOfStream, finalLine.outcome));

    setupInput("kept\n");
    Terminal::Types::Input::LineOptions invalidLineModeOptions;
    invalidLineModeOptions.lineEndingMode = static_cast<Terminal::Types::Input::LineEndingMode>(-1);
    static_cast<void>(
        context.expectEq("invalid line read mode is rejected", ErrorCode::InvalidArgument, Terminal::readLine(invalidLineModeOptions).status.code));
    static_cast<void>(context.expectEq("invalid line read mode consumes no input", std::string{"kept"}, Terminal::readLine().line));

    setupInput("line\n");
    Terminal::Types::Input::LineOptions zeroLineLimitOptions;
    zeroLineLimitOptions.maxReturnedBytes = 0;
    static_cast<void>(
        context.expectEq("zero line limit is rejected", ErrorCode::InvalidArgument, Terminal::readLine(zeroLineLimitOptions).status.code));
    static_cast<void>(context.expectEq("zero line limit consumes no input", std::string{"line"}, Terminal::readLine().line));

    setupInput("text");
    Terminal::Types::Input::TextOptions zeroTextLimitOptions;
    zeroTextLimitOptions.maxReturnedBytes = 0;
    static_cast<void>(
        context.expectEq("zero text limit is rejected", ErrorCode::InvalidArgument, Terminal::readText(zeroTextLimitOptions).status.code));
    static_cast<void>(context.expectEq("zero text limit consumes no input", std::string{"text"}, Terminal::readText().text));

    setupInput("abc\nnext\n");
    Terminal::Types::Input::LineOptions keepLimitedLfOptions;
    keepLimitedLfOptions.maxReturnedBytes = 3;
    keepLimitedLfOptions.lineEndingMode = Terminal::Types::Input::LineEndingMode::Keep;
    const Terminal::Types::Input::LineResult keepLimitedLf = Terminal::readLine(keepLimitedLfOptions);
    static_cast<void>(context.expectTrue("limited LF keep status", keepLimitedLf.status.ok()));
    static_cast<void>(context.expectEq("limited LF keep text", std::string{"abc"}, keepLimitedLf.line));
    static_cast<void>(context.expectEq("limited LF keep size", std::size_t{3}, keepLimitedLf.line.size()));
    static_cast<void>(context.expectTrue("limited LF keep reports truncation", keepLimitedLf.wasTruncated));
    static_cast<void>(
        context.expectEq("limited LF keep ending consumed", Terminal::Types::Input::ConsumedLineEnding::Lf, keepLimitedLf.consumedLineEnding));
    const Terminal::Types::Input::LineResult afterLimitedLf = Terminal::readLine();
    static_cast<void>(context.expectTrue("after limited LF status", afterLimitedLf.status.ok()));
    static_cast<void>(context.expectEq("after limited LF text", std::string{"next"}, afterLimitedLf.line));

    setupInput("abc\r\nnext\n");
    Terminal::Types::Input::LineOptions keepLimitedCrLfOptions;
    keepLimitedCrLfOptions.maxReturnedBytes = 4;
    keepLimitedCrLfOptions.lineEndingMode = Terminal::Types::Input::LineEndingMode::Keep;
    const Terminal::Types::Input::LineResult keepLimitedCrLf = Terminal::readLine(keepLimitedCrLfOptions);
    static_cast<void>(context.expectTrue("limited CRLF keep status", keepLimitedCrLf.status.ok()));
    static_cast<void>(context.expectEq("limited CRLF keep text avoids partial ending", std::string{"abc"}, keepLimitedCrLf.line));
    static_cast<void>(context.expectTrue("limited CRLF keep reports truncation", keepLimitedCrLf.wasTruncated));
    static_cast<void>(
        context.expectEq("limited CRLF keep ending consumed", Terminal::Types::Input::ConsumedLineEnding::CrLf, keepLimitedCrLf.consumedLineEnding));
    const Terminal::Types::Input::LineResult afterLimitedCrLf = Terminal::readLine();
    static_cast<void>(context.expectTrue("after limited CRLF status", afterLimitedCrLf.status.ok()));
    static_cast<void>(context.expectEq("after limited CRLF text", std::string{"next"}, afterLimitedCrLf.line));

    setupInput("abcd\r\nnext\n");
    Terminal::Types::Input::LineOptions normalizeLimitedOptions;
    normalizeLimitedOptions.maxReturnedBytes = 4;
    normalizeLimitedOptions.lineEndingMode = Terminal::Types::Input::LineEndingMode::NormalizeToLf;
    const Terminal::Types::Input::LineResult normalizeLimited = Terminal::readLine(normalizeLimitedOptions);
    static_cast<void>(context.expectTrue("limited normalize status", normalizeLimited.status.ok()));
    static_cast<void>(context.expectEq("limited normalize text", std::string{"abcd"}, normalizeLimited.line));
    static_cast<void>(context.expectEq("limited normalize size", std::size_t{4}, normalizeLimited.line.size()));
    static_cast<void>(context.expectTrue("limited normalize reports truncation", normalizeLimited.wasTruncated));
    static_cast<void>(
        context.expectEq("limited normalize ending consumed", Terminal::Types::Input::ConsumedLineEnding::CrLf, normalizeLimited.consumedLineEnding));
    const Terminal::Types::Input::LineResult afterLimitedNormalize = Terminal::readLine();
    static_cast<void>(context.expectTrue("after limited normalize status", afterLimitedNormalize.status.ok()));
    static_cast<void>(context.expectEq("after limited normalize text", std::string{"next"}, afterLimitedNormalize.line));

    std::string longCrLfLine(4095, 'x');
    longCrLfLine.append("\r\nnext\n");
    setupInput(longCrLfLine);
    const Terminal::Types::Input::LineResult longLine = Terminal::readLine();
    static_cast<void>(context.expectTrue("long line status", longLine.status.ok()));
    static_cast<void>(context.expectEq("long line size", std::size_t{4095}, longLine.line.size()));
    static_cast<void>(
        context.expectEq("long line detects CRLF across read chunks", Terminal::Types::Input::ConsumedLineEnding::CrLf, longLine.consumedLineEnding));
    static_cast<void>(context.expectEq("line after chunk-boundary CRLF", std::string{"next"}, Terminal::readLine().line));

    setupInput("\xc3\xa9z");
    Terminal::Types::Input::TextOptions textOptions;
    textOptions.maxReturnedBytes = 2;
    const Terminal::Types::Input::TextResult utf8First = Terminal::readText(textOptions);
    static_cast<void>(context.expectTrue("UTF-8 text status", utf8First.status.ok()));
    static_cast<void>(context.expectEq("UTF-8 text preserves boundary", std::string{"\xc3\xa9"}, utf8First.text));
    static_cast<void>(context.expectTrue("UTF-8 text reports truncation", utf8First.wasTruncated));

    textOptions.maxReturnedBytes = 8;
    const Terminal::Types::Input::TextResult utf8Second = Terminal::readText(textOptions);
    static_cast<void>(context.expectTrue("pending UTF-8 text status", utf8Second.status.ok()));
    static_cast<void>(context.expectEq("pending UTF-8 text", std::string{"z"}, utf8Second.text));

    setupInput("\xc3(");
    const Terminal::Types::Input::TextResult invalidText = Terminal::readText(textOptions);
    static_cast<void>(context.expectEq("invalid UTF-8 text fails", ErrorCode::EncodingFailed, invalidText.status.code));

    setupInput("", false);
    const Terminal::Types::Input::ByteResult wouldBlock = Terminal::readBytes(std::span<std::byte>(buffer), byteOptions);
    static_cast<void>(context.expectTrue("would-block status", wouldBlock.status.ok()));
    static_cast<void>(context.expectEq("would-block outcome", Terminal::Types::Input::ReadOutcome::WouldBlock, wouldBlock.outcome));

    Terminal::Types::Input::TextOptions timeoutOptions;
    timeoutOptions.timeout = std::chrono::milliseconds{1};
    const Terminal::Types::Input::TextResult timedOut = Terminal::readText(timeoutOptions);
    static_cast<void>(context.expectTrue("timed-out status", timedOut.status.ok()));
    static_cast<void>(context.expectEq("timed-out outcome", Terminal::Types::Input::ReadOutcome::TimedOut, timedOut.outcome));

    Hooks::forceNextReadFailure(ErrorCode::PermissionDenied);
    const Terminal::Types::Input::TextResult failedRead = Terminal::readText(textOptions);
    static_cast<void>(context.expectEq("forced read failure", ErrorCode::PermissionDenied, failedRead.status.code));

    Hooks::reset();
}
#endif
