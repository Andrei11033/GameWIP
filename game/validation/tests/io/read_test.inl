/// @file read_test.inl
/// @brief Focused io read correctness suites.

/// @brief Verifies whole-reader byte draining across known-size, unknown-size, limit, and failure paths.
void testReadAllBytes(TestSupport::Context &context)
{
    const std::vector<std::byte> source = makeBytes({0xde, 0xad, 0x00, 0xbe, 0xef});

    IO::MemoryReader reader(spanOf(source));
    const IO::Types::ReadAllBytesResult result = IO::readAllBytes(reader, IO::kNoByteLimit, 2);
    static_cast<void>(context.expectTrue("readAllBytes succeeds", result.status.ok()));
    static_cast<void>(context.expectEq("readAllBytes preserves bytes", source, result.bytes));

    IO::MemoryReader exactLimitReader(spanOf(source));
    const IO::Types::ReadAllBytesResult exactLimit = IO::readAllBytes(exactLimitReader, source.size(), 2);
    static_cast<void>(context.expectTrue("readAllBytes accepts exact byte limit", exactLimit.status.ok()));
    static_cast<void>(context.expectEq("readAllBytes exact limit preserves bytes", source, exactLimit.bytes));

    IO::MemoryReader seekedReader(spanOf(source));
    static_cast<void>(seekedReader.seek(2, IO::Types::SeekOrigin::Begin));
    const IO::Types::ReadAllBytesResult seekedResult = IO::readAllBytes(seekedReader, 3, 2);
    static_cast<void>(context.expectTrue("readAllBytes known remaining after seek succeeds", seekedResult.status.ok()));
    static_cast<void>(context.expectEq("readAllBytes known remaining after seek preserves bytes", makeBytes({0x00, 0xbe, 0xef}), seekedResult.bytes));

    IO::MemoryReader shortLimitReader(spanOf(source));
    const IO::Types::ReadAllBytesResult shortLimit = IO::readAllBytes(shortLimitReader, source.size() - 1, 2);
    static_cast<void>(context.expectEq("readAllBytes rejects known size beyond limit", ErrorCode::SizeLimitExceeded, shortLimit.status.code));
    static_cast<void>(context.expectTrue("readAllBytes limit failure returns no bytes", shortLimit.bytes.empty()));

    IO::MemoryReader zeroLimitReader(spanOf(source));
    const IO::Types::ReadAllBytesResult zeroLimit = IO::readAllBytes(zeroLimitReader, 0, 2);
    static_cast<void>(
        context.expectEq("readAllBytes zero limit rejects non-empty known stream", ErrorCode::SizeLimitExceeded, zeroLimit.status.code));
    static_cast<void>(context.expectTrue("readAllBytes zero limit returns empty bytes", zeroLimit.bytes.empty()));

    const std::vector<std::byte> emptySource;
    IO::MemoryReader emptyReader(spanOf(emptySource));
    const IO::Types::ReadAllBytesResult emptyResult = IO::readAllBytes(emptyReader, IO::kNoByteLimit, 2);
    static_cast<void>(context.expectTrue("readAllBytes empty known size succeeds", emptyResult.status.ok()));
    static_cast<void>(context.expectTrue("readAllBytes empty known size returns empty bytes", emptyResult.bytes.empty()));

    IO::MemoryReader emptyZeroLimitReader(spanOf(emptySource));
    const IO::Types::ReadAllBytesResult emptyZeroLimit = IO::readAllBytes(emptyZeroLimitReader, 0, 2);
    static_cast<void>(context.expectTrue("readAllBytes zero limit accepts empty known stream", emptyZeroLimit.status.ok()));

    std::array<std::byte, 2> scratch{};
    UnknownSizeChunkReader scratchReader(spanOf(source), 1);
    const IO::Types::ReadAllBytesResult scratchResult = IO::readAllBytes(scratchReader, std::span<std::byte>(scratch));
    static_cast<void>(context.expectTrue("readAllBytes scratch overload succeeds", scratchResult.status.ok()));
    static_cast<void>(context.expectEq("readAllBytes scratch overload preserves bytes", source, scratchResult.bytes));

    UnknownSizeChunkReader exactScratchLimitReader(spanOf(source), 2, false);
    const IO::Types::ReadAllBytesResult exactScratchLimit = IO::readAllBytes(exactScratchLimitReader, std::span<std::byte>(scratch), source.size());
    static_cast<void>(context.expectTrue("readAllBytes accepts exact unknown-size limit after EOF probe", exactScratchLimit.status.ok()));
    static_cast<void>(context.expectEq("readAllBytes exact unknown-size limit preserves bytes", source, exactScratchLimit.bytes));

    ProbeFailureReader probeFailureReader(spanOf(source));
    const IO::Types::ReadAllBytesResult probeFailure = IO::readAllBytes(probeFailureReader, std::span<std::byte>(scratch), source.size());
    static_cast<void>(context.expectEq("readAllBytes propagates limit probe failure", ErrorCode::PermissionDenied, probeFailure.status.code));
    static_cast<void>(context.expectEq("readAllBytes limit probe failure preserves limited bytes", source, probeFailure.bytes));

    UnknownSizeChunkReader scratchLimitReader(spanOf(source), 2);
    const IO::Types::ReadAllBytesResult scratchLimit = IO::readAllBytes(scratchLimitReader, std::span<std::byte>(scratch), 3);
    static_cast<void>(
        context.expectEq("readAllBytes rejects unknown-size stream beyond limit", ErrorCode::SizeLimitExceeded, scratchLimit.status.code));
    static_cast<void>(context.expectEq("readAllBytes scratch overload returns limited bytes", makeBytes({0xde, 0xad, 0x00}), scratchLimit.bytes));

    UnknownSizeChunkReader zeroLimitUnknownReader(spanOf(source), 1);
    const IO::Types::ReadAllBytesResult zeroLimitUnknown = IO::readAllBytes(zeroLimitUnknownReader, std::span<std::byte>(scratch), 0);
    static_cast<void>(
        context.expectEq("readAllBytes zero limit rejects non-empty unknown stream", ErrorCode::SizeLimitExceeded, zeroLimitUnknown.status.code));
    static_cast<void>(context.expectTrue("readAllBytes zero unknown limit returns no bytes", zeroLimitUnknown.bytes.empty()));

    UnknownSizeChunkReader emptyZeroLimitUnknownReader(spanOf(emptySource), 1);
    const IO::Types::ReadAllBytesResult emptyZeroLimitUnknown = IO::readAllBytes(emptyZeroLimitUnknownReader, std::span<std::byte>(scratch), 0);
    static_cast<void>(context.expectTrue("readAllBytes zero limit accepts empty unknown stream", emptyZeroLimitUnknown.status.ok()));

    UnknownSizeChunkReader emptyScratchReader(spanOf(source), 1);
    const IO::Types::ReadAllBytesResult emptyScratch = IO::readAllBytes(emptyScratchReader, std::span<std::byte>{});
    static_cast<void>(context.expectEq("readAllBytes rejects empty scratch", ErrorCode::InvalidArgument, emptyScratch.status.code));

    SizeWithoutPositionReader sizeWithoutPositionReader(spanOf(source), 2);
    const IO::Types::ReadAllBytesResult sizeWithoutPosition = IO::readAllBytes(sizeWithoutPositionReader, std::span<std::byte>(scratch));
    static_cast<void>(context.expectTrue("readAllBytes falls back when size is known but position is not", sizeWithoutPosition.status.ok()));
    static_cast<void>(
        context.expectEq("readAllBytes size-only fallback preserves remaining bytes", makeBytes({0x00, 0xbe, 0xef}), sizeWithoutPosition.bytes));

    ReportedSizeReader shortKnownReader(spanOf(source), source.size() + 2);
    const IO::Types::ReadAllBytesResult shortKnown = IO::readAllBytes(shortKnownReader, IO::kNoByteLimit, 2);
    static_cast<void>(context.expectEq("readAllBytes reports early known-size EOF as PartialRead", ErrorCode::PartialRead, shortKnown.status.code));
    static_cast<void>(context.expectEq("readAllBytes partial read preserves bytes", source, shortKnown.bytes));

    FailingKnownSizeReader failingReader(spanOf(source), 2, ErrorCode::PermissionDenied);
    const IO::Types::ReadAllBytesResult failingResult = IO::readAllBytes(failingReader, IO::kNoByteLimit, 2);
    static_cast<void>(context.expectEq("readAllBytes direct fill propagates failure", ErrorCode::PermissionDenied, failingResult.status.code));
    static_cast<void>(context.expectEq("readAllBytes direct fill keeps bytes before failure", makeBytes({0xde, 0xad, 0x00}), failingResult.bytes));

    SizeQueryFailureReader queryFailureReader;
    const IO::Types::ReadAllBytesResult queryFailure = IO::readAllBytes(queryFailureReader, IO::kNoByteLimit, 2);
    static_cast<void>(context.expectEq("readAllBytes propagates size query failure", ErrorCode::PermissionDenied, queryFailure.status.code));
    static_cast<void>(context.expectFalse("readAllBytes does not read after size query failure", queryFailureReader.readCalled()));

    PositionQueryFailureReader positionQueryFailureReader;
    const IO::Types::ReadAllBytesResult positionQueryFailure = IO::readAllBytes(positionQueryFailureReader, IO::kNoByteLimit, 2);
    static_cast<void>(
        context.expectEq("readAllBytes propagates position query failure", ErrorCode::PermissionDenied, positionQueryFailure.status.code));
    static_cast<void>(context.expectFalse("readAllBytes does not read after position query failure", positionQueryFailureReader.readCalled()));

    ImpossiblePositionReader impossiblePositionReader;
    const IO::Types::ReadAllBytesResult impossiblePosition = IO::readAllBytes(impossiblePositionReader, IO::kNoByteLimit, 2);
    static_cast<void>(context.expectEq("readAllBytes rejects position beyond size", ErrorCode::InvalidArgument, impossiblePosition.status.code));
    static_cast<void>(context.expectFalse("readAllBytes does not read after impossible position", impossiblePositionReader.readCalled()));

    UnsupportedSizeReader unsupportedSizeReader(spanOf(source));
    const IO::Types::ReadAllBytesResult unsupportedSize = IO::readAllBytes(unsupportedSizeReader, IO::kNoByteLimit, 2);
    static_cast<void>(context.expectTrue("readAllBytes falls back when size is Unsupported", unsupportedSize.status.ok()));
    static_cast<void>(context.expectEq("readAllBytes Unsupported fallback preserves bytes", source, unsupportedSize.bytes));

    InvalidReadCountReader invalidCountReader;
    const IO::Types::ReadAllBytesResult invalidCount = IO::readAllBytes(invalidCountReader, std::span<std::byte>(scratch));
    static_cast<void>(context.expectEq("readAllBytes rejects invalid reader byte count", ErrorCode::ReadFailed, invalidCount.status.code));

    UnknownSizeChunkReader zeroProgressReader(spanOf(source), 0);
    const IO::Types::ReadAllBytesResult zeroProgress = IO::readAllBytes(zeroProgressReader, std::span<std::byte>(scratch));
    static_cast<void>(context.expectEq("readAllBytes rejects zero-progress reader", ErrorCode::ReadFailed, zeroProgress.status.code));

    ZeroProgressKnownSizeReader zeroProgressKnownSizeReader;
    const IO::Types::ReadAllBytesResult zeroProgressKnownSize = IO::readAllBytes(zeroProgressKnownSizeReader, IO::kNoByteLimit, 2);
    static_cast<void>(
        context.expectEq("readAllBytes rejects zero-progress known-size reader", ErrorCode::ReadFailed, zeroProgressKnownSize.status.code));

    IO::MemoryReader invalidBufferReader(spanOf(source));
    const IO::Types::ReadAllBytesResult invalidBuffer = IO::readAllBytes(invalidBufferReader, IO::kNoByteLimit, 0);
    static_cast<void>(context.expectEq("readAllBytes rejects zero buffer size", ErrorCode::InvalidArgument, invalidBuffer.status.code));
}

/// @brief Verifies whole-reader text draining and byte-limit behavior.
void testReadAllText(TestSupport::Context &context)
{
    const std::string text("alpha\0beta", 10);

    IO::MemoryReader reader(bytesOf(text));
    const IO::Types::ReadAllTextResult result = IO::readAllText(reader, IO::kNoByteLimit, 3);
    static_cast<void>(context.expectTrue("readAllText succeeds", result.status.ok()));
    static_cast<void>(context.expectEq("readAllText preserves UTF-8 bytes and NUL", text, result.text));

    const std::string unicodeText = makeStringBytes({0x41, 0xc2, 0xa2, 0xe2, 0x82, 0xac, 0xf0, 0x9f, 0x98, 0x80});
    IO::MemoryReader unicodeReader(bytesOf(unicodeText));
    const IO::Types::ReadAllTextResult unicodeResult = IO::readAllText(unicodeReader, IO::kNoByteLimit, 2);
    static_cast<void>(context.expectTrue("readAllText accepts valid multi-byte UTF-8", unicodeResult.status.ok()));
    static_cast<void>(context.expectEq("readAllText preserves valid multi-byte UTF-8", unicodeText, unicodeResult.text));

    std::array<std::byte, 2> unicodeScratch{};
    UnknownSizeChunkReader splitUnicodeReader(bytesOf(unicodeText), 1);
    const IO::Types::ReadAllTextResult splitUnicodeResult = IO::readAllText(splitUnicodeReader, std::span<std::byte>(unicodeScratch));
    static_cast<void>(context.expectTrue("readAllText accepts UTF-8 split across reader chunks", splitUnicodeResult.status.ok()));
    static_cast<void>(context.expectEq("readAllText preserves split UTF-8", unicodeText, splitUnicodeResult.text));

    const std::string malformedText = makeStringBytes({0x6f, 0x6b, 0xff, 0x21});
    IO::MemoryReader malformedReader(bytesOf(malformedText));
    const IO::Types::ReadAllTextResult malformedResult = IO::readAllText(malformedReader);
    static_cast<void>(context.expectEq("readAllText rejects malformed UTF-8", ErrorCode::EncodingFailed, malformedResult.status.code));
    static_cast<void>(context.expectEq("readAllText malformed input preserves valid prefix", std::string{"ok"}, malformedResult.text));

    const std::string incompleteText = makeStringBytes({0x6f, 0x6b, 0xe2, 0x82});
    IO::MemoryReader incompleteReader(bytesOf(incompleteText));
    const IO::Types::ReadAllTextResult incompleteResult = IO::readAllText(incompleteReader);
    static_cast<void>(context.expectEq("readAllText rejects incomplete UTF-8 at EOF", ErrorCode::EncodingFailed, incompleteResult.status.code));
    static_cast<void>(context.expectEq("readAllText incomplete input preserves valid prefix", std::string{"ok"}, incompleteResult.text));

    const std::string incompleteFailureSource = makeStringBytes({0x6f, 0x6b, 0xe2, 0x82, 0xac});
    FailingKnownSizeReader incompleteFailureReader(bytesOf(incompleteFailureSource), 2, ErrorCode::PermissionDenied);
    const IO::Types::ReadAllTextResult incompleteFailure = IO::readAllText(incompleteFailureReader);
    static_cast<void>(
        context.expectEq("readAllText preserves backend failure for incomplete suffix", ErrorCode::PermissionDenied, incompleteFailure.status.code));
    static_cast<void>(context.expectEq("readAllText trims incomplete suffix after backend failure", std::string{"ok"}, incompleteFailure.text));

    const std::string malformedFailureSource = makeStringBytes({0x6f, 0x6b, 0xff, 0x21});
    FailingKnownSizeReader malformedFailureReader(bytesOf(malformedFailureSource), 2, ErrorCode::PermissionDenied);
    const IO::Types::ReadAllTextResult malformedFailure = IO::readAllText(malformedFailureReader);
    static_cast<void>(context.expectEq(
        "readAllText malformed bytes override simultaneous backend failure",
        ErrorCode::EncodingFailed,
        malformedFailure.status.code));
    static_cast<void>(context.expectEq("readAllText malformed failure preserves valid prefix", std::string{"ok"}, malformedFailure.text));

    const std::string incompletePartialSource = makeStringBytes({0x41, 0xe2});
    ReportedSizeReader incompletePartialReader(bytesOf(incompletePartialSource), 3);
    const IO::Types::ReadAllTextResult incompletePartial = IO::readAllText(incompletePartialReader);
    static_cast<void>(
        context.expectEq("readAllText incomplete early EOF reports EncodingFailed", ErrorCode::EncodingFailed, incompletePartial.status.code));
    static_cast<void>(context.expectEq("readAllText incomplete early EOF preserves valid prefix", std::string{"A"}, incompletePartial.text));

    IO::MemoryReader exactLimitReader(bytesOf(text));
    const IO::Types::ReadAllTextResult exactLimit = IO::readAllText(exactLimitReader, text.size(), 4);
    static_cast<void>(context.expectTrue("readAllText accepts exact byte limit", exactLimit.status.ok()));
    static_cast<void>(context.expectEq("readAllText exact limit preserves text", text, exactLimit.text));

    IO::MemoryReader seekedReader(bytesOf(text));
    static_cast<void>(seekedReader.seek(5, IO::Types::SeekOrigin::Begin));
    const IO::Types::ReadAllTextResult seekedResult = IO::readAllText(seekedReader, 5, 2);
    static_cast<void>(context.expectTrue("readAllText known remaining after seek succeeds", seekedResult.status.ok()));
    static_cast<void>(context.expectEq("readAllText known remaining after seek preserves text", std::string("\0beta", 5), seekedResult.text));

    IO::MemoryReader shortLimitReader(bytesOf(text));
    const IO::Types::ReadAllTextResult shortLimit = IO::readAllText(shortLimitReader, text.size() - 1, 4);
    static_cast<void>(context.expectEq("readAllText rejects known size beyond limit", ErrorCode::SizeLimitExceeded, shortLimit.status.code));
    static_cast<void>(context.expectTrue("readAllText limit failure returns empty text", shortLimit.text.empty()));

    IO::MemoryReader zeroLimitReader(bytesOf(text));
    const IO::Types::ReadAllTextResult zeroLimit = IO::readAllText(zeroLimitReader, 0, 4);
    static_cast<void>(context.expectEq("readAllText zero limit rejects non-empty known stream", ErrorCode::SizeLimitExceeded, zeroLimit.status.code));
    static_cast<void>(context.expectTrue("readAllText zero limit returns empty text", zeroLimit.text.empty()));

    IO::MemoryReader emptyReader(bytesOf(""));
    const IO::Types::ReadAllTextResult emptyResult = IO::readAllText(emptyReader, IO::kNoByteLimit, 4);
    static_cast<void>(context.expectTrue("readAllText empty known size succeeds", emptyResult.status.ok()));
    static_cast<void>(context.expectTrue("readAllText empty known size returns empty text", emptyResult.text.empty()));

    std::array<std::byte, 2> scratch{};
    UnknownSizeChunkReader scratchReader(bytesOf(text), 1);
    const IO::Types::ReadAllTextResult scratchResult = IO::readAllText(scratchReader, std::span<std::byte>(scratch));
    static_cast<void>(context.expectTrue("readAllText scratch overload succeeds", scratchResult.status.ok()));
    static_cast<void>(context.expectEq("readAllText scratch overload preserves text", text, scratchResult.text));

    UnknownSizeChunkReader exactScratchLimitReader(bytesOf(text), 2, false);
    const IO::Types::ReadAllTextResult exactScratchLimit = IO::readAllText(exactScratchLimitReader, std::span<std::byte>(scratch), text.size());
    static_cast<void>(context.expectTrue("readAllText accepts exact unknown-size limit after EOF probe", exactScratchLimit.status.ok()));
    static_cast<void>(context.expectEq("readAllText exact unknown-size limit preserves text", text, exactScratchLimit.text));

    const std::string splitLimitText = makeStringBytes({0x41, 0xe2, 0x82, 0xac});
    UnknownSizeChunkReader splitLimitReader(bytesOf(splitLimitText), 2);
    const IO::Types::ReadAllTextResult splitLimit = IO::readAllText(splitLimitReader, std::span<std::byte>(scratch), 2);
    static_cast<void>(
        context.expectEq("readAllText preserves SizeLimitExceeded when limit cuts UTF-8", ErrorCode::SizeLimitExceeded, splitLimit.status.code));
    static_cast<void>(context.expectEq("readAllText trims UTF-8 suffix cut by limit", std::string{"A"}, splitLimit.text));

    const std::string incompleteAtExactLimit = makeStringBytes({0x41, 0xe2});
    UnknownSizeChunkReader incompleteAtExactLimitReader(bytesOf(incompleteAtExactLimit), 2, false);
    const IO::Types::ReadAllTextResult incompleteAtExactLimitResult =
        IO::readAllText(incompleteAtExactLimitReader, std::span<std::byte>(scratch), incompleteAtExactLimit.size());
    static_cast<void>(context.expectEq(
        "readAllText exact-limit EOF rejects incomplete UTF-8",
        ErrorCode::EncodingFailed,
        incompleteAtExactLimitResult.status.code));
    static_cast<void>(
        context.expectEq("readAllText exact-limit incomplete UTF-8 preserves valid prefix", std::string{"A"}, incompleteAtExactLimitResult.text));

    ProbeFailureReader probeFailureReader(bytesOf(text));
    const IO::Types::ReadAllTextResult probeFailure = IO::readAllText(probeFailureReader, std::span<std::byte>(scratch), text.size());
    static_cast<void>(context.expectEq("readAllText propagates limit probe failure", ErrorCode::PermissionDenied, probeFailure.status.code));
    static_cast<void>(context.expectEq("readAllText limit probe failure preserves limited text", text, probeFailure.text));

    UnknownSizeChunkReader scratchLimitReader(bytesOf(text), 2);
    const IO::Types::ReadAllTextResult scratchLimit = IO::readAllText(scratchLimitReader, std::span<std::byte>(scratch), 3);
    static_cast<void>(
        context.expectEq("readAllText rejects unknown-size stream beyond limit", ErrorCode::SizeLimitExceeded, scratchLimit.status.code));
    static_cast<void>(context.expectEq("readAllText limit failure preserves limited text", std::string{"alp"}, scratchLimit.text));

    const std::vector<std::byte> emptySource;
    UnknownSizeChunkReader emptyZeroLimitUnknownReader(spanOf(emptySource), 1);
    const IO::Types::ReadAllTextResult emptyZeroLimitUnknown = IO::readAllText(emptyZeroLimitUnknownReader, std::span<std::byte>(scratch), 0);
    static_cast<void>(context.expectTrue("readAllText zero limit accepts empty unknown stream", emptyZeroLimitUnknown.status.ok()));

    UnknownSizeChunkReader zeroLimitUnknownReader(bytesOf(text), 1);
    const IO::Types::ReadAllTextResult zeroLimitUnknown = IO::readAllText(zeroLimitUnknownReader, std::span<std::byte>(scratch), 0);
    static_cast<void>(
        context.expectEq("readAllText zero limit rejects non-empty unknown stream", ErrorCode::SizeLimitExceeded, zeroLimitUnknown.status.code));

    ReportedSizeReader shortKnownReader(bytesOf(text), text.size() + 2);
    const IO::Types::ReadAllTextResult shortKnown = IO::readAllText(shortKnownReader, IO::kNoByteLimit, 4);
    static_cast<void>(context.expectEq("readAllText reports early known-size EOF as PartialRead", ErrorCode::PartialRead, shortKnown.status.code));
    static_cast<void>(context.expectEq("readAllText partial read preserves text", text, shortKnown.text));

    FailingKnownSizeReader failingReader(bytesOf(text), 2, ErrorCode::PermissionDenied);
    const IO::Types::ReadAllTextResult failingResult = IO::readAllText(failingReader, IO::kNoByteLimit, 4);
    static_cast<void>(context.expectEq("readAllText direct fill propagates failure", ErrorCode::PermissionDenied, failingResult.status.code));
    static_cast<void>(context.expectEq("readAllText direct fill keeps text before failure", std::string{"alp"}, failingResult.text));

    SizeQueryFailureReader queryFailureReader;
    const IO::Types::ReadAllTextResult queryFailure = IO::readAllText(queryFailureReader, IO::kNoByteLimit, 4);
    static_cast<void>(context.expectEq("readAllText propagates size query failure", ErrorCode::PermissionDenied, queryFailure.status.code));
    static_cast<void>(context.expectFalse("readAllText does not read after size query failure", queryFailureReader.readCalled()));

    PositionQueryFailureReader positionQueryFailureReader;
    const IO::Types::ReadAllTextResult positionQueryFailure = IO::readAllText(positionQueryFailureReader, IO::kNoByteLimit, 4);
    static_cast<void>(
        context.expectEq("readAllText propagates position query failure", ErrorCode::PermissionDenied, positionQueryFailure.status.code));
    static_cast<void>(context.expectFalse("readAllText does not read after position query failure", positionQueryFailureReader.readCalled()));

    ImpossiblePositionReader impossiblePositionReader;
    const IO::Types::ReadAllTextResult impossiblePosition = IO::readAllText(impossiblePositionReader, IO::kNoByteLimit, 4);
    static_cast<void>(context.expectEq("readAllText rejects position beyond size", ErrorCode::InvalidArgument, impossiblePosition.status.code));
    static_cast<void>(context.expectFalse("readAllText does not read after impossible position", impossiblePositionReader.readCalled()));

    UnsupportedSizeReader unsupportedSizeReader(bytesOf(text));
    const IO::Types::ReadAllTextResult unsupportedSize = IO::readAllText(unsupportedSizeReader, IO::kNoByteLimit, 4);
    static_cast<void>(context.expectTrue("readAllText falls back when size is Unsupported", unsupportedSize.status.ok()));
    static_cast<void>(context.expectEq("readAllText Unsupported fallback preserves text", text, unsupportedSize.text));

    InvalidReadCountReader invalidCountReader;
    const IO::Types::ReadAllTextResult invalidCount = IO::readAllText(invalidCountReader, std::span<std::byte>(scratch));
    static_cast<void>(context.expectEq("readAllText rejects invalid reader byte count", ErrorCode::ReadFailed, invalidCount.status.code));

    UnknownSizeChunkReader zeroProgressReader(bytesOf(text), 0);
    const IO::Types::ReadAllTextResult zeroProgress = IO::readAllText(zeroProgressReader, std::span<std::byte>(scratch));
    static_cast<void>(context.expectEq("readAllText rejects zero-progress reader", ErrorCode::ReadFailed, zeroProgress.status.code));

    ZeroProgressKnownSizeReader zeroProgressKnownSizeReader;
    const IO::Types::ReadAllTextResult zeroProgressKnownSize = IO::readAllText(zeroProgressKnownSizeReader, IO::kNoByteLimit, 4);
    static_cast<void>(
        context.expectEq("readAllText rejects zero-progress known-size reader", ErrorCode::ReadFailed, zeroProgressKnownSize.status.code));

    UnknownSizeChunkReader emptyScratchReader(bytesOf(text), 1);
    const IO::Types::ReadAllTextResult emptyScratch = IO::readAllText(emptyScratchReader, std::span<std::byte>{});
    static_cast<void>(context.expectEq("readAllText rejects empty scratch", ErrorCode::InvalidArgument, emptyScratch.status.code));

    IO::MemoryReader invalidBufferReader(bytesOf(text));
    const IO::Types::ReadAllTextResult invalidBuffer = IO::readAllText(invalidBufferReader, IO::kNoByteLimit, 0);
    static_cast<void>(context.expectEq("readAllText rejects zero buffer size", ErrorCode::InvalidArgument, invalidBuffer.status.code));
}
