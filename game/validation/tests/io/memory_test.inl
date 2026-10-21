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

    if (!context.expectEq("MemoryReader first read status", ErrorCode::Success, firstRead.status.code) ||
        !context.expectEq("MemoryReader first read byte count", firstChunk.size(), firstRead.bytesRead))
    {
        return;
    }
    static_cast<void>(context.expectFalse("MemoryReader first read not EOS", firstRead.endOfStream));
    static_cast<void>(
        context.expectEq("MemoryReader preserves first binary chunk", makeBytes({0x41, 0x00}), copyBytes(std::span<const std::byte>(firstChunk))));

    IO::MemoryReader movedReader(std::move(reader));
    static_cast<void>(context.expectTrue("MemoryReader move preserves open state", movedReader.isOpen()));
    const IO::Types::PositionResult position = movedReader.position();

    if (context.expectEq("moved MemoryReader position status", ErrorCode::Success, position.status.code))
    {
        static_cast<void>(context.expectEq("MemoryReader move preserves position", std::uint64_t{2}, position.position));
    }

    const IO::Types::SizeResult size = movedReader.size();

    if (context.expectEq("moved MemoryReader size status", ErrorCode::Success, size.status.code))
    {
        static_cast<void>(context.expectEq("MemoryReader move preserves source size", std::uint64_t{4}, size.sizeBytes));
    }

    std::array<std::byte, 8> finalChunk{};
    const IO::Types::ReadResult finalRead = movedReader.read(finalChunk);

    if (!context.expectEq("moved MemoryReader final read status", ErrorCode::Success, finalRead.status.code) ||
        !context.expectEq("moved MemoryReader final read byte count", std::size_t{2}, finalRead.bytesRead))
    {
        return;
    }
    static_cast<void>(context.expectTrue("MemoryReader final read reports EOS", finalRead.endOfStream));
    static_cast<void>(context.expectEq(
        "MemoryReader move continues from the preserved position",
        makeBytes({0x42, 0xff}),
        copyBytes(std::span{finalChunk}.first(finalRead.bytesRead))));

    const IO::Types::ReadResult eosRead = movedReader.read(finalChunk);
    static_cast<void>(context.expectTrue("MemoryReader EOS read succeeds", eosRead.status.ok()));
    static_cast<void>(context.expectEq("MemoryReader EOS read returns zero bytes", std::size_t{0}, eosRead.bytesRead));
    static_cast<void>(context.expectTrue("MemoryReader EOS read remains EOS", eosRead.endOfStream));

    const IO::Types::ReadResult emptyRead = movedReader.read(std::span<std::byte>{});
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

    static_cast<void>(context.expectTrue("MemoryReader close succeeds", movedReader.close().ok()));
    static_cast<void>(context.expectFalse("MemoryReader reports closed state", movedReader.isOpen()));
    static_cast<void>(context.expectFalse("MemoryReader cannot seek after close", movedReader.canSeek()));
    static_cast<void>(
        context.expectEq("MemoryReader read after close reports NotOpen", ErrorCode::NotOpen, movedReader.read(firstChunk).status.code));
    static_cast<void>(context.expectEq("MemoryReader position after close reports NotOpen", ErrorCode::NotOpen, movedReader.position().status.code));
    static_cast<void>(context.expectEq("MemoryReader size after close reports NotOpen", ErrorCode::NotOpen, movedReader.size().status.code));
    static_cast<void>(context.expectEq(
        "MemoryReader seek after close reports NotOpen",
        ErrorCode::NotOpen,
        movedReader.seek(0, IO::Types::SeekOrigin::Begin).code));
    static_cast<void>(context.expectTrue("MemoryReader repeated close succeeds", movedReader.close().ok()));
}

/// @brief Verifies MemoryReader seek origins, bounds checks, and position updates.
void testMemoryReaderSeek(TestSupport::Context &context)
{
    const std::vector<std::byte> source = makeBytes({0x10, 0x20, 0x30, 0x40});
    IO::MemoryReader reader(spanOf(source));

    struct SeekCase
    {
        std::string_view name;
        std::int64_t offset;
        IO::Types::SeekOrigin origin;
        std::uint64_t position;
        std::byte byte;
    };
    const std::array cases{
        SeekCase{"begin", 2, IO::Types::SeekOrigin::Begin, 2, std::byte{0x30}},
        SeekCase{"current", -1, IO::Types::SeekOrigin::Current, 2, std::byte{0x30}},
        SeekCase{"end", -1, IO::Types::SeekOrigin::End, 3, std::byte{0x40}},
    };

    for (const SeekCase &test : cases)
    {
        const std::string name = std::format("MemoryReader seek from {}", test.name);

        if (!context.expectEq(name + " status", ErrorCode::Success, reader.seek(test.offset, test.origin).code))
        {
            return;
        }
        const auto position = reader.position();

        if (context.expectEq(name + " position status", ErrorCode::Success, position.status.code))
        {
            static_cast<void>(context.expectEq(name + " position", test.position, position.position));
        }
        std::array<std::byte, 1> byte{};
        const auto read = reader.read(byte);

        if (!context.expectEq(name + " read status", ErrorCode::Success, read.status.code) ||
            !context.expectEq(name + " read count", byte.size(), read.bytesRead))
        {
            return;
        }
        static_cast<void>(context.expectEq(name + " payload", test.byte, byte[0]));
    }

    if (!context.expectEq("MemoryReader position before rejected seeks", ErrorCode::Success, reader.seek(2, IO::Types::SeekOrigin::Begin).code))
    {
        return;
    }
    struct RejectedSeek
    {
        std::string_view name;
        std::int64_t offset;
        IO::Types::SeekOrigin origin;
        ErrorCode error;
    };
    const std::array rejected{
        RejectedSeek{"before beginning", -5, IO::Types::SeekOrigin::Begin, ErrorCode::SeekFailed},
        RejectedSeek{"after end", 1, IO::Types::SeekOrigin::End, ErrorCode::SeekFailed},
        RejectedSeek{"invalid origin", 0, static_cast<IO::Types::SeekOrigin>(99), ErrorCode::InvalidArgument},
    };

    for (const RejectedSeek &test : rejected)
    {
        const std::string name = std::format("MemoryReader rejected seek {}", test.name);
        static_cast<void>(context.expectEq(name + " status", test.error, reader.seek(test.offset, test.origin).code));
        const auto position = reader.position();

        if (context.expectEq(name + " position status", ErrorCode::Success, position.status.code))
        {
            static_cast<void>(context.expectEq(name + " preserves position", std::uint64_t{2}, position.position));
        }
    }
}

/// @brief Verifies MemoryWriter ownership, aliasing, reserve, clear, and close behavior.
void testMemoryWriter(TestSupport::Context &context)
{
    IO::MemoryWriter writer;
    const std::vector<std::byte> source = makeBytes({0x01, 0x00, 0x02, 0xff});
    const IO::Types::Status initialReserve = writer.reserve(8);

    if (!context.expectEq("MemoryWriter initial reserve status", ErrorCode::Success, initialReserve.code))
    {
        return;
    }
    static_cast<void>(context.expectTrue("MemoryWriter starts open", writer.isOpen()));
    static_cast<void>(context.expectFalse("MemoryWriter is append-only", writer.canSeek()));
    static_cast<void>(context.expectTrue("MemoryWriter initial capacity is reserved", writer.capacity() >= 8));
    static_cast<void>(context.expectEq("MemoryWriter initial position is zero", std::uint64_t{0}, writer.position().position));
    static_cast<void>(
        context.expectEq("MemoryWriter seek reports NotSeekable", ErrorCode::NotSeekable, writer.seek(0, IO::Types::SeekOrigin::Begin).code));

    const IO::Types::WriteResult write = writer.write(source);

    if (!context.expectEq("MemoryWriter write status", ErrorCode::Success, write.status.code) ||
        !context.expectEq("MemoryWriter write byte count", source.size(), write.bytesWritten))
    {
        return;
    }
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
    constexpr std::string_view retainedText = "retained";

    if (!context.expectEq("MemoryWriter retained fixture write", ErrorCode::Success, writer.write(bytesOf(retainedText)).status.code))
    {
        return;
    }
    const std::size_t capacityBeforeFailure = writer.capacity();
    const IO::Types::Status impossibleReserve = writer.reserve((std::numeric_limits<std::size_t>::max)());
    static_cast<void>(context.expectEq("MemoryWriter reserve rejects impossible capacity", ErrorCode::SizeLimitExceeded, impossibleReserve.code));
    static_cast<void>(context.expectEq("MemoryWriter rejected reserve preserves bytes", copyBytes(bytesOf(retainedText)), copyBytes(writer.bytes())));
    static_cast<void>(context.expectEq("MemoryWriter rejected reserve preserves capacity", capacityBeforeFailure, writer.capacity()));

    IO::MemoryWriter textWriter;
    const auto textWrite = IO::writeAllText(textWriter, std::string_view{"a\0b", 3});

    if (!context.expectEq("MemoryWriter text fixture write", ErrorCode::Success, textWrite.status.code))
    {
        return;
    }
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
    const auto malformedWrite = invalidTextWriter.write(makeBytes({0x6f, 0x6b, 0xff}));

    if (!context.expectEq("MemoryWriter malformed fixture write", ErrorCode::Success, malformedWrite.status.code))
    {
        return;
    }
    const IO::Types::CopyTextResult invalidText = invalidTextWriter.copyText();
    static_cast<void>(context.expectEq("MemoryWriter text copy rejects malformed UTF-8", ErrorCode::EncodingFailed, invalidText.status.code));
    static_cast<void>(context.expectTrue("MemoryWriter malformed text copy returns no text", invalidText.text.empty()));

    IO::MemoryWriter aliasWriter;

    if (!context.expectEq("MemoryWriter alias fixture reserve", ErrorCode::Success, aliasWriter.reserve(source.size()).code))
    {
        return;
    }
    const auto aliasSeed = aliasWriter.write(source);

    if (!context.expectEq("MemoryWriter alias fixture write", ErrorCode::Success, aliasSeed.status.code) ||
        !context.expectEq("MemoryWriter alias fixture write count", source.size(), aliasSeed.bytesWritten) ||
        !context.expectEq("MemoryWriter alias fixture size", source.size(), aliasWriter.size()))
    {
        return;
    }
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
    static_cast<void>(context.expectEq("MemoryWriter close preserves bytes", copyBytes(bytesOf(retainedText)), copyBytes(writer.bytes())));
    static_cast<void>(context.expectFalse("MemoryWriter reports closed state", writer.isOpen()));
    static_cast<void>(context.expectFalse("MemoryWriter remains non-seekable after close", writer.canSeek()));
    static_cast<void>(context.expectEq("MemoryWriter write after close reports NotOpen", ErrorCode::NotOpen, writer.write(source).status.code));
    static_cast<void>(context.expectEq("MemoryWriter flush after close reports NotOpen", ErrorCode::NotOpen, writer.flush().code));
    static_cast<void>(context.expectEq("MemoryWriter position after close reports NotOpen", ErrorCode::NotOpen, writer.position().status.code));
    static_cast<void>(context.expectTrue("MemoryWriter reserve remains available after close", writer.reserve(64).ok()));
    const auto closedText = writer.copyText();

    if (context.expectEq("MemoryWriter text copy after close status", ErrorCode::Success, closedText.status.code))
    {
        static_cast<void>(context.expectEq("MemoryWriter text copy after close preserves content", retainedText, closedText.text));
    }
    static_cast<void>(context.expectTrue("MemoryWriter repeated close succeeds", writer.close().ok()));
}
