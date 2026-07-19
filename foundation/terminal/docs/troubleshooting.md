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

## A timed read returns no text

Inspect both `status` and `outcome`. `TimedOut` and `WouldBlock` are normal outcomes. Real Windows console input currently does not support finite or non-blocking reads and returns `Unsupported` instead.

## `flushTo()` did not force an OS flush

`OutputBuffer::flushTo()` means write and clear on success. Set `TextWriteOptions::flushMode` to request a backend flush.

## Flush succeeds but a pipe consumer sees no change

Terminal retains no unwritten stream buffer. On Win32, console and pipe flushes are successful no-ops; only a standard handle redirected to a regular disk file receives an operating-system flush. Flush any C/C++ stream buffer through the API that owns it.

## `writeBytes()` failed after reporting full progress

A requested flush can fail after all bytes were accepted. Inspect both `status` and `bytesWritten`.

## A scope is inactive immediately after creation

Setup failed. Inspect `status()`. Scope factories are `noexcept` and return an inactive object rather than throwing.

## Move assignment did not consume the source scope

The destination already owned active state and failed to restore or leave it. The destination remains active and the source retains its responsibility so state ownership is not silently lost.

## Output interleaves with `std::cout` or `printf`

Terminal serializes only calls made through Terminal. Use one output abstraction for records that must not interleave, and flush foreign buffers through their owning APIs before switching.

## Redirected text accepted invalid UTF-8

Redirected output is byte-oriented and may preserve supplied bytes without validation. Real-console conversion is stricter. Validate independently when endpoint-invariant UTF-8 rejection is required.

## A capability result no longer matches behavior

The process replaced or redirected a standard handle, changed native mode externally, or otherwise modified endpoint state. Query again and treat the operation status as authoritative.
