/// @file memory_test.inl
/// @brief Focused io memory correctness suites.

/// @brief Verifies MemoryReader construction, reads, state, and close behavior.
void testMemoryReader(TestSupport::Context &context)
{
    const std::vector<std::byte> source = makeBytes({0x41, 0x00, 0x42, 0xff});
    IO::MemoryReader reader(source);
    static_cast<void>(context.expectTrue("MemoryReader starts open", reader.isOpen()));
    static_cast<void>(context.expectTrue("MemoryReader can seek while open", reader.canSeek()));

    std::array<std::byte, 2> firstChunk{};
    const IO::Types::ReadResult firstRead = reader.read(firstChunk);
    static_cast<void>(context.expectTrue("MemoryReader first read succeeds", firstRead.status.ok()));
    static_cast<void>(context.expectEq("MemoryReader first read byte count", std::size_t{2}, firstRead.bytesRead));
    static_cast<void>(context.expectFalse("MemoryReader first read not EOS", firstRead.endOfStream));
    static_cast<void>(
        context.expectEq("MemoryReader preserves first binary chunk", makeBytes({0x41, 0x00}), copyBytes(std::span<const std::byte>(firstChunk))));

    const IO::Types::PositionResult position = reader.position();
    static_cast<void>(context.expectTrue("MemoryReader position succeeds", position.status.ok()));
    static_cast<void>(context.expectEq("MemoryReader position reports current position", std::uint64_t{2}, position.position));

    const IO::Types::SizeResult size = reader.size();
    static_cast<void>(context.expectTrue("MemoryReader size succeeds", size.status.ok()));
    static_cast<void>(context.expectEq("MemoryReader size reports byte count", std::uint64_t{4}, size.sizeBytes));

    std::array<std::byte, 8> finalChunk{};
    const IO::Types::ReadResult finalRead = reader.read(finalChunk);
    static_cast<void>(context.expectTrue("MemoryReader final read succeeds", finalRead.status.ok()));
    static_cast<void>(context.expectEq("MemoryReader final read byte count", std::size_t{2}, finalRead.bytesRead));
    static_cast<void>(context.expectTrue("MemoryReader final read reports EOS", finalRead.endOfStream));
    static_cast<void>(context.expectEq(
        "MemoryReader preserves final binary chunk",
        makeBytes({0x42, 0xff}),
        copyBytes(std::span<const std::byte>(finalChunk.data(), finalRead.bytesRead))));

    const IO::Types::ReadResult eosRead = reader.read(finalChunk);
    static_cast<void>(context.expectTrue("MemoryReader EOS read succeeds", eosRead.status.ok()));
    static_cast<void>(context.expectEq("MemoryReader EOS read returns zero bytes", std::size_t{0}, eosRead.bytesRead));
    static_cast<void>(context.expectTrue("MemoryReader EOS read remains EOS", eosRead.endOfStream));

    const IO::Types::ReadResult emptyRead = reader.read(std::span<std::byte>{});
    static_cast<void>(context.expectTrue("MemoryReader empty read succeeds", emptyRead.status.ok()));
    static_cast<void>(context.expectTrue("MemoryReader empty read reports EOS at end", emptyRead.endOfStream));

    IO::MemoryReader textReader("abc");
    const IO::Types::ReadAllTextResult textResult = IO::readAllText(textReader);
    static_cast<void>(context.expectTrue("MemoryReader string view read succeeds", textResult.status.ok()));
    static_cast<void>(context.expectEq("MemoryReader string view preserves text", std::string{"abc"}, textResult.text));

    std::vector<std::byte> overlapSource = makeBytes({0x10, 0x20, 0x30, 0x40});
    IO::MemoryReader overlapReader(overlapSource);
    const IO::Types::ReadResult overlapRead = overlapReader.read(std::span<std::byte>(overlapSource).subspan(1, 3));
    static_cast<void>(context.expectTrue("MemoryReader overlapping read succeeds", overlapRead.status.ok()));
    static_cast<void>(context.expectEq("MemoryReader overlapping read preserves source order", makeBytes({0x10, 0x10, 0x20, 0x30}), overlapSource));

    static_cast<void>(context.expectTrue("MemoryReader close succeeds", reader.close().ok()));
    static_cast<void>(context.expectFalse("MemoryReader reports closed state", reader.isOpen()));
    static_cast<void>(context.expectFalse("MemoryReader cannot seek after close", reader.canSeek()));
    static_cast<void>(context.expectEq("MemoryReader read after close reports NotOpen", ErrorCode::NotOpen, reader.read(firstChunk).status.code));
    static_cast<void>(context.expectEq("MemoryReader position after close reports NotOpen", ErrorCode::NotOpen, reader.position().status.code));
    static_cast<void>(context.expectEq("MemoryReader size after close reports NotOpen", ErrorCode::NotOpen, reader.size().status.code));
    static_cast<void>(
        context.expectEq("MemoryReader seek after close reports NotOpen", ErrorCode::NotOpen, reader.seek(0, IO::Types::SeekOrigin::Begin).code));
    static_cast<void>(context.expectTrue("MemoryReader repeated close succeeds", reader.close().ok()));
}

/// @brief Verifies MemoryReader seek origins, bounds checks, and position updates.
void testMemoryReaderSeek(TestSupport::Context &context)
{
    const std::vector<std::byte> source = makeBytes({0x10, 0x20, 0x30, 0x40});
    IO::MemoryReader reader(spanOf(source));

    static_cast<void>(context.expectTrue("MemoryReader seek from begin succeeds", reader.seek(2, IO::Types::SeekOrigin::Begin).ok()));
    static_cast<void>(context.expectEq("MemoryReader seek begin position", std::uint64_t{2}, reader.position().position));

    std::array<std::byte, 1> byte{};
    static_cast<void>(reader.read(byte));
    static_cast<void>(
        context.expectEq("MemoryReader read after seek returns target byte", makeBytes({0x30}), copyBytes(std::span<const std::byte>(byte))));

    static_cast<void>(context.expectTrue("MemoryReader seek from current succeeds", reader.seek(-1, IO::Types::SeekOrigin::Current).ok()));
    static_cast<void>(reader.read(byte));
    static_cast<void>(
        context.expectEq("MemoryReader read after current seek returns same byte", makeBytes({0x30}), copyBytes(std::span<const std::byte>(byte))));

    static_cast<void>(context.expectTrue("MemoryReader seek from end succeeds", reader.seek(-1, IO::Types::SeekOrigin::End).ok()));
    static_cast<void>(reader.read(byte));
    static_cast<void>(
        context.expectEq("MemoryReader read after end seek returns last byte", makeBytes({0x40}), copyBytes(std::span<const std::byte>(byte))));

    static_cast<void>(context.expectEq(
        "MemoryReader rejects negative seek before beginning",
        ErrorCode::SeekFailed,
        reader.seek(-5, IO::Types::SeekOrigin::Begin).code));
    static_cast<void>(
        context.expectEq("MemoryReader rejects seek after end", ErrorCode::SeekFailed, reader.seek(1, IO::Types::SeekOrigin::End).code));
    static_cast<void>(context.expectEq(
        "MemoryReader rejects invalid seek origin",
        ErrorCode::InvalidArgument,
        reader.seek(0, static_cast<IO::Types::SeekOrigin>(99)).code));
}

/// @brief Verifies MemoryWriter ownership, aliasing, reserve, clear, and close behavior.
void testMemoryWriter(TestSupport::Context &context)
{
    IO::MemoryWriter writer;
    const std::vector<std::byte> source = makeBytes({0x01, 0x00, 0x02, 0xff});
    const IO::Types::Status initialReserve = writer.reserve(8);
    static_cast<void>(context.expectTrue("MemoryWriter initial reserve succeeds", initialReserve.ok()));
    static_cast<void>(context.expectTrue("MemoryWriter starts open", writer.isOpen()));
    static_cast<void>(context.expectFalse("MemoryWriter is append-only", writer.canSeek()));
    static_cast<void>(context.expectTrue("MemoryWriter initial capacity is reserved", writer.capacity() >= 8));
    static_cast<void>(context.expectEq("MemoryWriter initial position is zero", std::uint64_t{0}, writer.position().position));
    static_cast<void>(
        context.expectEq("MemoryWriter seek reports NotSeekable", ErrorCode::NotSeekable, writer.seek(0, IO::Types::SeekOrigin::Begin).code));

    const IO::Types::WriteResult write = writer.write(source);
    static_cast<void>(context.expectTrue("MemoryWriter write succeeds", write.status.ok()));
    static_cast<void>(context.expectEq("MemoryWriter write byte count", source.size(), write.bytesWritten));
    static_cast<void>(context.expectEq("MemoryWriter size reports bytes written", source.size(), writer.size()));
    static_cast<void>(
        context.expectEq("MemoryWriter position reports bytes written", static_cast<std::uint64_t>(source.size()), writer.position().position));
    static_cast<void>(context.expectFalse("MemoryWriter no longer empty", writer.empty()));
    static_cast<void>(context.expectEq("MemoryWriter preserves binary data", source, copyBytes(writer.bytes())));

    const IO::Types::WriteResult emptyWrite = writer.write(std::span<const std::byte>{});
    static_cast<void>(context.expectTrue("MemoryWriter empty write succeeds", emptyWrite.status.ok()));
    static_cast<void>(context.expectEq("MemoryWriter empty write accepts zero bytes", std::size_t{0}, emptyWrite.bytesWritten));

    const std::size_t capacityBeforeClear = writer.capacity();
    writer.clear();
    static_cast<void>(context.expectTrue("MemoryWriter clear leaves writer empty", writer.empty()));
    static_cast<void>(context.expectEq("MemoryWriter clear resets size", std::size_t{0}, writer.size()));
    static_cast<void>(context.expectEq("MemoryWriter clear resets position", std::uint64_t{0}, writer.position().position));
    static_cast<void>(context.expectEq("MemoryWriter clear preserves capacity", capacityBeforeClear, writer.capacity()));
    static_cast<void>(context.expectEq(
        "MemoryWriter rejects invalid flush modes",
        ErrorCode::InvalidArgument,
        writer.flush(static_cast<IO::Types::FlushMode>(-1)).code));

    const IO::Types::Status largerReserve = writer.reserve(32);
    static_cast<void>(context.expectTrue("MemoryWriter larger reserve succeeds", largerReserve.ok()));
    static_cast<void>(context.expectTrue("MemoryWriter reserve grows capacity", writer.capacity() >= 32));

    IO::MemoryWriter textWriter;
    static_cast<void>(IO::writeAllText(textWriter, std::string_view{"a\0b", 3}));
    const IO::Types::CopyTextResult textCopy = textWriter.copyText();
    static_cast<void>(context.expectTrue("MemoryWriter text copy succeeds", textCopy.status.ok()));
    static_cast<void>(context.expectEq("MemoryWriter text copy preserves NUL bytes", std::string("a\0b", 3), textCopy.text));
    IO::MemoryWriter movedWriter(std::move(textWriter));
    const IO::Types::CopyTextResult movedText = movedWriter.copyText();
    static_cast<void>(context.expectTrue("MemoryWriter moved text copy succeeds", movedText.status.ok()));
    static_cast<void>(context.expectEq("MemoryWriter move preserves bytes", std::string("a\0b", 3), movedText.text));
    static_cast<void>(context.expectTrue("MemoryWriter move preserves open state", movedWriter.isOpen()));

    IO::MemoryWriter emptyTextWriter;
    const IO::Types::CopyTextResult emptyText = emptyTextWriter.copyText();
    static_cast<void>(context.expectTrue("MemoryWriter empty text copy succeeds", emptyText.status.ok()));
    static_cast<void>(context.expectTrue("MemoryWriter empty text copy is empty", emptyText.text.empty()));

    IO::MemoryWriter invalidTextWriter;
    static_cast<void>(invalidTextWriter.write(makeBytes({0x6f, 0x6b, 0xff})));
    const IO::Types::CopyTextResult invalidText = invalidTextWriter.copyText();
    static_cast<void>(context.expectEq("MemoryWriter text copy rejects malformed UTF-8", ErrorCode::EncodingFailed, invalidText.status.code));
    static_cast<void>(context.expectTrue("MemoryWriter malformed text copy returns no text", invalidText.text.empty()));

    IO::MemoryWriter aliasWriter;
    static_cast<void>(aliasWriter.reserve(source.size()));
    static_cast<void>(aliasWriter.write(source));
    const IO::Types::WriteResult aliasWrite = aliasWriter.write(aliasWriter.bytes().subspan(1, 2));
    static_cast<void>(context.expectTrue("MemoryWriter aliased write succeeds", aliasWrite.status.ok()));
    static_cast<void>(context.expectEq("MemoryWriter aliased write reports byte count", std::size_t{2}, aliasWrite.bytesWritten));
    static_cast<void>(context.expectEq(
        "MemoryWriter aliased write preserves source bytes",
        makeBytes({0x01, 0x00, 0x02, 0xff, 0x00, 0x02}),
        copyBytes(aliasWriter.bytes())));

    const std::vector<std::byte> takenBytes = aliasWriter.takeBytes();
    static_cast<void>(
        context.expectEq("MemoryWriter takeBytes returns collected bytes", makeBytes({0x01, 0x00, 0x02, 0xff, 0x00, 0x02}), takenBytes));
    static_cast<void>(context.expectTrue("MemoryWriter takeBytes leaves writer empty", aliasWriter.empty()));
    static_cast<void>(context.expectTrue("MemoryWriter remains open after takeBytes", aliasWriter.isOpen()));
    static_cast<void>(context.expectEq("MemoryWriter takeBytes resets position", std::uint64_t{0}, aliasWriter.position().position));

    static_cast<void>(context.expectTrue("MemoryWriter close succeeds", writer.close().ok()));
    static_cast<void>(context.expectFalse("MemoryWriter reports closed state", writer.isOpen()));
    static_cast<void>(context.expectFalse("MemoryWriter remains non-seekable after close", writer.canSeek()));
    static_cast<void>(context.expectEq("MemoryWriter write after close reports NotOpen", ErrorCode::NotOpen, writer.write(source).status.code));
    static_cast<void>(context.expectEq("MemoryWriter flush after close reports NotOpen", ErrorCode::NotOpen, writer.flush().code));
    static_cast<void>(context.expectEq("MemoryWriter position after close reports NotOpen", ErrorCode::NotOpen, writer.position().status.code));
    static_cast<void>(context.expectTrue("MemoryWriter reserve remains available after close", writer.reserve(64).ok()));
    static_cast<void>(context.expectTrue("MemoryWriter copied text remains available after close", writer.copyText().status.ok()));
    static_cast<void>(context.expectTrue("MemoryWriter repeated close succeeds", writer.close().ok()));
}
