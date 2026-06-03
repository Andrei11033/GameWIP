@page foundation_io_examples IO examples

## Collect text

```cpp
GameWIP::IO::MemoryWriter writer;
GameWIP::IO::Types::Status status = GameWIP::IO::writeAllText(writer, "hello");

if (status.ok()) {
    std::string text = writer.text();
}
```

`MemoryWriter::bytes()` returns the exact bytes written so far.

`MemoryWriter::takeBytes()` moves the collected byte vector out of the writer and may discard the writer's previous reserved capacity.

## Read text from memory

```cpp
GameWIP::IO::MemoryReader reader("controls.default.jump=Space");
GameWIP::IO::Types::ReadAllTextResult result = GameWIP::IO::readAllText(reader);
```

The string view passed to `MemoryReader` must outlive the reader.

## Seek in memory

```cpp
GameWIP::IO::MemoryReader reader(bytes);
reader.seek(2, GameWIP::IO::Types::SeekOrigin::Begin);

std::array<std::byte, 1> next{};
GameWIP::IO::Types::ReadResult read = reader.read(next);
GameWIP::IO::Types::PositionResult position = reader.position();
```

## Byte limits

```cpp
GameWIP::IO::Types::ReadAllBytesResult limited =
    GameWIP::IO::readAllBytes(reader, 1024);
```

`maxBytes` is a hard maximum accepted stream size. For seekable readers that can report size and position, `readAllBytes()` returns `SizeLimitExceeded` before reading when the known remaining byte count is greater than the caller limit.

For unknown-size readers, observing data beyond `maxBytes` returns `SizeLimitExceeded`.

An unknown-size helper may consume one additional probe byte to distinguish an exact-limit stream from an over-limit stream. Exact end-of-stream succeeds, and the returned result never contains more than `maxBytes` bytes.

## Scratch buffers

```cpp
std::array<std::byte, 4096> scratch{};
GameWIP::IO::Types::ReadAllBytesResult result =
    GameWIP::IO::readAllBytes(reader, std::span<std::byte>(scratch));
```

Scratch-buffer overloads are useful for repeated reads from unknown-size streams. Known-size readers use direct output-buffer reads and do not need the scratch storage.

## Error names

```cpp
std::string_view name = GameWIP::IO::errorCodeName(result.status.code);
```

Use `errorCodeName()` for stable symbolic names in tests, logs, and diagnostics.

## Create statuses

```cpp
GameWIP::IO::Types::Status success = GameWIP::IO::successStatus();
GameWIP::IO::Types::Status failure =
    GameWIP::IO::makeStatus(GameWIP::IO::Types::ErrorCode::NativeFailure, nativeCode, "operation failed");
```

Resource-owning libraries can use the public helpers to create consistent IO statuses.

## Reuse writer capacity

```cpp
GameWIP::IO::MemoryWriter writer(4096);

for (const Record& record : records) {
    writer.clear();
    writeRecord(writer, record);
    consume(writer.bytes());
}
```

`clear()` preserves capacity. Use `takeBytes()` instead when the caller should own the collected vector.
