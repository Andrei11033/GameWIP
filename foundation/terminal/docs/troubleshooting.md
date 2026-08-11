@page terminal_troubleshooting Troubleshooting

## A control returns `Unsupported`

The selected endpoint does not currently advertise the required terminal feature. Redirected, detached, and `Other` endpoints commonly cannot run cursor, clear, alternate-screen, title, or bell controls.

Query `getOutputCapabilities()` for planning, but still handle the operation status because capabilities can change.

## Styled output is plain

`StyleMode::Auto` falls back to plain text when preparation or the complete requested style is unavailable. Use `StyleMode::Required` only when failure is preferable to plain output.

## A style sequence appears without its reset

Terminal assembles a logical styled record, but platform writes are not transactional. A failing endpoint can emit a prefix. Recover with `resetStyle()` when the endpoint still supports terminal controls.

## Text reads fail with `EncodingFailed`

Text and line reads require complete valid UTF-8. Use `readBytes()` for arbitrary redirected input. Incomplete UTF-8 at end-of-stream also fails.

## A small read limit returns `SizeLimitExceeded`

The next UTF-8 code point does not fit in `maxReturnedBytes`. Increase the limit; Terminal will not split the encoding.

## A timed, polled, or cancelled read returns no text

Inspect both `status` and `outcome`. `TimedOut`, `WouldBlock`, and `Cancelled` are normal domain outcomes with a successful status.

Read timeouts are optional: `std::nullopt` waits indefinitely, `0ms` polls, positive durations establish one total deadline, and negative durations are `InvalidArgument`. Real Win32 console input and named-pipe input support polling, finite deadlines, and requested cancellation. Regular redirected files still do not promise bounded waiting.

## Interactive `readLine()` returns `Unsupported` before reading

Managed echo requires a terminal output that supports cursor positioning, cursor-position queries, and line clearing. If input is interactive but the bound output cannot render managed echo, set `LineReadOptions::echo = false` and render the application UI yourself.

## A special key does not appear in `readText()`

Stream text/byte reads project only text-producing/control events into stream data. Navigation, function, modifier, resize, and other logical events are intentionally not serialized into arbitrary escape sequences. Use `InputDeliveryMode::Events` when the application needs those keys.

## `flushTo()` did not force an OS flush

`OutputBuffer::flushTo()` means write and clear on success. Set `TextWriteOptions::flushMode` to request a backend flush.

## Flush succeeds but a pipe consumer sees no change

Terminal retains no unwritten stream buffer. On Win32, console and pipe flushes are successful no-ops; only a standard handle redirected to a regular disk file receives an operating-system flush. Flush any C/C++ stream buffer through the API that owns it.

## `writeBytes()` failed after reporting full progress

A requested flush can fail after all bytes were accepted. Inspect both `status` and `bytesWritten`.

## `Session::open()` returns `AlreadyOpen` or `ResourceBusy`

`AlreadyOpen` means that same `Session` object is already open. `ResourceBusy` means another persistent session or direct managed input operation owns stdin.

Close the existing owner explicitly before opening another one. Do not reintroduce availability-check-then-read logic; ownership is intentionally acquired by the operation itself.

## `Session::close()` failed and `isOpen()` is still true

Exact native input restoration failed. Explicit close intentionally keeps the session open and retains stdin ownership so cleanup can be retried. Fix or report the backend problem, then call `close()` again.

Destruction is different: the destructor makes one best-effort restoration attempt and releases process-wide ownership because the destroyed object cannot be retried.

## A stop token returns `Unsupported`

The token is stoppable and the requested read may block, but the endpoint cannot currently observe cancellation safely. Use an endpoint that advertises `supportsCancellation`, use a `0ms` poll loop owned by the application where appropriate, or omit the stoppable token when indefinite blocking is acceptable.

A token that is already stopped returns `ReadOutcome::Cancelled` before input is consumed.

## An output-state scope is inactive immediately after creation

Cursor-hidden or alternate-screen setup failed. Inspect `status()`. Those scope factories are `noexcept` and return an inactive object rather than throwing.

## Output-state scope move assignment did not consume the source

The destination already owned active output state and failed to restore or leave it. The destination remains active and the source retains its responsibility so state ownership is not silently lost.

## Output interleaves with `std::cout` or `printf`

Terminal serializes only calls made through Terminal. Use one output abstraction for records that must not interleave, and flush foreign buffers through their owning APIs before switching.

## Redirected text accepted invalid UTF-8

Redirected output is byte-oriented and may preserve supplied bytes without validation. Real-console conversion is stricter. Validate independently when endpoint-invariant UTF-8 rejection is required.

## A capability result no longer matches behavior

The process replaced or redirected a standard handle, changed native mode externally, or otherwise modified endpoint state. Query again and treat the operation status as authoritative.
