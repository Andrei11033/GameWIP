/// @file contracts_test.inl
/// @brief Focused io contracts correctness suites.

/// @brief Verifies every portable error-code name and the unknown-value fallback.
void testErrorCodeNames(TestSupport::Context &context)
{
    // Keep the table beside the assertions so additions cannot silently miss a name mapping.
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
