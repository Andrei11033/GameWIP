/// @file read_test.inl
/// @brief Focused IO read tests.

/// ------------------------------------------------------------
/// Whole-stream byte reads
/// ------------------------------------------------------------

/// @brief Verifies whole-reader byte draining across known-size, unknown-size, limit, and failure paths.
void testReadAllBytes(TestSupport::Context &context)
{
    // Check known-size reads, current position, and hard limits.
    const std::vector<std::byte> source = makeBytes({0xde, 0xad, 0x00, 0xbe, 0xef});

    IO::MemoryReader reader(spanOf(source));
    const StatusTypes::Result<std::vector<std::byte>> result = IO::readAllBytes(reader, IO::kNoByteLimit, 2);
    static_cast<void>(context.expectTrue("readAllBytes succeeds", result.ok()));
    if(result.ok())
    {
        static_cast<void>(context.expectEq("readAllBytes preserves bytes", source, result.value()));
    }

    IO::MemoryReader exactLimitReader(spanOf(source));
    const StatusTypes::Result<std::vector<std::byte>> exactLimit = IO::readAllBytes(exactLimitReader, source.size(), 2);
    static_cast<void>(context.expectTrue("readAllBytes accepts exact byte limit", exactLimit.ok()));
    if(exactLimit.ok())
    {
        static_cast<void>(context.expectEq("readAllBytes exact limit preserves bytes", source, exactLimit.value()));
    }

    IO::MemoryReader seekedReader(spanOf(source));

    if(context.expectEq("readAllBytes fixture seek status", ErrorCode::Success, seekedReader.seek(2, IO::Types::SeekOrigin::Begin).status().code()))
    {
        const StatusTypes::Result<std::vector<std::byte>> seekedResult = IO::readAllBytes(seekedReader, 3, 2);
        static_cast<void>(context.expectTrue("readAllBytes known remaining after seek succeeds", seekedResult.ok()));
        if(seekedResult.ok())
        {
            static_cast<void>(
                context.expectEq("readAllBytes known remaining after seek preserves bytes", makeBytes({0x00, 0xbe, 0xef}), seekedResult.value()));
        }
    }

    IO::MemoryReader shortLimitReader(spanOf(source));
    const StatusTypes::Result<std::vector<std::byte>> shortLimit = IO::readAllBytes(shortLimitReader, source.size() - 1, 2);
    static_cast<void>(context.expectEq("readAllBytes rejects known size beyond limit", ErrorCode::SizeLimitExceeded, shortLimit.status().code()));
    static_cast<void>(context.expectFalse("readAllBytes limit failure has no value", shortLimit.hasValue()));

    IO::MemoryReader zeroLimitReader(spanOf(source));
    const StatusTypes::Result<std::vector<std::byte>> zeroLimit = IO::readAllBytes(zeroLimitReader, 0, 2);
    static_cast<void>(
        context.expectEq("readAllBytes zero limit rejects non-empty known stream", ErrorCode::SizeLimitExceeded, zeroLimit.status().code()));
    static_cast<void>(context.expectFalse("readAllBytes zero limit has no value", zeroLimit.hasValue()));

    const std::vector<std::byte> emptySource;
    IO::MemoryReader emptyReader(spanOf(emptySource));
    const StatusTypes::Result<std::vector<std::byte>> emptyResult = IO::readAllBytes(emptyReader, IO::kNoByteLimit, 2);
    static_cast<void>(context.expectTrue("readAllBytes empty known size succeeds", emptyResult.ok()));
    if(emptyResult.ok())
    {
        static_cast<void>(context.expectTrue("readAllBytes empty known size returns empty bytes", emptyResult.value().empty()));
    }

    IO::MemoryReader emptyZeroLimitReader(spanOf(emptySource));
    const StatusTypes::Result<std::vector<std::byte>> emptyZeroLimit = IO::readAllBytes(emptyZeroLimitReader, 0, 2);
    static_cast<void>(context.expectTrue("readAllBytes zero limit accepts empty known stream", emptyZeroLimit.ok()));

    // Check unknown-size reads, scratch reuse, and limit probes.
    std::array<std::byte, 2> scratch{};
    UnknownSizeChunkReader scratchReader(spanOf(source), 1);
    const StatusTypes::Result<std::vector<std::byte>> scratchResult = IO::readAllBytes(scratchReader, std::span<std::byte>(scratch));
    static_cast<void>(context.expectTrue("readAllBytes scratch overload succeeds", scratchResult.ok()));
    if(scratchResult.ok())
    {
        static_cast<void>(context.expectEq("readAllBytes scratch overload preserves bytes", source, scratchResult.value()));
    }

    UnknownSizeChunkReader exactScratchLimitReader(spanOf(source), 2, false);
    const StatusTypes::Result<std::vector<std::byte>> exactScratchLimit = IO::readAllBytes(exactScratchLimitReader, std::span<std::byte>(scratch), source.size());
    static_cast<void>(context.expectTrue("readAllBytes accepts exact unknown-size limit after EOF probe", exactScratchLimit.ok()));
    if(exactScratchLimit.ok())
    {
        static_cast<void>(context.expectEq("readAllBytes exact unknown-size limit preserves bytes", source, exactScratchLimit.value()));
    }

    ProbeFailureReader probeFailureReader(spanOf(source));
    const StatusTypes::Result<std::vector<std::byte>> probeFailure = IO::readAllBytes(probeFailureReader, std::span<std::byte>(scratch), source.size());
    static_cast<void>(context.expectEq("readAllBytes propagates limit probe failure", ErrorCode::PermissionDenied, probeFailure.status().code()));
    static_cast<void>(context.expectTrue("readAllBytes limit probe failure preserves result", probeFailure.hasValue()));
    if(probeFailure.hasValue())
    {
        static_cast<void>(context.expectEq("readAllBytes limit probe failure preserves limited bytes", source, probeFailure.value()));
    }

    UnknownSizeChunkReader scratchLimitReader(spanOf(source), 2);
    const StatusTypes::Result<std::vector<std::byte>> scratchLimit = IO::readAllBytes(scratchLimitReader, std::span<std::byte>(scratch), 3);
    static_cast<void>(
        context.expectEq("readAllBytes rejects unknown-size stream beyond limit", ErrorCode::SizeLimitExceeded, scratchLimit.status().code()));
    static_cast<void>(context.expectTrue("readAllBytes scratch limit preserves partial result", scratchLimit.hasValue()));
    if(scratchLimit.hasValue())
    {
        static_cast<void>(context.expectEq("readAllBytes scratch overload returns limited bytes", makeBytes({0xde, 0xad, 0x00}), scratchLimit.value()));
    }

    UnknownSizeChunkReader zeroLimitUnknownReader(spanOf(source), 1);
    const StatusTypes::Result<std::vector<std::byte>> zeroLimitUnknown = IO::readAllBytes(zeroLimitUnknownReader, std::span<std::byte>(scratch), 0);
    static_cast<void>(
        context.expectEq("readAllBytes zero limit rejects non-empty unknown stream", ErrorCode::SizeLimitExceeded, zeroLimitUnknown.status().code()));
    static_cast<void>(context.expectFalse("readAllBytes zero unknown limit has no value", zeroLimitUnknown.hasValue()));

    UnknownSizeChunkReader emptyZeroLimitUnknownReader(spanOf(emptySource), 1);
    const StatusTypes::Result<std::vector<std::byte>> emptyZeroLimitUnknown = IO::readAllBytes(emptyZeroLimitUnknownReader, std::span<std::byte>(scratch), 0);
    static_cast<void>(context.expectTrue("readAllBytes zero limit accepts empty unknown stream", emptyZeroLimitUnknown.ok()));

    UnknownSizeChunkReader emptyScratchReader(spanOf(source), 1);
    const StatusTypes::Result<std::vector<std::byte>> emptyScratch = IO::readAllBytes(emptyScratchReader, std::span<std::byte>{});
    static_cast<void>(context.expectEq("readAllBytes rejects empty scratch", ErrorCode::InvalidArgument, emptyScratch.status().code()));

    // Check capability fallbacks, reported progress, and invalid reader results.
    SizeWithoutPositionReader sizeWithoutPositionReader(spanOf(source), 2);
    const StatusTypes::Result<std::vector<std::byte>> sizeWithoutPosition = IO::readAllBytes(sizeWithoutPositionReader, std::span<std::byte>(scratch));
    static_cast<void>(context.expectTrue("readAllBytes falls back when size is known but position is not", sizeWithoutPosition.ok()));
    if(sizeWithoutPosition.ok())
    {
        static_cast<void>(
            context.expectEq("readAllBytes size-only fallback preserves remaining bytes", makeBytes({0x00, 0xbe, 0xef}), sizeWithoutPosition.value()));
    }

    ReportedSizeReader shortKnownReader(spanOf(source), source.size() + 2);
    const StatusTypes::Result<std::vector<std::byte>> shortKnown = IO::readAllBytes(shortKnownReader, IO::kNoByteLimit, 2);
    static_cast<void>(context.expectEq("readAllBytes reports early known-size EOF as PartialRead", ErrorCode::PartialRead, shortKnown.status().code()));
    static_cast<void>(context.expectTrue("readAllBytes partial read preserves result", shortKnown.hasValue()));
    if(shortKnown.hasValue())
    {
        static_cast<void>(context.expectEq("readAllBytes partial read preserves bytes", source, shortKnown.value()));
    }

    FailingKnownSizeReader failingReader(spanOf(source), 2, ErrorCode::PermissionDenied);
    const StatusTypes::Result<std::vector<std::byte>> failingResult = IO::readAllBytes(failingReader, IO::kNoByteLimit, 2);
    static_cast<void>(context.expectEq("readAllBytes direct fill propagates failure", ErrorCode::PermissionDenied, failingResult.status().code()));
    static_cast<void>(context.expectTrue("readAllBytes direct fill preserves partial result", failingResult.hasValue()));
    if(failingResult.hasValue())
    {
        static_cast<void>(context.expectEq("readAllBytes direct fill keeps bytes before failure", makeBytes({0xde, 0xad, 0x00}), failingResult.value()));
    }

    SizeQueryFailureReader queryFailureReader;
    const StatusTypes::Result<std::vector<std::byte>> queryFailure = IO::readAllBytes(queryFailureReader, IO::kNoByteLimit, 2);
    static_cast<void>(context.expectEq("readAllBytes propagates size query failure", ErrorCode::PermissionDenied, queryFailure.status().code()));
    static_cast<void>(context.expectFalse("readAllBytes does not read after size query failure", queryFailureReader.readCalled()));

    PositionQueryFailureReader positionQueryFailureReader;
    const StatusTypes::Result<std::vector<std::byte>> positionQueryFailure = IO::readAllBytes(positionQueryFailureReader, IO::kNoByteLimit, 2);
    static_cast<void>(
        context.expectEq("readAllBytes propagates position query failure", ErrorCode::PermissionDenied, positionQueryFailure.status().code()));
    static_cast<void>(context.expectFalse("readAllBytes does not read after position query failure", positionQueryFailureReader.readCalled()));

    ImpossiblePositionReader impossiblePositionReader;
    const StatusTypes::Result<std::vector<std::byte>> impossiblePosition = IO::readAllBytes(impossiblePositionReader, IO::kNoByteLimit, 2);
    static_cast<void>(context.expectEq("readAllBytes rejects position beyond size", ErrorCode::InvalidArgument, impossiblePosition.status().code()));
    static_cast<void>(context.expectFalse("readAllBytes does not read after impossible position", impossiblePositionReader.readCalled()));

    UnsupportedSizeReader unsupportedSizeReader(spanOf(source));
    const StatusTypes::Result<std::vector<std::byte>> unsupportedSize = IO::readAllBytes(unsupportedSizeReader, IO::kNoByteLimit, 2);
    static_cast<void>(context.expectTrue("readAllBytes falls back when size is Unsupported", unsupportedSize.ok()));
    if(unsupportedSize.ok())
    {
        static_cast<void>(context.expectEq("readAllBytes Unsupported fallback preserves bytes", source, unsupportedSize.value()));
    }

    InvalidReadCountReader invalidCountReader;
    const StatusTypes::Result<std::vector<std::byte>> invalidCount = IO::readAllBytes(invalidCountReader, std::span<std::byte>(scratch));
    static_cast<void>(context.expectEq("readAllBytes rejects invalid reader byte count", ErrorCode::ReadFailed, invalidCount.status().code()));

    UnknownSizeChunkReader zeroProgressReader(spanOf(source), 0);
    const StatusTypes::Result<std::vector<std::byte>> zeroProgress = IO::readAllBytes(zeroProgressReader, std::span<std::byte>(scratch));
    static_cast<void>(context.expectEq("readAllBytes rejects zero-progress reader", ErrorCode::ReadFailed, zeroProgress.status().code()));

    ZeroProgressKnownSizeReader zeroProgressKnownSizeReader;
    const StatusTypes::Result<std::vector<std::byte>> zeroProgressKnownSize = IO::readAllBytes(zeroProgressKnownSizeReader, IO::kNoByteLimit, 2);
    static_cast<void>(
        context.expectEq("readAllBytes rejects zero-progress known-size reader", ErrorCode::ReadFailed, zeroProgressKnownSize.status().code()));

    IO::MemoryReader invalidBufferReader(spanOf(source));
    const StatusTypes::Result<std::vector<std::byte>> invalidBuffer = IO::readAllBytes(invalidBufferReader, IO::kNoByteLimit, 0);
    static_cast<void>(context.expectEq("readAllBytes rejects zero buffer size", ErrorCode::InvalidArgument, invalidBuffer.status().code()));
}

/// ------------------------------------------------------------
/// Whole-stream text reads
/// ------------------------------------------------------------

/// @brief Verifies whole-reader text draining and byte-limit behavior.
void testReadAllText(TestSupport::Context &context)
{
    // Check valid UTF-8 across memory and scratch-buffer reads.
    const std::string text("alpha\0beta", 10);

    IO::MemoryReader reader(bytesOf(text));
    const StatusTypes::Result<std::string> result = IO::readAllText(reader, IO::kNoByteLimit, 3);
    static_cast<void>(context.expectTrue("readAllText succeeds", result.ok()));
    if(result.ok())
    {
        static_cast<void>(context.expectEq("readAllText preserves UTF-8 bytes and NUL", text, result.value()));
    }

    const std::string unicodeText = makeStringBytes({0x41, 0xc2, 0xa2, 0xe2, 0x82, 0xac, 0xf0, 0x9f, 0x98, 0x80});
    IO::MemoryReader unicodeReader(bytesOf(unicodeText));
    const StatusTypes::Result<std::string> unicodeResult = IO::readAllText(unicodeReader, IO::kNoByteLimit, 2);
    static_cast<void>(context.expectTrue("readAllText accepts valid multi-byte UTF-8", unicodeResult.ok()));
    if(unicodeResult.ok())
    {
        static_cast<void>(context.expectEq("readAllText preserves valid multi-byte UTF-8", unicodeText, unicodeResult.value()));
    }

    std::array<std::byte, 2> unicodeScratch{};
    UnknownSizeChunkReader splitUnicodeReader(bytesOf(unicodeText), 1);
    const StatusTypes::Result<std::string> splitUnicodeResult = IO::readAllText(splitUnicodeReader, std::span<std::byte>(unicodeScratch));
    static_cast<void>(context.expectTrue("readAllText accepts UTF-8 split across reader chunks", splitUnicodeResult.ok()));
    if(splitUnicodeResult.ok())
    {
        static_cast<void>(context.expectEq("readAllText preserves split UTF-8", unicodeText, splitUnicodeResult.value()));
    }

    // Preserve only the valid prefix when input or a read boundary cuts UTF-8.
    const std::string malformedText = makeStringBytes({0x6f, 0x6b, 0xff, 0x21});
    IO::MemoryReader malformedReader(bytesOf(malformedText));
    const StatusTypes::Result<std::string> malformedResult = IO::readAllText(malformedReader);
    static_cast<void>(context.expectEq("readAllText rejects malformed UTF-8", ErrorCode::EncodingFailed, malformedResult.status().code()));
    static_cast<void>(context.expectTrue("readAllText malformed input preserves result", malformedResult.hasValue()));
    if(malformedResult.hasValue())
    {
        static_cast<void>(context.expectEq("readAllText malformed input preserves valid prefix", std::string{"ok"}, malformedResult.value()));
    }

    const std::string incompleteText = makeStringBytes({0x6f, 0x6b, 0xe2, 0x82});
    IO::MemoryReader incompleteReader(bytesOf(incompleteText));
    const StatusTypes::Result<std::string> incompleteResult = IO::readAllText(incompleteReader);
    static_cast<void>(context.expectEq("readAllText rejects incomplete UTF-8 at EOF", ErrorCode::EncodingFailed, incompleteResult.status().code()));
    static_cast<void>(context.expectTrue("readAllText incomplete input preserves result", incompleteResult.hasValue()));
    if(incompleteResult.hasValue())
    {
        static_cast<void>(context.expectEq("readAllText incomplete input preserves valid prefix", std::string{"ok"}, incompleteResult.value()));
    }

    const std::string incompleteFailureSource = makeStringBytes({0x6f, 0x6b, 0xe2, 0x82, 0xac});
    FailingKnownSizeReader incompleteFailureReader(bytesOf(incompleteFailureSource), 2, ErrorCode::PermissionDenied);
    const StatusTypes::Result<std::string> incompleteFailure = IO::readAllText(incompleteFailureReader);
    static_cast<void>(
        context.expectEq("readAllText preserves backend failure for incomplete suffix", ErrorCode::PermissionDenied, incompleteFailure.status().code()));
    static_cast<void>(context.expectTrue("readAllText backend failure preserves valid text", incompleteFailure.hasValue()));
    if(incompleteFailure.hasValue())
    {
        static_cast<void>(context.expectEq("readAllText trims incomplete suffix after backend failure", std::string{"ok"}, incompleteFailure.value()));
    }

    const std::string malformedFailureSource = makeStringBytes({0x6f, 0x6b, 0xff, 0x21});
    FailingKnownSizeReader malformedFailureReader(bytesOf(malformedFailureSource), 2, ErrorCode::PermissionDenied);
    const StatusTypes::Result<std::string> malformedFailure = IO::readAllText(malformedFailureReader);
    static_cast<void>(context.expectEq(
        "readAllText malformed bytes override simultaneous backend failure",
        ErrorCode::EncodingFailed,
        malformedFailure.status().code()));
    static_cast<void>(context.expectTrue("readAllText malformed failure preserves result", malformedFailure.hasValue()));
    if(malformedFailure.hasValue())
    {
        static_cast<void>(context.expectEq("readAllText malformed failure preserves valid prefix", std::string{"ok"}, malformedFailure.value()));
    }

    const std::string incompletePartialSource = makeStringBytes({0x41, 0xe2});
    ReportedSizeReader incompletePartialReader(bytesOf(incompletePartialSource), 3);
    const StatusTypes::Result<std::string> incompletePartial = IO::readAllText(incompletePartialReader);
    static_cast<void>(
        context.expectEq("readAllText incomplete early EOF reports EncodingFailed", ErrorCode::EncodingFailed, incompletePartial.status().code()));
    static_cast<void>(context.expectTrue("readAllText incomplete early EOF preserves result", incompletePartial.hasValue()));
    if(incompletePartial.hasValue())
    {
        static_cast<void>(context.expectEq("readAllText incomplete early EOF preserves valid prefix", std::string{"A"}, incompletePartial.value()));
    }

    // Check hard limits, exact-limit EOF probes, and partial reads.
    IO::MemoryReader exactLimitReader(bytesOf(text));
    const StatusTypes::Result<std::string> exactLimit = IO::readAllText(exactLimitReader, text.size(), 4);
    static_cast<void>(context.expectTrue("readAllText accepts exact byte limit", exactLimit.ok()));
    if(exactLimit.ok())
    {
        static_cast<void>(context.expectEq("readAllText exact limit preserves text", text, exactLimit.value()));
    }

    IO::MemoryReader seekedReader(bytesOf(text));

    if(context.expectEq("readAllText fixture seek status", ErrorCode::Success, seekedReader.seek(5, IO::Types::SeekOrigin::Begin).status().code()))
    {
        const StatusTypes::Result<std::string> seekedResult = IO::readAllText(seekedReader, 5, 2);
        static_cast<void>(context.expectTrue("readAllText known remaining after seek succeeds", seekedResult.ok()));
        if(seekedResult.ok())
        {
            static_cast<void>(context.expectEq("readAllText known remaining after seek preserves text", std::string("\0beta", 5), seekedResult.value()));
        }
    }

    IO::MemoryReader shortLimitReader(bytesOf(text));
    const StatusTypes::Result<std::string> shortLimit = IO::readAllText(shortLimitReader, text.size() - 1, 4);
    static_cast<void>(context.expectEq("readAllText rejects known size beyond limit", ErrorCode::SizeLimitExceeded, shortLimit.status().code()));
    static_cast<void>(context.expectFalse("readAllText limit failure has no value", shortLimit.hasValue()));

    IO::MemoryReader zeroLimitReader(bytesOf(text));
    const StatusTypes::Result<std::string> zeroLimit = IO::readAllText(zeroLimitReader, 0, 4);
    static_cast<void>(context.expectEq("readAllText zero limit rejects non-empty known stream", ErrorCode::SizeLimitExceeded, zeroLimit.status().code()));
    static_cast<void>(context.expectFalse("readAllText zero limit has no value", zeroLimit.hasValue()));

    IO::MemoryReader emptyReader(bytesOf(""));
    const StatusTypes::Result<std::string> emptyResult = IO::readAllText(emptyReader, IO::kNoByteLimit, 4);
    static_cast<void>(context.expectTrue("readAllText empty known size succeeds", emptyResult.ok()));
    if(emptyResult.ok())
    {
        static_cast<void>(context.expectTrue("readAllText empty known size returns empty text", emptyResult.value().empty()));
    }

    std::array<std::byte, 2> scratch{};
    UnknownSizeChunkReader scratchReader(bytesOf(text), 1);
    const StatusTypes::Result<std::string> scratchResult = IO::readAllText(scratchReader, std::span<std::byte>(scratch));
    static_cast<void>(context.expectTrue("readAllText scratch overload succeeds", scratchResult.ok()));
    if(scratchResult.ok())
    {
        static_cast<void>(context.expectEq("readAllText scratch overload preserves text", text, scratchResult.value()));
    }

    UnknownSizeChunkReader exactScratchLimitReader(bytesOf(text), 2, false);
    const StatusTypes::Result<std::string> exactScratchLimit = IO::readAllText(exactScratchLimitReader, std::span<std::byte>(scratch), text.size());
    static_cast<void>(context.expectTrue("readAllText accepts exact unknown-size limit after EOF probe", exactScratchLimit.ok()));
    if(exactScratchLimit.ok())
    {
        static_cast<void>(context.expectEq("readAllText exact unknown-size limit preserves text", text, exactScratchLimit.value()));
    }

    const std::string splitLimitText = makeStringBytes({0x41, 0xe2, 0x82, 0xac});
    UnknownSizeChunkReader splitLimitReader(bytesOf(splitLimitText), 2);
    const StatusTypes::Result<std::string> splitLimit = IO::readAllText(splitLimitReader, std::span<std::byte>(scratch), 2);
    static_cast<void>(
        context.expectEq("readAllText preserves SizeLimitExceeded when limit cuts UTF-8", ErrorCode::SizeLimitExceeded, splitLimit.status().code()));
    static_cast<void>(context.expectTrue("readAllText limit failure preserves valid text", splitLimit.hasValue()));
    if(splitLimit.hasValue())
    {
        static_cast<void>(context.expectEq("readAllText trims UTF-8 suffix cut by limit", std::string{"A"}, splitLimit.value()));
    }

    const std::string incompleteAtExactLimit = makeStringBytes({0x41, 0xe2});
    UnknownSizeChunkReader incompleteAtExactLimitReader(bytesOf(incompleteAtExactLimit), 2, false);
    const StatusTypes::Result<std::string> incompleteAtExactLimitResult =
        IO::readAllText(incompleteAtExactLimitReader, std::span<std::byte>(scratch), incompleteAtExactLimit.size());
    static_cast<void>(context.expectEq(
        "readAllText exact-limit EOF rejects incomplete UTF-8",
        ErrorCode::EncodingFailed,
        incompleteAtExactLimitResult.status().code()));
    static_cast<void>(context.expectTrue("readAllText exact-limit failure preserves valid text", incompleteAtExactLimitResult.hasValue()));
    if(incompleteAtExactLimitResult.hasValue())
    {
        static_cast<void>(context.expectEq(
            "readAllText exact-limit incomplete UTF-8 preserves valid prefix", std::string{"A"}, incompleteAtExactLimitResult.value()));
    }

    ProbeFailureReader probeFailureReader(bytesOf(text));
    const StatusTypes::Result<std::string> probeFailure = IO::readAllText(probeFailureReader, std::span<std::byte>(scratch), text.size());
    static_cast<void>(context.expectEq("readAllText propagates limit probe failure", ErrorCode::PermissionDenied, probeFailure.status().code()));
    static_cast<void>(context.expectTrue("readAllText limit probe failure preserves result", probeFailure.hasValue()));
    if(probeFailure.hasValue())
    {
        static_cast<void>(context.expectEq("readAllText limit probe failure preserves limited text", text, probeFailure.value()));
    }

    UnknownSizeChunkReader scratchLimitReader(bytesOf(text), 2);
    const StatusTypes::Result<std::string> scratchLimit = IO::readAllText(scratchLimitReader, std::span<std::byte>(scratch), 3);
    static_cast<void>(
        context.expectEq("readAllText rejects unknown-size stream beyond limit", ErrorCode::SizeLimitExceeded, scratchLimit.status().code()));
    static_cast<void>(context.expectTrue("readAllText limit failure preserves partial result", scratchLimit.hasValue()));
    if(scratchLimit.hasValue())
    {
        static_cast<void>(context.expectEq("readAllText limit failure preserves limited text", std::string{"alp"}, scratchLimit.value()));
    }

    const std::vector<std::byte> emptySource;
    UnknownSizeChunkReader emptyZeroLimitUnknownReader(spanOf(emptySource), 1);
    const StatusTypes::Result<std::string> emptyZeroLimitUnknown = IO::readAllText(emptyZeroLimitUnknownReader, std::span<std::byte>(scratch), 0);
    static_cast<void>(context.expectTrue("readAllText zero limit accepts empty unknown stream", emptyZeroLimitUnknown.ok()));

    UnknownSizeChunkReader zeroLimitUnknownReader(bytesOf(text), 1);
    const StatusTypes::Result<std::string> zeroLimitUnknown = IO::readAllText(zeroLimitUnknownReader, std::span<std::byte>(scratch), 0);
    static_cast<void>(
        context.expectEq("readAllText zero limit rejects non-empty unknown stream", ErrorCode::SizeLimitExceeded, zeroLimitUnknown.status().code()));

    // Check capability failures and invalid reader progress.
    ReportedSizeReader shortKnownReader(bytesOf(text), text.size() + 2);
    const StatusTypes::Result<std::string> shortKnown = IO::readAllText(shortKnownReader, IO::kNoByteLimit, 4);
    static_cast<void>(context.expectEq("readAllText reports early known-size EOF as PartialRead", ErrorCode::PartialRead, shortKnown.status().code()));
    static_cast<void>(context.expectTrue("readAllText partial read preserves result", shortKnown.hasValue()));
    if(shortKnown.hasValue())
    {
        static_cast<void>(context.expectEq("readAllText partial read preserves text", text, shortKnown.value()));
    }

    FailingKnownSizeReader failingReader(bytesOf(text), 2, ErrorCode::PermissionDenied);
    const StatusTypes::Result<std::string> failingResult = IO::readAllText(failingReader, IO::kNoByteLimit, 4);
    static_cast<void>(context.expectEq("readAllText direct fill propagates failure", ErrorCode::PermissionDenied, failingResult.status().code()));
    static_cast<void>(context.expectTrue("readAllText direct fill preserves partial result", failingResult.hasValue()));
    if(failingResult.hasValue())
    {
        static_cast<void>(context.expectEq("readAllText direct fill keeps text before failure", std::string{"alp"}, failingResult.value()));
    }

    SizeQueryFailureReader queryFailureReader;
    const StatusTypes::Result<std::string> queryFailure = IO::readAllText(queryFailureReader, IO::kNoByteLimit, 4);
    static_cast<void>(context.expectEq("readAllText propagates size query failure", ErrorCode::PermissionDenied, queryFailure.status().code()));
    static_cast<void>(context.expectFalse("readAllText does not read after size query failure", queryFailureReader.readCalled()));

    PositionQueryFailureReader positionQueryFailureReader;
    const StatusTypes::Result<std::string> positionQueryFailure = IO::readAllText(positionQueryFailureReader, IO::kNoByteLimit, 4);
    static_cast<void>(
        context.expectEq("readAllText propagates position query failure", ErrorCode::PermissionDenied, positionQueryFailure.status().code()));
    static_cast<void>(context.expectFalse("readAllText does not read after position query failure", positionQueryFailureReader.readCalled()));

    ImpossiblePositionReader impossiblePositionReader;
    const StatusTypes::Result<std::string> impossiblePosition = IO::readAllText(impossiblePositionReader, IO::kNoByteLimit, 4);
    static_cast<void>(context.expectEq("readAllText rejects position beyond size", ErrorCode::InvalidArgument, impossiblePosition.status().code()));
    static_cast<void>(context.expectFalse("readAllText does not read after impossible position", impossiblePositionReader.readCalled()));

    UnsupportedSizeReader unsupportedSizeReader(bytesOf(text));
    const StatusTypes::Result<std::string> unsupportedSize = IO::readAllText(unsupportedSizeReader, IO::kNoByteLimit, 4);
    static_cast<void>(context.expectTrue("readAllText falls back when size is Unsupported", unsupportedSize.ok()));
    if(unsupportedSize.ok())
    {
        static_cast<void>(context.expectEq("readAllText Unsupported fallback preserves text", text, unsupportedSize.value()));
    }

    InvalidReadCountReader invalidCountReader;
    const StatusTypes::Result<std::string> invalidCount = IO::readAllText(invalidCountReader, std::span<std::byte>(scratch));
    static_cast<void>(context.expectEq("readAllText rejects invalid reader byte count", ErrorCode::ReadFailed, invalidCount.status().code()));

    UnknownSizeChunkReader zeroProgressReader(bytesOf(text), 0);
    const StatusTypes::Result<std::string> zeroProgress = IO::readAllText(zeroProgressReader, std::span<std::byte>(scratch));
    static_cast<void>(context.expectEq("readAllText rejects zero-progress reader", ErrorCode::ReadFailed, zeroProgress.status().code()));

    ZeroProgressKnownSizeReader zeroProgressKnownSizeReader;
    const StatusTypes::Result<std::string> zeroProgressKnownSize = IO::readAllText(zeroProgressKnownSizeReader, IO::kNoByteLimit, 4);
    static_cast<void>(
        context.expectEq("readAllText rejects zero-progress known-size reader", ErrorCode::ReadFailed, zeroProgressKnownSize.status().code()));

    UnknownSizeChunkReader emptyScratchReader(bytesOf(text), 1);
    const StatusTypes::Result<std::string> emptyScratch = IO::readAllText(emptyScratchReader, std::span<std::byte>{});
    static_cast<void>(context.expectEq("readAllText rejects empty scratch", ErrorCode::InvalidArgument, emptyScratch.status().code()));

    IO::MemoryReader invalidBufferReader(bytesOf(text));
    const StatusTypes::Result<std::string> invalidBuffer = IO::readAllText(invalidBufferReader, IO::kNoByteLimit, 0);
    static_cast<void>(context.expectEq("readAllText rejects zero buffer size", ErrorCode::InvalidArgument, invalidBuffer.status().code()));
}
