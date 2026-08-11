@page terminal_read_write Read, write, buffering, and concurrency

This page owns the transfer, outcome, exception, lifetime, blocking, and serialization contracts for Terminal I/O.

## Streams and overloads

Terminal currently exposes `InputStream::Stdin` and `OutputStream::{Stdout, Stderr}`. Free reads default to stdin; free writes and controls default to stdout. Explicit-stream overloads make stderr or protocol endpoint selection visible.

Unknown stream enum values return `InvalidArgument` before endpoint access.

## Process-wide ownership and serialization

Terminal owns one process-wide managed stdin ownership domain plus backend input serialization, and independent process-wide output state for stdout and stderr.

- one persistent `Session` or one direct input operation may own stdin at a time;
- a competing session/direct read returns `ResourceBusy` rather than waiting behind an unrelated owner;
- one `Session` read is serialized with `close()` on that object so restoration cannot race active input;
- stdout operations serialize with one another;
- stderr operations serialize with one another;
- stdout and stderr can progress independently;
- a sequence of public calls is not a transaction.

`getInputCapabilities()` remains observational. `getCursorPosition(outputStream, responseStream, ...)` may coordinate both endpoints; future response-consuming protocol paths must participate in the same managed input ownership contract. Direct C streams, iostreams, native handles, and third-party terminal libraries bypass Terminal's coordination.

Formatted free functions perform formatting before taking the final output lock. This permits a custom formatter to call Terminal without deadlocking the same stream; the nested operation completes before the outer formatted record is emitted.

Caller-owned objects such as `OutputBuffer`, `Session`, and output-state scope objects must not be mutated concurrently except for the documented external `std::stop_token` cancellation request.

## Read operations

| Operation | Purpose | Result |
| --- | --- | --- |
| `readEvent()` | Returns one normalized logical key, paste, or resize event. | `EventReadResult` |
| `readBytes()` | Copies raw bytes into caller storage. | `ByteReadResult` |
| `readText()` | Returns one available complete valid UTF-8 chunk. | `TextReadResult` |
| `readLine()` | Returns one valid UTF-8 line or a terminating partial line. | `LineReadResult` |

`ByteReadOptions::allowPartial = true` permits success after any positive byte count. With `allowPartial = false`, the operation attempts to fill the supplied span until completion, a terminating outcome, or failure.

An empty byte span performs no transfer and returns a successful completed result with `bytesRead == 0`.

Text and line reads require `maxReturnedBytes > 0`. They preserve complete valid UTF-8 code points. If the configured limit cannot hold the next complete code point, the operation returns `SizeLimitExceeded` rather than returning a partial encoding.

Direct input functions use temporary managed ownership. `Session` selects `InputDeliveryMode::Events` or `Stream` once at `open()`. Event delivery accepts only `readEvent()`; Stream delivery accepts byte/text/line reads. A mode-incompatible read returns `Unsupported` without consuming input.

## Read outcomes and partial progress

Expected stopping conditions are separate from backend failures:

| Situation | `status` | `outcome` |
| --- | --- | --- |
| Requested data completed normally | Success | `Completed` |
| Endpoint ended | Success | `EndOfStream` |
| Finite wait expired | Success | `TimedOut` |
| Non-blocking read found no data | Success | `WouldBlock` |
| Caller stop request observed | Success | `Cancelled` |
| Validation, encoding, or backend failure | Failure | Best available stopping state |

`ByteReadResult` can preserve copied bytes together with a terminating outcome or a later backend failure. `LineReadResult` can preserve an unterminated valid UTF-8 line with `EndOfStream`, `TimedOut`, `WouldBlock`, or `Cancelled`. `readText()` returns as soon as one complete UTF-8 chunk is available, so a non-empty text payload normally uses `Completed`. `EventReadResult::event` is populated only for a completed event. Always inspect the status, outcome, and payload together.

`wasTruncated` means the configured returned-byte limit stopped the text or line. Truncation is normally a successful result and unread data remains pending where the backend can preserve it.

For line reads, `consumedLineEnding` reports `None`, `Lf`, `CrLf`, or `Cr`. `ReadLineEndingMode` controls whether the returned string strips, keeps, or normalizes a consumed ending. If the line body fits but a retained ending does not, the ending is consumed and reported while the returned string omits the partial ending and sets `wasTruncated`.

Long-line scanning retains progress between backend chunks, including the case where `\r\n` is split across reads.

## Deadlines, polling, and cancellation

Read options use `std::optional<std::chrono::milliseconds>`:

- `std::nullopt` waits indefinitely;
- `0ms` performs a non-blocking poll;
- a positive duration establishes one total deadline for the complete operation;
- a negative duration is `InvalidArgument`.

Partial input does not restart a positive deadline. `kNoWait` remains a readable `0ms` constant; there is no negative public wait-forever sentinel.

A `std::stop_token` is a requested-cancellation channel, not an operational failure. A pre-requested stop returns successful `ReadOutcome::Cancelled` without consuming input. When a read could block and the selected endpoint cannot observe cancellation, a stoppable token is rejected as `Unsupported` rather than silently ignored.

Current Win32 named-pipe input supports non-blocking reads, finite deadlines, and cooperative cancellation. The current cooked real-console stream backend still supports only unbounded non-cancellable blocking reads; structured Win32 event delivery and its cancellation path are completed in the following #57 backend slice.

Blocking reads can wait indefinitely when requested. Output writes can block in the operating-system endpoint. Terminal does not require an asynchronous runtime or permanent background thread.

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

## Managed ownership and restoration failures

A direct read captures required native terminal state, performs the read, restores the exact snapshot, and releases ownership before returning. If the read succeeds but restoration fails, the restoration failure becomes the returned status. If both fail, the read failure remains primary and restoration failure is appended to that result's diagnostic text on a best-effort basis.

A persistent `Session::close()` behaves differently because the caller can retry cleanup: failed restoration leaves the session open and stdin ownership retained. The non-throwing destructor attempts restoration once and then releases process-wide ownership because no caller remains to retry through that object.

## Failure and exception model

Expected option, endpoint, ownership, capability, Unicode, cancellation, and backend failures use IO statuses/results. Managed read entry points and `Session` lifecycle functions are `noexcept` and translate owned allocation/setup/backend exceptions to statuses.

Free formatted output converts `std::format_error`, allocation failure, and unexpected formatting-stage exceptions to portable statuses before the outer write begins. `OutputBuffer` construction, reserve, append, and formatting use ordinary `std::string` and `std::format` semantics and may throw in this intermediate #57 slice. Other unchanged output/control paths retain their documented exception behavior until the checked-output migration is completed.

Output-state scope factories are `noexcept`. Their returned objects store setup failure in `status()` and remain inactive.

## Related pages

- @ref terminal_unicode_io
- @ref terminal_capabilities_and_redirection
- @ref terminal_segmented_writes
- @ref terminal_troubleshooting
