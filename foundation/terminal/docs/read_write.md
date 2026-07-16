@page terminal_read_write Terminal read, write, buffering, and concurrency

This page owns the transfer, outcome, exception, lifetime, blocking, and serialization contracts for Terminal I/O.

## Streams and overloads

Terminal currently exposes `InputStream::Stdin` and `OutputStream::{Stdout, Stderr}`. Free reads default to stdin; free writes and controls default to stdout. Explicit-stream overloads make stderr or protocol endpoint selection visible.

Unknown stream enum values return `InvalidArgument` before endpoint access.

## Process-wide serialization

Terminal owns one process-wide input mutex and independent process-wide output state for stdout and stderr.

- stdin operations serialize with one another;
- stdout operations serialize with one another;
- stderr operations serialize with one another;
- stdout and stderr can progress independently;
- one public operation is one Terminal serialization unit;
- a sequence of public calls is not a transaction.

`getCursorPosition(outputStream, responseStream, ...)` may coordinate both endpoints. Direct C streams, iostreams, native handles, and third-party terminal libraries bypass Terminal's locks.

Formatted free functions perform formatting before taking the final output lock. This permits a custom formatter to call Terminal without deadlocking the same stream; the nested operation completes before the outer formatted record is emitted.

Caller-owned objects such as `OutputBuffer` and scope objects are not automatically safe for concurrent access to the same object.

## Read operations

| Operation | Purpose | Result |
| --- | --- | --- |
| `readBytes()` | Copies raw bytes into caller storage. | `ByteReadResult` |
| `readText()` | Returns one available complete UTF-8 chunk. | `TextReadResult` |
| `readLine()` | Returns one UTF-8 line or a terminating partial line. | `LineReadResult` |

`ByteReadOptions::allowPartial = true` permits success after any positive byte count. With `allowPartial = false`, the operation attempts to fill the supplied span until completion, a terminating outcome, or failure.

An empty byte span performs no transfer and returns a successful completed result with `bytesRead == 0`.

Text and line reads require `maxReturnedBytes > 0`. They preserve complete UTF-8 code points. If the configured limit cannot hold the next complete code point, the operation returns `SizeLimitExceeded` rather than returning a partial encoding.

## Read outcomes and partial progress

Expected stopping conditions are separate from backend failures:

| Situation | `status` | `outcome` |
| --- | --- | --- |
| Requested data completed normally | Success | `Completed` |
| Endpoint ended | Success | `EndOfStream` |
| Finite wait expired | Success | `TimedOut` |
| Non-blocking read found no data | Success | `WouldBlock` |
| Validation, encoding, or backend failure | Failure | Best available stopping state |

`ByteReadResult` can preserve copied bytes together with a terminating outcome or a later backend failure. `LineReadResult` can preserve an unterminated line with `EndOfStream`, `TimedOut`, or `WouldBlock`. `readText()` returns as soon as one complete UTF-8 chunk is available, so a non-empty text payload uses `Completed`. Always inspect the status, outcome, and payload together.

`wasTruncated` means the configured returned-byte limit stopped the text or line. Truncation is normally a successful result and unread data remains pending where the backend can preserve it.

For line reads, `consumedLineEnding` reports `None`, `Lf`, `CrLf`, or `Cr`. `ReadLineEndingMode` controls whether the returned string strips, keeps, or normalizes a consumed ending. If the line body fits but a retained ending does not, the ending is consumed and reported while the returned string omits the partial ending and sets `wasTruncated`.

Long-line scanning retains progress between backend chunks, including the case where `\r\n` is split across reads.

## Timeouts and blocking

A negative timeout requests an unbounded wait, zero requests a non-blocking attempt, and a positive duration requests a finite wait. Endpoint support is authoritative:

- Windows real-console input currently supports only `kWaitForever`;
- named-pipe input supports finite waits;
- regular redirected files support availability queries but do not promise bounded reads.

Blocking reads can wait indefinitely. Output writes can block in the operating-system endpoint. Terminal does not create an asynchronous output layer.

## Text writes

`writeText()` writes exactly the supplied text. `writeLine()` appends the selected line ending. `print()` and `println()` use compile-time-checked `std::format_string` overloads, format before final stream serialization, then write one logical record.

Plain text writes avoid formatting and style-assembly work when those features are not requested; they are not promised to be allocation-free on every backend or failure path.

A failed platform write can have emitted a prefix. Terminal reports completion status but does not provide rollback or transactional output.

## Byte writes

`writeBytes()` returns `IO::Types::WriteResult`. Real Win32 console handles do not support arbitrary byte output; redirected endpoints can accept bytes.

`bytesWritten` reports bytes accepted before the operation stopped. If a requested flush fails after all bytes were written, `bytesWritten` can equal the full input size while `status` is a failure.

## Segmented writes

`writeSegments()` validates a batch, assembles it as one Terminal operation, and writes the result. It does not make output transactional and does not coordinate with non-Terminal writers. See @ref terminal_segmented_writes.

## OutputBuffer

`OutputBuffer` owns plain-text storage for caller-controlled batching.

- `reserve()` changes capacity without changing text;
- `appendText()` appends bytes expected to contain UTF-8;
- `appendLine()` appends text and the constructor-selected line ending;
- `print()` and `println()` append formatted text;
- `writeTo()` writes without clearing;
- `flushTo()` writes and clears only when the write succeeds.

Despite its name, `flushTo()` does not automatically request an operating-system flush. A backend flush occurs only when `TextWriteOptions::flushMode` is not `None`.

A failed `flushTo()` preserves the text for retry. Closing or initialization concepts do not apply because `OutputBuffer` is only caller-owned memory.

`text()` returns a non-owning view into the internal string. Reserve, append, clear, formatting, move, assignment, or destruction can invalidate it. Do not access the same buffer concurrently without external synchronization.

The buffer does not validate UTF-8 when text is appended. Validation/conversion behavior occurs when a Terminal text write reaches the selected endpoint.

## Flush behavior

Terminal retains no unwritten output in a library-owned stream buffer. `FlushMode::None` requests no flush. On Win32, `Data` and `DataAndMetadataBestEffort` request an operating-system flush only for a standard handle redirected to a regular disk file. Console and pipe flushes are successful no-ops.

Terminal flush does not flush `std::cout`, `std::cerr`, C `FILE*` buffers, or storage owned by another library. Invalid flush modes are rejected before normal emission begins.

## Failure and exception model

Expected option, endpoint, capability, Unicode, and backend failures use IO statuses/results. Public functions are not universally `noexcept`.

Free formatted output converts `std::format_error`, allocation failure, and unexpected formatting-stage exceptions to portable statuses before the outer write begins. `OutputBuffer` construction, reserve, append, and formatting use ordinary `std::string` and `std::format` semantics and may throw. Other operations can allocate temporary assembly or conversion storage and can propagate exceptions not explicitly converted by the implementation.

Scope factories are `noexcept`. Their returned objects store setup failure in `status()` and remain inactive.

## Related pages

- @ref terminal_unicode_io
- @ref terminal_capabilities_and_redirection
- @ref terminal_segmented_writes
- @ref terminal_troubleshooting
