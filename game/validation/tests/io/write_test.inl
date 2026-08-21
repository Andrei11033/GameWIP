/// @file write_test.inl
/// @brief Focused io write correctness suites.

/// @brief Verifies complete byte writes, partial progress, zero progress, and invalid backend counts.
void testWriteAllBytes(TestSupport::Context &context)
{
    const std::vector<std::byte> source = makeBytes({0x01, 0x02, 0x00, 0x03, 0xff});

    ChunkedWriter chunkedWriter(2);
    const IO::Types::WriteResult chunkedResult = IO::writeAllBytes(chunkedWriter, source);
    static_cast<void>(context.expectTrue("writeAllBytes accepts partial successful writes", chunkedResult.status.ok()));
    static_cast<void>(context.expectEq("writeAllBytes reports complete progress", source.size(), chunkedResult.bytesWritten));
    static_cast<void>(context.expectEq("writeAllBytes preserves bytes across partial writes", source, chunkedWriter.bytes()));

    ChunkedWriter zeroProgressWriter(0);
    const IO::Types::WriteResult zeroProgressResult = IO::writeAllBytes(zeroProgressWriter, spanOf(source));
    static_cast<void>(context.expectEq("writeAllBytes rejects zero-byte progress", ErrorCode::WriteFailed, zeroProgressResult.status.code));
    static_cast<void>(context.expectEq("writeAllBytes zero progress reports zero", std::size_t{0}, zeroProgressResult.bytesWritten));

    FailingWriter failingWriter(ErrorCode::PermissionDenied);
    const IO::Types::WriteResult failureResult = IO::writeAllBytes(failingWriter, spanOf(source));
    static_cast<void>(context.expectEq("writeAllBytes propagates writer failure", ErrorCode::PermissionDenied, failureResult.status.code));
    static_cast<void>(context.expectEq("writeAllBytes immediate failure reports zero", std::size_t{0}, failureResult.bytesWritten));

    PartialFailingWriter partialFailingWriter(2, ErrorCode::PermissionDenied);
    const IO::Types::WriteResult partialFailureResult = IO::writeAllBytes(partialFailingWriter, spanOf(source));
    static_cast<void>(
        context.expectEq("writeAllBytes partial failure propagates status", ErrorCode::PermissionDenied, partialFailureResult.status.code));
    static_cast<void>(context.expectEq("writeAllBytes partial failure preserves progress", std::size_t{2}, partialFailureResult.bytesWritten));

    InvalidWriteCountWriter invalidCountWriter;
    const IO::Types::WriteResult invalidCountResult = IO::writeAllBytes(invalidCountWriter, spanOf(source));
    static_cast<void>(context.expectEq("writeAllBytes rejects invalid writer byte count", ErrorCode::WriteFailed, invalidCountResult.status.code));
    static_cast<void>(context.expectEq("writeAllBytes invalid count preserves prior progress", std::size_t{0}, invalidCountResult.bytesWritten));

    FailingWriter emptyFailingWriter(ErrorCode::WriteFailed);
    const IO::Types::WriteResult emptyResult = IO::writeAllBytes(emptyFailingWriter, std::span<const std::byte>{});
    static_cast<void>(context.expectTrue("writeAllBytes empty input succeeds without calling writer", emptyResult.status.ok()));
    static_cast<void>(context.expectEq("writeAllBytes empty input reports zero", std::size_t{0}, emptyResult.bytesWritten));
}

/// @brief Verifies text writes preserve byte progress and failure status.
void testWriteAllText(TestSupport::Context &context)
{
    const std::string text("a\0b\0c", 5);

    ChunkedWriter chunkedWriter(2);
    const IO::Types::WriteResult chunkedResult = IO::writeAllText(chunkedWriter, text);
    static_cast<void>(context.expectTrue("writeAllText accepts partial successful writes", chunkedResult.status.ok()));
    static_cast<void>(context.expectEq("writeAllText reports complete progress", text.size(), chunkedResult.bytesWritten));
    static_cast<void>(context.expectEq("writeAllText preserves text bytes", copyBytes(bytesOf(text)), chunkedWriter.bytes()));

    const std::string unicodeText = makeStringBytes({0xe2, 0x82, 0xac, 0xf0, 0x9f, 0x98, 0x80});
    ChunkedWriter unicodeWriter(1);
    const IO::Types::WriteResult unicodeResult = IO::writeAllText(unicodeWriter, unicodeText);
    static_cast<void>(context.expectTrue("writeAllText accepts valid multi-byte UTF-8", unicodeResult.status.ok()));
    static_cast<void>(context.expectEq("writeAllText preserves valid multi-byte UTF-8", copyBytes(bytesOf(unicodeText)), unicodeWriter.bytes()));

    FailingWriter failingWriter(ErrorCode::WriteFailed);
    const IO::Types::WriteResult failureResult = IO::writeAllText(failingWriter, text);
    static_cast<void>(context.expectEq("writeAllText propagates writer failure", ErrorCode::WriteFailed, failureResult.status.code));
    static_cast<void>(context.expectEq("writeAllText immediate failure reports zero", std::size_t{0}, failureResult.bytesWritten));

    const std::string malformedText = makeStringBytes({0x6f, 0x6b, 0xff});
    ChunkedWriter malformedWriter(2);
    const IO::Types::WriteResult malformedResult = IO::writeAllText(malformedWriter, malformedText);
    static_cast<void>(context.expectEq("writeAllText rejects malformed UTF-8", ErrorCode::EncodingFailed, malformedResult.status.code));
    static_cast<void>(context.expectEq("writeAllText malformed UTF-8 reports zero progress", std::size_t{0}, malformedResult.bytesWritten));
    static_cast<void>(context.expectTrue("writeAllText validates before calling writer", malformedWriter.bytes().empty()));

    const std::string incompleteText = makeStringBytes({0x6f, 0x6b, 0xe2, 0x82});
    ChunkedWriter incompleteWriter(2);
    const IO::Types::WriteResult incompleteResult = IO::writeAllText(incompleteWriter, incompleteText);
    static_cast<void>(context.expectEq("writeAllText rejects incomplete UTF-8", ErrorCode::EncodingFailed, incompleteResult.status.code));
    static_cast<void>(context.expectEq("writeAllText incomplete UTF-8 reports zero progress", std::size_t{0}, incompleteResult.bytesWritten));
    static_cast<void>(context.expectTrue("writeAllText incomplete UTF-8 never reaches writer", incompleteWriter.bytes().empty()));
}
