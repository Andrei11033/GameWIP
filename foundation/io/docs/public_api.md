@page foundation_io_public_api IO public API guide

This page is the user-facing guide for the IO public API. Header comments stay compact for IntelliSense; this page explains how the pieces fit together and when to choose each API.

## Include and link

```cpp
#include "io/io.h"
```

Link `GameWIP::IO`.

## API family map

| Family | Public APIs | Primary behavior |
| --- | --- | --- |
| Status and errors | `Types::Status`, `Types::ErrorCode`, `makeStatus()`, `successStatus()`, `errorCodeName()` | Portable expected-failure reporting and stable symbolic error names. |
| Reader contract | `Reader`, `Types::ReadResult`, `Types::PositionResult`, `Types::SizeResult`, `Types::SeekOrigin` | Active byte reading with optional size, position, seek, and close capabilities. |
| Writer contract | `Writer`, `Types::WriteResult`, `Types::PositionResult`, `Types::SeekOrigin`, `Types::FlushMode` | Active byte writing with optional position, seek, flush, and close behavior. |
| Memory helpers | `MemoryReader`, `MemoryWriter` | Non-owning memory reads and owning memory writes. |
| Whole-stream reads | `readAllBytes()`, `readAllText()` | Drain a Reader with optional scratch reuse and a hard accepted-size limit. |
| Whole-stream writes | `writeAllBytes()`, `writeAllText()` | Retry partial successful writes until complete or failed. |

## Types

`Types::Status` carries an `ErrorCode`, optional native error code, and diagnostic message.

`Types::ReadResult`, `Types::WriteResult`, `Types::PositionResult`, `Types::SizeResult`, `Types::ReadAllBytesResult`, and `Types::ReadAllTextResult` are passive result structs.

`Types::SeekOrigin` selects the base for seek operations.

`Types::FlushMode` describes requested flush strength for concrete writers that own flushable state.

## Error names

`errorCodeName()` returns a stable string literal for each known `Types::ErrorCode` value.

This is useful for tests, logs, and developer diagnostics.

`makeStatus()` creates a status from a portable code, optional native code, and optional diagnostic message. `successStatus()` creates a default success status. These public helpers let FileSystem, Terminal, and other IO consumers create statuses consistently.

## Reader and Writer

`Reader` and `Writer` are movable and non-copyable. Concrete implementations can therefore own movable resource state without fighting the base contracts.

`Reader::isOpen()` reports whether the reader currently has readable state. The default implementation returns true for stateless readers.

`Reader::canSeek()` reports whether seek operations are currently available. The default implementation returns false.

`Reader::read()` reads into caller-owned memory and reports both byte count and end-of-stream state.

`Reader::close()` closes closeable state. The default implementation succeeds for readers without owned resources.

`Reader::position()`, `Reader::size()`, and `Reader::seek()` default to `ErrorCode::NotSeekable`.

`Writer::isOpen()` reports whether the writer currently has writable state. The default implementation returns true for stateless writers.

`Writer::canSeek()` reports whether seek operations are currently available. The default implementation returns false.

`Writer::write()` writes from caller-owned memory and reports the accepted byte count.

`Writer::position()` and `Writer::seek()` default to `ErrorCode::NotSeekable`.

`Writer::flush()` and `Writer::close()` default to success for writers without owned resources.

## Memory helpers

`MemoryReader` is a non-owning reader over an existing byte span.

`MemoryReader` also accepts `std::string_view` and lvalue `std::vector<std::byte>` storage as convenience overloads. The reader remains non-owning; the caller-owned storage must outlive the reader. Direct construction from temporary `std::string` or vector storage is rejected because it would leave the reader dangling.

`MemoryReader::read()` safely handles destination spans that overlap its source memory.

`MemoryWriter` owns a growing byte vector and exposes a read-only byte span for collected output.

`MemoryWriter::position()` reports the current append position while open. MemoryWriter remains non-seekable, so `canSeek()` returns false and `seek()` returns `NotSeekable`.

`MemoryWriter::text()` returns a copy of collected bytes as `std::string` without validating UTF-8.

`MemoryWriter::takeBytes()` moves collected bytes out of the writer and leaves the writer empty. Ownership transfer may discard the writer's previous reserved capacity.

`MemoryWriter::reserve()` and `MemoryWriter::capacity()` expose vector capacity control for tests and helpers that know their expected output size.

`MemoryWriter::write()` accepts byte spans and `std::vector<std::byte>` storage. It handles input spans that alias its own current bytes without allocating a temporary copy.

Closing a `MemoryReader` disables reads, position queries, size queries, and seeks. Closing a `MemoryWriter` disables writes, flushes, and stream-position queries, but collected output remains inspectable and extractable.

## Why IO has no open function

IO does not know which resource should be opened, so it intentionally has no generic `open()` API. File opening belongs to FileSystem, and terminal stream access belongs to Terminal. Memory-backed IO uses constructors and `close()` / `isOpen()` state instead of an operating-system-style open operation.

## Whole-stream helpers

`readAllBytes()` and `readAllText()` read until end-of-stream or failure.

`maxBytes` is a hard maximum accepted stream size. If the helper can determine that the stream exceeds `maxBytes`, it returns `SizeLimitExceeded` and preserves the bytes collected before the failure where practical.

When both size and position queries succeed, the whole-stream helpers read the known remaining byte count directly into the final output buffer. This avoids a temporary transfer buffer and preserves bytes read before any later failure.

When size or position returns `NotSeekable` or `Unsupported`, callers may either let the helper allocate a temporary buffer from `bufferSize`, or pass a non-empty caller-owned scratch buffer. Other capability-query failures are propagated.

For unknown-size readers, the helper may perform a one-byte probe after collecting exactly `maxBytes` bytes. End-of-stream succeeds; another byte returns `SizeLimitExceeded`. The probe may advance an over-limit reader by one byte, but the result never stores more than `maxBytes` bytes.

`writeAllBytes()` and `writeAllText()` retry partial successful writes until all data is accepted or a failure is reported.

`writeAllBytes()` accepts byte spans and `std::vector<std::byte>` storage.

## Failure and exception behavior

Expected I/O and contract failures return `Types::Status`.

Whole-stream helpers and `MemoryWriter::write()` convert allocation failure to `SizeLimitExceeded` where practical. Constructors, `MemoryWriter::reserve()`, and `MemoryWriter::text()` may throw allocation exceptions because they do not return `Types::Status`.

## Related pages

- @ref foundation_io_reader_writer_contract
- @ref foundation_io_error_model
- @ref foundation_io_runtime_performance
- @ref foundation_io_examples
