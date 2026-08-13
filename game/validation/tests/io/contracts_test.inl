/// @file contracts_test.inl
/// @brief Focused io contracts correctness suites.

/// @brief Verifies every portable error-code name and the unknown-value fallback.
void testErrorCodeNames(TestSupport::Context &context)
{
    struct ErrorCodeName
    {
        ErrorCode code;
        std::string_view name;
    };

    constexpr std::array names{
        ErrorCodeName{ErrorCode::Success, "Success"},
        ErrorCodeName{ErrorCode::InvalidArgument, "InvalidArgument"},
        ErrorCodeName{ErrorCode::Unsupported, "Unsupported"},
        ErrorCodeName{ErrorCode::NotOpen, "NotOpen"},
        ErrorCodeName{ErrorCode::AlreadyOpen, "AlreadyOpen"},
        ErrorCodeName{ErrorCode::NotFound, "NotFound"},
        ErrorCodeName{ErrorCode::AlreadyExists, "AlreadyExists"},
        ErrorCodeName{ErrorCode::PermissionDenied, "PermissionDenied"},
        ErrorCodeName{ErrorCode::PathTooLong, "PathTooLong"},
        ErrorCodeName{ErrorCode::IsDirectory, "IsDirectory"},
        ErrorCodeName{ErrorCode::NotDirectory, "NotDirectory"},
        ErrorCodeName{ErrorCode::NotSeekable, "NotSeekable"},
        ErrorCodeName{ErrorCode::EndOfStream, "EndOfStream"},
        ErrorCodeName{ErrorCode::OpenFailed, "OpenFailed"},
        ErrorCodeName{ErrorCode::ReadFailed, "ReadFailed"},
        ErrorCodeName{ErrorCode::WriteFailed, "WriteFailed"},
        ErrorCodeName{ErrorCode::FlushFailed, "FlushFailed"},
        ErrorCodeName{ErrorCode::CloseFailed, "CloseFailed"},
        ErrorCodeName{ErrorCode::SeekFailed, "SeekFailed"},
        ErrorCodeName{ErrorCode::StatFailed, "StatFailed"},
        ErrorCodeName{ErrorCode::RemoveFailed, "RemoveFailed"},
        ErrorCodeName{ErrorCode::ReplaceFailed, "ReplaceFailed"},
        ErrorCodeName{ErrorCode::CopyFailed, "CopyFailed"},
        ErrorCodeName{ErrorCode::MoveFailed, "MoveFailed"},
        ErrorCodeName{ErrorCode::ResizeFailed, "ResizeFailed"},
        ErrorCodeName{ErrorCode::LockFailed, "LockFailed"},
        ErrorCodeName{ErrorCode::UnlockFailed, "UnlockFailed"},
        ErrorCodeName{ErrorCode::DirectoryCreateFailed, "DirectoryCreateFailed"},
        ErrorCodeName{ErrorCode::DirectoryListFailed, "DirectoryListFailed"},
        ErrorCodeName{ErrorCode::DirectoryNotEmpty, "DirectoryNotEmpty"},
        ErrorCodeName{ErrorCode::PartialRead, "PartialRead"},
        ErrorCodeName{ErrorCode::PartialWrite, "PartialWrite"},
        ErrorCodeName{ErrorCode::SizeLimitExceeded, "SizeLimitExceeded"},
        ErrorCodeName{ErrorCode::OutOfMemory, "OutOfMemory"},
        ErrorCodeName{ErrorCode::ResourceBusy, "ResourceBusy"},
        ErrorCodeName{ErrorCode::StorageFull, "StorageFull"},
        ErrorCodeName{ErrorCode::BrokenPipe, "BrokenPipe"},
        ErrorCodeName{ErrorCode::Interrupted, "Interrupted"},
        ErrorCodeName{ErrorCode::EncodingFailed, "EncodingFailed"},
        ErrorCodeName{ErrorCode::NativeFailure, "NativeFailure"},
        ErrorCodeName{ErrorCode::Unknown, "Unknown"},
    };

    for (const ErrorCodeName &entry : names)
    {
        static_cast<void>(context.expectEq(std::format("errorCodeName returns {}", entry.name), entry.name, IO::errorCodeName(entry.code)));
    }

    static_cast<void>(context.expectEq(
        "errorCodeName maps unknown enumerators to Unknown",
        std::string_view{"Unknown"},
        IO::errorCodeName(static_cast<ErrorCode>(-1))));
}

/// @brief Verifies status helpers and default Reader/Writer optional-operation contracts.
void testStatusAndDefaultContracts(TestSupport::Context &context)
{
    const IO::Types::Status success = IO::successStatus();
    static_cast<void>(context.expectTrue("successStatus returns success", success.ok()));
    static_cast<void>(context.expectEq("successStatus native code defaults to zero", std::int64_t{0}, success.nativeCode));
    static_cast<void>(context.expectTrue("successStatus message defaults to empty", success.message.empty()));

    const IO::Types::Status failure = IO::makeStatus(ErrorCode::ReadFailed, 42, "read failed");
    static_cast<void>(context.expectFalse("makeStatus failure is not ok", failure.ok()));
    static_cast<void>(context.expectEq("makeStatus preserves portable code", ErrorCode::ReadFailed, failure.code));
    static_cast<void>(context.expectEq("makeStatus preserves native code", std::int64_t{42}, failure.nativeCode));
    static_cast<void>(context.expectEq("makeStatus preserves message", std::string{"read failed"}, failure.message));

    MinimalReader reader;
    static_cast<void>(context.expectTrue("Reader isOpen defaults to true", reader.isOpen()));
    static_cast<void>(context.expectFalse("Reader canSeek defaults to false", reader.canSeek()));
    static_cast<void>(context.expectTrue("Reader close default succeeds", reader.close().ok()));
    static_cast<void>(context.expectTrue("Stateless Reader remains open after default close", reader.isOpen()));
    static_cast<void>(context.expectEq("Reader position defaults to NotSeekable", ErrorCode::NotSeekable, reader.position().status.code));
    static_cast<void>(context.expectEq("Reader size defaults to NotSeekable", ErrorCode::NotSeekable, reader.size().status.code));
    static_cast<void>(
        context.expectEq("Reader seek defaults to NotSeekable", ErrorCode::NotSeekable, reader.seek(0, IO::Types::SeekOrigin::Begin).code));

    ChunkedWriter writer(1);
    static_cast<void>(context.expectTrue("Writer isOpen defaults to true", writer.isOpen()));
    static_cast<void>(context.expectFalse("Writer canSeek defaults to false", writer.canSeek()));
    static_cast<void>(context.expectTrue("Writer flush default succeeds", writer.flush().ok()));
    static_cast<void>(
        context.expectEq("Writer rejects invalid flush modes", ErrorCode::InvalidArgument, writer.flush(static_cast<IO::Types::FlushMode>(-1)).code));
    static_cast<void>(context.expectTrue("Writer close default succeeds", writer.close().ok()));
    static_cast<void>(context.expectTrue("Stateless Writer remains open after default close", writer.isOpen()));
    static_cast<void>(context.expectEq("Writer position defaults to NotSeekable", ErrorCode::NotSeekable, writer.position().status.code));
    static_cast<void>(
        context.expectEq("Writer seek defaults to NotSeekable", ErrorCode::NotSeekable, writer.seek(0, IO::Types::SeekOrigin::Begin).code));
}

#if IO_INTERNAL_TEST_HOOKS
/// @brief Verifies deterministic allocation, length, and unexpected-exception translation.
void testCheckedFailureTranslation(TestSupport::Context &context)
{
    using FailureKind = IO::TestHooks::FailureKind;
    using FailurePoint = IO::TestHooks::FailurePoint;

    const ScopedIOFailureHooks hooks;
    const std::vector<std::byte> source = makeBytes({0x41, 0x42, 0x43});

    IO::MemoryWriter writer;
    static_cast<void>(writer.write(source));
    const std::vector<std::byte> original = copyBytes(writer.bytes());

    IO::TestHooks::forceNextFailure(FailurePoint::MemoryWriterReserve, FailureKind::OutOfMemory);
    static_cast<void>(context.expectEq("MemoryWriter reserve translates allocation failure", ErrorCode::OutOfMemory, writer.reserve(64).code));

    IO::TestHooks::forceNextFailure(FailurePoint::MemoryWriterReserve, FailureKind::LengthError);
    static_cast<void>(context.expectEq("MemoryWriter reserve translates length failure", ErrorCode::SizeLimitExceeded, writer.reserve(64).code));

    IO::TestHooks::forceNextFailure(FailurePoint::MemoryWriterReserve, FailureKind::Unexpected);
    static_cast<void>(context.expectEq("MemoryWriter reserve translates unexpected failure", ErrorCode::Unknown, writer.reserve(64).code));

    const IO::Types::Status oversizeReserve = writer.reserve(std::numeric_limits<std::size_t>::max());
    static_cast<void>(context.expectEq("MemoryWriter reserve rejects impossible capacity", ErrorCode::SizeLimitExceeded, oversizeReserve.code));

    IO::TestHooks::forceNextFailure(FailurePoint::MemoryWriterCopyText, FailureKind::OutOfMemory);
    const IO::Types::CopyTextResult allocationText = writer.copyText();
    static_cast<void>(context.expectEq("MemoryWriter copyText translates allocation failure", ErrorCode::OutOfMemory, allocationText.status.code));
    static_cast<void>(context.expectTrue("MemoryWriter failed text copy returns no invalid text", allocationText.text.empty()));

    IO::TestHooks::forceNextFailure(FailurePoint::MemoryWriterCopyText, FailureKind::LengthError);
    static_cast<void>(
        context.expectEq("MemoryWriter copyText translates length failure", ErrorCode::SizeLimitExceeded, writer.copyText().status.code));

    IO::TestHooks::forceNextFailure(FailurePoint::MemoryWriterCopyText, FailureKind::Unexpected);
    static_cast<void>(context.expectEq("MemoryWriter copyText translates unexpected failure", ErrorCode::Unknown, writer.copyText().status.code));

    for (const auto [kind, expected] :
         {std::pair{FailureKind::OutOfMemory, ErrorCode::OutOfMemory},
          std::pair{FailureKind::LengthError, ErrorCode::SizeLimitExceeded},
          std::pair{FailureKind::Unexpected, ErrorCode::Unknown}})
    {
        IO::TestHooks::forceNextFailure(FailurePoint::MemoryWriterWrite, kind);
        const IO::Types::WriteResult failedWrite = writer.write(source);
        static_cast<void>(context.expectEq("MemoryWriter write translates injected failure", expected, failedWrite.status.code));
        static_cast<void>(context.expectEq("MemoryWriter failed write reports zero progress", std::size_t{0}, failedWrite.bytesWritten));
        static_cast<void>(context.expectEq("MemoryWriter failed write preserves bytes", original, copyBytes(writer.bytes())));
    }

    for (const auto [kind, expected] :
         {std::pair{FailureKind::OutOfMemory, ErrorCode::OutOfMemory},
          std::pair{FailureKind::LengthError, ErrorCode::SizeLimitExceeded},
          std::pair{FailureKind::Unexpected, ErrorCode::Unknown}})
    {
        IO::MemoryReader bytesReader(source);
        IO::TestHooks::forceNextFailure(FailurePoint::ReadAllBytesStorage, kind);
        const IO::Types::ReadAllBytesResult bytesResult = IO::readAllBytes(bytesReader);
        static_cast<void>(context.expectEq("readAllBytes translates output-storage failure", expected, bytesResult.status.code));
        static_cast<void>(context.expectTrue("readAllBytes storage failure returns no invalid bytes", bytesResult.bytes.empty()));

        IO::MemoryReader textReader(source);
        IO::TestHooks::forceNextFailure(FailurePoint::ReadAllTextStorage, kind);
        const IO::Types::ReadAllTextResult textResult = IO::readAllText(textReader);
        static_cast<void>(context.expectEq("readAllText translates output-storage failure", expected, textResult.status.code));
        static_cast<void>(context.expectTrue("readAllText storage failure returns no invalid text", textResult.text.empty()));
    }

    std::array<std::byte, 2> textScratch{};
    for (const auto [kind, expected] :
         {std::pair{FailureKind::OutOfMemory, ErrorCode::OutOfMemory},
          std::pair{FailureKind::LengthError, ErrorCode::SizeLimitExceeded},
          std::pair{FailureKind::Unexpected, ErrorCode::Unknown}})
    {
        DeferredTextStorageFailureReader textAppendReader(spanOf(source), kind);
        const IO::Types::ReadAllTextResult textAppendFailure = IO::readAllText(textAppendReader, std::span<std::byte>(textScratch));
        static_cast<void>(context.expectEq("readAllText translates unknown-size append failure", expected, textAppendFailure.status.code));
        static_cast<void>(context.expectEq("readAllText unknown-size append failure preserves prior text", std::string{"A"}, textAppendFailure.text));
    }

    for (const auto [kind, expected] :
         {std::pair{FailureKind::OutOfMemory, ErrorCode::OutOfMemory},
          std::pair{FailureKind::LengthError, ErrorCode::SizeLimitExceeded},
          std::pair{FailureKind::Unexpected, ErrorCode::Unknown}})
    {
        UnknownSizeChunkReader bytesScratchAllocationReader(spanOf(source), 1);
        IO::TestHooks::forceNextFailure(FailurePoint::ReadAllScratchAllocation, kind);
        const IO::Types::ReadAllBytesResult bytesScratchAllocationFailure = IO::readAllBytes(bytesScratchAllocationReader);
        static_cast<void>(
            context.expectEq("readAllBytes translates scratch allocation failure", expected, bytesScratchAllocationFailure.status.code));
        static_cast<void>(
            context.expectTrue("readAllBytes scratch allocation failure returns no bytes", bytesScratchAllocationFailure.bytes.empty()));

        UnknownSizeChunkReader textScratchAllocationReader(spanOf(source), 1);
        IO::TestHooks::forceNextFailure(FailurePoint::ReadAllScratchAllocation, kind);
        const IO::Types::ReadAllTextResult textScratchAllocationFailure = IO::readAllText(textScratchAllocationReader);
        static_cast<void>(context.expectEq("readAllText translates scratch allocation failure", expected, textScratchAllocationFailure.status.code));
        static_cast<void>(context.expectTrue("readAllText scratch allocation failure returns no text", textScratchAllocationFailure.text.empty()));
    }

    std::array<std::byte, 2> scratch{};
    UnknownSizeChunkReader appendReader(spanOf(source), 1);
    IO::TestHooks::forceNextFailure(FailurePoint::ReadAllBytesStorage, FailureKind::OutOfMemory);
    const IO::Types::ReadAllBytesResult appendFailure = IO::readAllBytes(appendReader, std::span<std::byte>(scratch));
    static_cast<void>(context.expectEq("readAllBytes translates append allocation failure", ErrorCode::OutOfMemory, appendFailure.status.code));
    static_cast<void>(context.expectTrue("readAllBytes append failure returns no invalid bytes", appendFailure.bytes.empty()));
}
#endif
