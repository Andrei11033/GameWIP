@page io_reader_writer_contract IO Reader and Writer contract

## Reader

`GameWIP::IO::Reader` is the active byte-reading contract.

Reader objects are movable and non-copyable. `canSeek()` reports whether seek operations are currently available; the default is false.

Reader responsibilities:

- report whether readable state is currently open through `isOpen()`;
- read zero or more bytes into caller-provided storage;
- report how many bytes were actually read;
- report end-of-stream separately from ordinary failures;
- close owned readable state through `close()` when applicable;
- expose position and size when the concrete reader supports them;
- report `NotSeekable` for seek/position/size operations that are not supported;
- avoid throwing for expected I/O failures.

`read()` must never report `bytesRead` greater than the destination size.

A successful read may return fewer bytes than requested. It must report end-of-stream when no more bytes are available. Returning success with zero bytes and `endOfStream = false` prevents whole-stream helpers from making progress and is treated as `ReadFailed`.

A read may return bytes together with a failure status. Whole-stream helpers preserve those bytes before returning the failure.

`MemoryReader` is an in-memory Reader implementation for tests, parsing helpers, and code that already has bytes in memory. It preserves binary data exactly, including NUL bytes.

`MemoryReader` is non-owning. The caller-owned span, string view, or vector storage must outlive the reader. Direct construction from temporary `std::string` or vector storage is rejected because it would leave the reader dangling.

`MemoryReader::read()` safely handles destination spans that overlap its source memory.

## Writer

`GameWIP::IO::Writer` is the active byte-writing contract.

Writer objects are movable and non-copyable. `canSeek()` reports whether seek operations are currently available; the default is false.

Writer responsibilities:

- report whether writable state is currently open through `isOpen()`;
- write zero or more bytes from caller-provided storage;
- report how many bytes were actually written;
- expose the current stream position when the concrete writer supports it;
- move the stream position when the concrete writer supports seeking;
- expose flush and close operations where the concrete writer owns a flushable or closeable resource;
- return `PartialWrite`, `WriteFailed`, `FlushFailed`, or `CloseFailed` through `Types::Status` for expected failures;
- avoid throwing for expected I/O failures.

`write()` must never report `bytesWritten` greater than the source size.

A successful write may accept fewer bytes than requested. Returning success with zero bytes for non-empty input prevents whole-stream helpers from making progress and is treated as `WriteFailed`.

A write may report accepted bytes together with a failure status. Whole-stream helpers return that failure and do not retry it.

`MemoryWriter` is an in-memory Writer implementation for tests, formatting helpers, and code that needs to collect bytes. It preserves binary data exactly, including NUL bytes.

Unknown `FlushMode` values return `InvalidArgument`. `isValidFlushMode()` provides the shared validation rule used by IO and libraries that consume its flush contract. The default `Writer` implementation and `MemoryWriter` perform no physical flush, but still validate the enum so invalid requests do not silently succeed.

`MemoryWriter::write()` handles input spans that alias the writer's own current bytes without allocating a temporary copy.

`MemoryWriter::position()` reports the current append position while open. MemoryWriter remains append-only, reports `canSeek() == false`, and returns `NotSeekable` from `seek()`.

## Helper functions

`readAllBytes()` and `readAllText()` repeatedly read from a Reader until end-of-stream or failure.

`maxBytes` is a hard maximum accepted stream size. The helpers return `SizeLimitExceeded` if they detect the stream exceeds the caller-provided limit.

For readers that expose both size and position, the helpers use the remaining byte count from the current position and read directly into the final output storage. If either capability returns `NotSeekable` or `Unsupported`, the helpers use the unknown-size path. Other capability-query failures are propagated. If a reader reports an impossible position greater than its size, the helpers return `InvalidArgument`.

If a known-size reader reaches end-of-stream before producing the known remaining byte count, the helpers return `PartialRead` and preserve bytes collected before the failure.

For unknown-size readers, overloads that take a caller-owned scratch buffer avoid allocating the temporary transfer buffer internally. The scratch buffer must not be empty.

At a finite `maxBytes`, an unknown-size reader may be advanced by one extra byte so the helper can distinguish exact end-of-stream from an over-limit stream. That byte is not stored in the returned result.

`writeAllBytes()` and `writeAllText()` repeatedly write to a Writer until all requested bytes are accepted or a failure occurs. They return `Types::WriteResult`, preserving the total accepted byte count even when the final writer call accepts bytes and reports failure.

Text helpers treat text as UTF-8 bytes. They do not normalize, parse, or validate higher-level formats.

## Thread-safety

Different Reader/Writer objects may be used concurrently.

The same Reader/Writer object is not thread-safe unless the concrete implementation explicitly says otherwise.

`MemoryReader` and `MemoryWriter` are not internally synchronized.

## Blocking behavior

`GameWIP::IO` does not perform operating-system calls by itself.

Whether `read()`, `write()`, `position()`, `size()`, `seek()`, `flush()`, or `close()` may block depends on the concrete Reader or Writer implementation.

`MemoryReader` and `MemoryWriter` do not block on the operating system.

## Exception behavior

Expected I/O failures are returned through `Types::Status`.

Memory allocation failure inside whole-stream helpers is converted to `OutOfMemory` where practical.

Constructors and direct container-capacity operations, such as `MemoryWriter::reserve()`, may still throw allocation exceptions because they do not return `Types::Status`.

`MemoryWriter::text()` may also throw while allocating its returned string copy. `MemoryWriter::takeBytes()` transfers ownership without allocation and may discard the writer's reserved capacity.

Destructors must not throw.

## Zero-length operations

A zero-length read request should return success with `bytesRead = 0`.

A zero-length write request should return success with `bytesWritten = 0`.

A zero-length read may report `endOfStream = true` if the reader is already at the end.

## Lifetime

Destructors must not throw. Concrete implementations that own resources should expose explicit close behavior. Callers that need to observe close failure must call close explicitly before destruction.

Closing `MemoryReader` or `MemoryWriter` is idempotent. A closed memory object cannot be reopened. `MemoryWriter` output remains inspectable, clearable, reservable, and extractable after close.

## Open behavior

IO intentionally has no `open()` function because it does not know which resource should be opened. Resource-owning libraries such as FileSystem and Terminal own open behavior. Memory-backed IO is created directly with constructors and uses `close()` / `isOpen()` state.

## Non-goals

The v1 IO contract does not provide:

- operating-system file access;
- terminal output;
- network I/O;
- async I/O;
- memory-mapped files;
- file watching;
- JSON parsing;
- config parsing;
- asset package parsing;
- texture decoding.
