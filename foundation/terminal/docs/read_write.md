@page terminal_read_write Read, write, buffering, and concurrency

Terminal I/O changes behavior according to the endpoint and the kind of data
being transferred. This guide explains text and byte guarantees, stopping
outcomes, blocking, serialization, lifetime, and exception handling for both
direct calls and Sessions.

## Streams and overloads

Terminal currently exposes `Types::Input::Stream::Stdin` and `Types::Output::Stream::{Stdout, Stderr}`. Free reads default to stdin; free writes and
controls default to stdout. Explicit-stream overloads make stderr or protocol endpoint selection visible.

Unknown stream enum values return `InvalidArgument` before endpoint access.

## Process-wide ownership and serialization

Terminal owns one process-wide managed stdin ownership domain plus backend input serialization, and independent process-wide output state for stdout
and stderr.

- One persistent `Session` or one direct input operation may own stdin at a time.
- A competing Session or direct read returns `ResourceBusy`; it does not wait
  behind an unrelated owner.
- Input-consuming operations on one Session serialize with each other.
- Session output may continue while another thread is blocked in a Session read.
- `close()` stops new Session operations and waits for active ones before
  restoring state.
- Stdout operations serialize with other stdout operations, whether they are
  direct or Session-bound. Stderr behaves the same way independently.
- Stdout and stderr can make progress at the same time.
- A sequence of public calls is not a transaction.

`getInputCapabilities()` remains observational. `getCursorPosition(outputStream, responseStream, ...)` may coordinate both endpoints; future
response-consuming protocol paths must participate in the same managed input ownership contract. Direct C streams, iostreams, native handles, and
third-party terminal libraries bypass Terminal's coordination.

Formatted free functions and Session formatting bridges perform formatting before taking the final output lock. This permits a custom formatter to
call global Terminal output or the same Session without deadlocking the same stream; the nested operation completes before the outer formatted record
is emitted.

Each Session call holds a private active-operation lease for its complete logical
lifetime. The lease keeps the bound options, managed input ownership, and
persistent state valid without holding the lifecycle mutex across backend work
or arbitrary formatter code.

`close()` prevents unrelated new operations from entering and waits for active
leases. Reentrant formatter calls safely share the existing lifetime. A
formatter-triggered `close()` on the same Session cannot wait for its own outer
lease, so it returns `ResourceBusy` and leaves the Session open. The outer
formatting operation can finish, and a later non-reentrant `close()` can retry
normally.

`Session` internally synchronizes the concurrent operation combinations listed above. Its lifetime must still be stable: do not move or destroy a
Session while another thread is using it. `OutputBuffer` and output-state scope objects are not internally synchronized and require external
synchronization for concurrent access. External `std::stop_token` cancellation requests follow the standard stop-token thread-safety contract.

## Read operations

| Operation | Purpose | Result |
| --- | --- | --- |
| `readEvent()` | Returns one normalized logical key, paste, or resize event. | `Types::Input::EventResult` |
| `readBytes()` | Copies raw bytes into caller storage. | `Types::Input::ByteResult` |
| `readText()` | Returns one available complete valid UTF-8 chunk. | `Types::Input::TextResult` |
| `readLine()` | Returns one valid UTF-8 line or a terminating partial line. | `Types::Input::LineResult` |

`Types::Input::ByteOptions::allowPartial = true` permits success after any positive byte count. With `allowPartial = false`, the operation attempts to
fill the supplied span until completion, a terminating outcome, or failure.

An empty byte span performs no transfer and returns a successful completed result with `bytesRead == 0`.

Text and line reads require `maxReturnedBytes > 0`. They preserve complete valid UTF-8 code points. If the configured limit cannot hold the next
complete code point, the operation returns `SizeLimitExceeded` rather than returning a partial encoding.

Direct input functions use temporary managed ownership. `Session` selects `Types::Input::DeliveryMode::Events` or `Stream` once at `open()`. Event
delivery accepts only `readEvent()`; Stream delivery accepts byte/text/line reads. A mode-incompatible read returns `Unsupported` without consuming
input.

Interactive terminals use one immediate structured-input engine for both delivery modes. Stream byte/text reads project text-producing logical events
back to stream bytes; managed `readLine()` consumes the same events through Terminal-owned line discipline. `Types::Input::LineOptions::echo` defaults
to true for interactive terminals and is ignored for redirected input.

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

`Types::Input::ByteResult` can preserve copied bytes together with a terminating outcome or a later backend failure. `Types::Input::LineResult` can
preserve an unterminated valid UTF-8 line with `EndOfStream`, `TimedOut`, `WouldBlock`, or `Cancelled`. `readText()` returns as soon as one complete
UTF-8 chunk is available, so a non-empty text payload normally uses `Completed`. `Types::Input::EventResult::event` is populated only for a completed
event. Always inspect the status, outcome, and payload together.

`wasTruncated` means the configured returned-byte limit stopped the text or line. Truncation is normally a successful result and unread data remains
pending where the backend can preserve it.

For line reads, `consumedLineEnding` reports `None`, `Lf`, `CrLf`, or `Cr`. `Types::Input::LineEndingMode` controls whether the returned string
strips, keeps, or normalizes a consumed ending. If the line body fits but a retained ending does not, the ending is consumed and reported while the
returned string omits the partial ending and sets `wasTruncated`.

Redirected long-line scanning retains progress between backend chunks, including the case where `\r\n` is split across reads.

Interactive line editing sits above the platform decoder and works in Unicode
grapheme boundaries. Backspace, Delete, and left/right movement therefore act on
user-perceived text elements rather than individual UTF-8 bytes. Terminal also
handles Home/End, Enter, bounded paste insertion, repeat events, resizing,
deadlines, cancellation, and optional echo.

Echo keeps separate spans for rendered text and the caret. Before destructive
editing, it rebuilds the anchor from the live caret in a backend-stable
coordinate; viewport scrolling cannot leave a stale viewport-relative row
behind. After a resize, the next redraw reflows that coordinate before clearing
or rewriting the suffix.

Printable ASCII appended at the end advances the tracked span without a cursor
query. Unicode and control text use measured cursor movement instead of a
guessed terminal-width policy. The editor builds grapheme indexes lazily and
reuses Session storage, so repeated suffix deletion does not repeatedly scan the
complete prefix.

## Deadlines, polling, and cancellation

Read options use `std::optional<std::chrono::milliseconds>`:

- `std::nullopt` waits indefinitely;
- `0ms` performs a non-blocking poll;
- a positive duration establishes one total deadline for the complete operation;
- a negative duration is `InvalidArgument`.

Partial input does not restart a positive deadline. `kNoWait` remains a readable `0ms` constant; there is no negative public wait-forever sentinel.

A `std::stop_token` is a requested-cancellation channel, not an operational failure. A pre-requested stop returns successful
`Types::Input::ReadOutcome::Cancelled` without consuming input. When a read could block and the selected endpoint cannot observe cancellation, a
stoppable token is rejected as `Unsupported` rather than silently ignored.

Current Win32 named-pipe input supports non-blocking reads, finite deadlines, and cooperative cancellation. Real Win32 console input now uses waitable
console handles plus one lazily-created cancellation event, so event, byte/text, and managed line reads support polling, finite total deadlines, and
requested cancellation without a permanent worker thread or sleep-based polling loop.

The console backend reads `INPUT_RECORD` values in a small fixed batch and keeps
unread records with the endpoint. It emits portable key and resize events
without allocating for each key. Repeat follow-up state, key-down tracking,
UTF-16 surrogate state, and the record batch all use inline storage.

Public Win32 cursor queries remain relative to `srWindow`, matching the visible
terminal viewport. Managed line echo privately uses stable screen-buffer
coordinates so scrolling does not invalidate its anchor. A caller may still
request an indefinite blocking read, and output can still block inside the
operating-system endpoint.

## Text writes

`writeText()` writes exactly the supplied text. `writeLine()` appends the selected line ending. `print()` and `println()` use compile-time-checked
`std::format_string` overloads, format before final stream serialization, then write one logical record.
All Text operations require complete valid UTF-8 independent of the selected endpoint. Validation occurs before style preparation, capability-changing
output work, or intentional emission; malformed or incomplete text returns `EncodingFailed`.

Plain text writes avoid formatting and style-assembly work when those features are not requested; they are not promised to be allocation-free on every
backend or failure path.

A failed platform write can have emitted a prefix. Terminal reports completion status but does not provide rollback or transactional output.

## Byte writes

`writeBytes()` returns `IO::Types::WriteResult`. Real Win32 console handles do not support arbitrary byte output; redirected endpoints can accept
bytes.

`bytesWritten` reports bytes accepted before the operation stopped. If a requested flush fails after all bytes were written, `bytesWritten` can equal
the full input size while `status` is a failure.

## Segmented writes

`writeSegments()` validates every Text and StyledText payload as UTF-8 before capability preparation or emission; Bytes payloads remain arbitrary
binary. An all-text record uses the text lane, while any record containing a Bytes segment uses the byte lane after text validation. The operation is
not transactional and does not coordinate with non-Terminal writers. See @ref terminal_segmented_writes.

## OutputBuffer

`OutputBuffer` owns plain-text storage for caller-controlled batching.

- construction is non-throwing and starts with `Types::Output::LineEnding::Native`;
- `setLineEnding()` changes future line policy through a checked status;
- `reserve()` changes capacity without changing text and translates allocation/length failure;
- `appendText()` appends valid UTF-8 by contract through a checked mutation;
- `appendLine()` appends text and the configured line ending atomically;
- `print()` and `println()` format into retained storage and roll back to the previous size on failure;
- `writeTo()` writes without clearing;
- `flushTo()` writes and clears only when the write succeeds.

Despite its name, `flushTo()` does not automatically request an operating-system flush. A backend flush occurs only when
`Types::Output::TextOptions::flushMode` is not `None`.

A failed `flushTo()` preserves the text for retry. Closing or initialization concepts do not apply because `OutputBuffer` is only caller-owned memory.

`text()` returns a non-owning view into the internal string. Reserve, append, clear, formatting, move, assignment, or destruction can invalidate it.
Do not access the same buffer concurrently without external synchronization.

`OutputBuffer` enforces its text-only invariant when text is appended. `appendText()` and `appendLine()` reject malformed or incomplete UTF-8 before
mutation. Formatted appends validate only the newly produced suffix and roll back that complete append on encoding failure, so repeated appends do not
rescan the already trusted prefix. `text()` therefore always exposes valid UTF-8.

## Flush behavior

Terminal retains no unwritten output in a library-owned stream buffer. `FlushMode::None` requests no flush. On Win32, `Data` and
`DataAndMetadataBestEffort` request an operating-system flush only for a standard handle redirected to a regular disk file. Console and pipe flushes
are successful no-ops.

Terminal flush does not flush `std::cout`, `std::cerr`, C `FILE*` buffers, or storage owned by another library. Invalid flush modes are rejected
before normal emission begins.

## Managed ownership and restoration failures

A direct read captures required native terminal state, performs the read, restores the exact snapshot, and releases ownership before returning. If the
read succeeds but restoration fails, the restoration failure becomes the returned status. If both fail, the read failure remains primary and
restoration failure is appended to that result's diagnostic text on a best-effort basis.

A persistent `Session::close()` keeps the object retryable. It restores
Session-owned output state in reverse activation order, then restores input. If
either step fails, the Session remains open and retains stdin ownership so the
caller can try again.

The non-throwing destructor has no caller to report failure to. It attempts each
pending output restoration once in reverse order, attempts input restoration,
and then releases process-wide ownership bookkeeping. Failed destructor cleanup
is not retried later out of order.

## Failure and exception model

Expected option, endpoint, ownership, capability, Unicode, cancellation, formatting, allocation, and backend failures use IO statuses/results. Checked
direct Terminal operations, Session operations, and `OutputBuffer` allocating mutations/formatting are `noexcept`; once control enters a checked
Terminal operation, implementation-owned failure is represented through its status/result. Caller argument construction still occurs before that
boundary.

Free and Session formatted output contain formatter, allocation, length, and unexpected formatting-stage exceptions before the outer write begins.
Checked direct output paths contain Terminal-owned assembly allocation where it occurs, and diagnostic enrichment is best effort so constructing an
error message never replaces the primary code-only failure. Caller-owned argument construction that happens before Terminal receives control remains
outside the checked boundary.

Output-state scope factories are `noexcept` and store setup failure in `status()`. Failure before the state-changing control sequence is emitted
produces an inactive scope. If the sequence is emitted and its requested flush then fails, the scope remains active, owns the inverse transition, and
retries only the pending flush after a successful inverse emission.

## Related pages

- @ref terminal_unicode_io
- @ref terminal_capabilities_and_redirection
- @ref terminal_segmented_writes
- @ref terminal_troubleshooting
