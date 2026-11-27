/// @file contracts_test.inl
/// @brief Focused IO contract tests.

/// @brief Verifies default Reader and Writer optional-operation contracts.
void testDefaultContracts(TestSupport::Context &context)
{
    // Check the default Reader capabilities and lifecycle.
    MinimalReader reader;
    static_cast<void>(context.expectTrue("Reader isOpen defaults to true", reader.isOpen()));
    static_cast<void>(context.expectFalse("Reader canSeek defaults to false", reader.canSeek()));
    static_cast<void>(context.expectTrue("Reader close default succeeds", reader.close().ok()));
    static_cast<void>(context.expectTrue("Stateless Reader remains open after default close", reader.isOpen()));
    static_cast<void>(context.expectEq("Reader position defaults to NotSeekable", ErrorCode::NotSeekable, reader.position().status().code()));
    static_cast<void>(context.expectEq("Reader size defaults to NotSeekable", ErrorCode::NotSeekable, reader.size().status().code()));
    static_cast<void>(context.expectEq(
        "Reader seek defaults to NotSeekable",
        ErrorCode::NotSeekable,
        reader.seek(0, IO::Types::SeekOrigin::Begin).status().code()));

    // Check the default Writer capabilities and lifecycle.
    ChunkedWriter writer(1);
    static_cast<void>(context.expectTrue("Writer isOpen defaults to true", writer.isOpen()));
    static_cast<void>(context.expectFalse("Writer canSeek defaults to false", writer.canSeek()));
    static_cast<void>(context.expectTrue("Writer flush default succeeds", writer.flush().ok()));
    static_cast<void>(context.expectEq(
        "Writer rejects invalid flush modes",
        ErrorCode::InvalidArgument,
        writer.flush(static_cast<IO::Types::FlushMode>(-1)).status().code()));
    static_cast<void>(context.expectTrue("Writer close default succeeds", writer.close().ok()));
    static_cast<void>(context.expectTrue("Stateless Writer remains open after default close", writer.isOpen()));
    static_cast<void>(context.expectEq("Writer position defaults to NotSeekable", ErrorCode::NotSeekable, writer.position().status().code()));
    static_cast<void>(context.expectEq(
        "Writer seek defaults to NotSeekable",
        ErrorCode::NotSeekable,
        writer.seek(0, IO::Types::SeekOrigin::Begin).status().code()));
}
