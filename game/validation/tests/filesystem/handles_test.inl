/// @file handles_test.inl
/// @brief FileSystem file-handle test.

/// @brief Verifies whole-file helpers and reader, writer, and read/write handle contracts.
void testWholeFileHelpersAndHandles(TestSupport::Context &context, const std::filesystem::path &root)
{
    const std::filesystem::path file = root / "handles" / "stream.txt";

    // Verify whole-file helpers and their byte counts.
    const IO::Types::WriteResult write = FileSystem::writeAllText(file, "abc");
    static_cast<void>(context.expectTrue("writeAllText succeeds", write.status.ok()));
    static_cast<void>(context.expectEq("writeAllText byte count", std::size_t{3}, write.bytesWritten));

    const IO::Types::WriteResult append = FileSystem::appendText(file, "def");
    static_cast<void>(context.expectTrue("appendText succeeds", append.status.ok()));
    static_cast<void>(context.expectEq("appendText byte count", std::size_t{3}, append.bytesWritten));

    const IO::Types::ReadAllTextResult text = FileSystem::readAllText(file);
    static_cast<void>(context.expectTrue("readAllText succeeds", text.status.ok()));
    static_cast<void>(context.expectEq("readAllText sees appended content", std::string{"abcdef"}, text.text));

    // Verify read/write handle positioning and close behavior.
    FileSystem::File fileHandle;
    const IO::Types::Status openStatus = fileHandle.open(
        file,
        FileSystem::Types::File::OpenOptions{
            .access = FileSystem::Types::File::Access::ReadWrite,
            .mode = FileSystem::Types::File::OpenMode::OpenExisting,
            .initialPosition = FileSystem::Types::File::InitialPosition::End});

    if(!expectStatus(context, "File open succeeds", openStatus))
    {
        return;
    }

    const IO::Types::PositionResult endPosition = fileHandle.position();
    static_cast<void>(context.expectTrue("File position succeeds", endPosition.status.ok()));
    static_cast<void>(context.expectEq("File::InitialPosition::End seeks to EOF", std::uint64_t{6}, endPosition.position));

    const IO::Types::WriteResult handleWrite = fileHandle.write(bytesOf("g"));
    static_cast<void>(context.expectTrue("File write succeeds", handleWrite.status.ok()));
    static_cast<void>(context.expectEq("File write byte count", std::size_t{1}, handleWrite.bytesWritten));
    static_cast<void>(context.expectTrue("File close succeeds", fileHandle.close().ok()));

    const IO::Types::ReadAllTextResult updated = FileSystem::readAllText(file);
    static_cast<void>(context.expectTrue("readAllText after File write succeeds", updated.status.ok()));
    static_cast<void>(context.expectEq("File write updated content", std::string{"abcdefg"}, updated.text));

    // Verify append-only writer seek restrictions.
    FileSystem::FileWriter appendWriter;
    const IO::Types::Status appendOpen =
        appendWriter.open(file, FileSystem::Types::File::WriterOpenOptions{.mode = FileSystem::Types::File::WriterMode::AppendExisting});

    if(!expectStatus(context, "append writer opens", appendOpen))
    {
        return;
    }

    static_cast<void>(context.expectFalse("append writer is not seekable", appendWriter.canSeek()));
    static_cast<void>(context.expectEq("append writer position is NotSeekable", ErrorCode::NotSeekable, appendWriter.position().status.code));
    static_cast<void>(
        context.expectEq("append writer seek is NotSeekable", ErrorCode::NotSeekable, appendWriter.seek(0, IO::Types::SeekOrigin::Begin).code));
    static_cast<void>(context.expectTrue("append writer writes", appendWriter.write(bytesOf("h")).status.ok()));
    static_cast<void>(context.expectTrue("append writer closes", appendWriter.close().ok()));

    const IO::Types::ReadAllTextResult appendUpdated = FileSystem::readAllText(file);
    static_cast<void>(context.expectTrue("readAllText after append writer succeeds", appendUpdated.status.ok()));
    static_cast<void>(context.expectEq("append writer adds content at EOF", std::string{"abcdefgh"}, appendUpdated.text));
}
