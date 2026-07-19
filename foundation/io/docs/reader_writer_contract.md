@page io_reader_writer_contract Reader and writer contract

`Reader` and `Writer` are extension interfaces for resource-owning libraries. This page owns the transfer, capability, lifetime, exception, and concurrency rules that implementations and generic callers must preserve.

## Interface model

Both interfaces are movable and non-copyable. Their destructors are virtual and non-throwing.

The only required virtual operation is `Reader::read()` or `Writer::write()`. Base implementations provide neutral defaults for optional behavior.

### Reader defaults

| Operation | Base behavior |
| --- | --- |
| `isOpen()` | Returns `true`. |
| `canSeek()` | Returns `false`. |
| `close()` | Successful no-op. |
| `position()` | Returns `NotSeekable`. |
| `size()` | Returns `NotSeekable`. |
| `seek()` | Returns `NotSeekable`. |

### Writer defaults

| Operation | Base behavior |
| --- | --- |
| `isOpen()` | Returns `true`. |
| `canSeek()` | Returns `false`. |
| `flush()` | Rejects unknown modes, otherwise succeeds without requiring physical I/O. |
| `close()` | Successful no-op. |
| `position()` | Returns `NotSeekable`. |
| `seek()` | Returns `NotSeekable`. |

These defaults are suitable for stateless streaming adapters. Resource-owning implementations should override state and lifecycle operations that have meaningful behavior.

## Read contract

`read(destination)` may copy from zero through `destination.size()` bytes.

An implementation must:

- Never report `bytesRead > destination.size()`.
- Report valid copied bytes even when the same call also reports failure.
- Set `endOfStream` according to whether no additional input remains after the call.
- Return a successful zero-byte result only for an empty request or when end-of-stream is reported.
- Avoid retaining the destination span after the call returns unless a separate public contract explicitly transfers or extends its lifetime.

The final non-empty read may set `endOfStream = true`. Callers must inspect both fields.

## Write contract

`write(bytes)` may accept from zero through `bytes.size()` bytes.

An implementation must:

- Never report `bytesWritten > bytes.size()`.
- Report valid accepted bytes even when the same call also reports failure.
- Return success with zero bytes for an empty request.
- Avoid returning success with zero bytes while non-empty input remains, because generic retry loops cannot make progress.
- Avoid retaining the input span after the call returns unless a separate public contract explicitly transfers or extends its lifetime.

A successful short write means progress was made and the remaining suffix may be retried. A writer may instead accept a prefix and return a failure in the same call; generic helpers preserve that accepted count and stop.

## Optional capabilities

`canSeek()` is an advisory query. Callers must still inspect the result of `position()`, `size()`, and `seek()` because runtime state may change and size support is not implied by seek support.

Whole-stream read helpers do not rely on `canSeek()`. They query `size()` and `position()` directly:

- If `size()` returns `NotSeekable` or `Unsupported`, the reader is treated as unknown-size.
- If size succeeds but position returns either of those codes, the known size is discarded and the unknown-size path is used.
- Other capability-query failures are propagated before payload reads.
- A reported position greater than size is an invalid contract and returns `InvalidArgument`.

A backend should use `NotSeekable` for stream-position capabilities and `Unsupported` for other intentionally absent capabilities.

## Whole-stream reads

`readAllBytes()` and `readAllText()` start at the reader's current position.

### Known-size path

When both size and position succeed, the helpers calculate the remaining byte count and:

- Reject counts above `maxBytes` or the result container's representational limit before reading.
- Allocate the final output to the known remaining size.
- Read until that exact count is produced or a failure occurs.
- Preserve bytes produced by a final failing read.
- Return `PartialRead` if end-of-stream occurs before the promised count.
- Return `ReadFailed` for an impossible byte count or zero progress before completion.

An exact known byte count is authoritative; the final read does not need to set `endOfStream` once the promised count has been produced.

### Unknown-size path

When size or position is unavailable, the helpers read through either an internally allocated buffer or caller-owned scratch storage.

They:

- Require a nonzero internal buffer size or non-empty scratch span.
- Append valid bytes before processing a failure from the same read.
- Stop successfully when `endOfStream` is reported.
- Return `ReadFailed` for impossible counts or successful zero progress without end-of-stream.
- Never retain the scratch span after the call returns.

The scratch buffer is ignored when the reader qualifies for the known-size path.

### Hard byte limits

`maxBytes` is the maximum accepted output size, not a truncation request.

For an unknown-size reader, reaching the limit requires a one-byte probe:

- End-of-stream makes the operation successful.
- One additional byte returns `SizeLimitExceeded` and consumes that byte without storing it.
- A zero-byte backend failure is propagated.
- A zero-byte success without end-of-stream returns `ReadFailed`.

A zero limit accepts an empty stream and probes unknown-size input immediately.

## Whole-stream writes

`writeAllBytes()` repeatedly passes the unwritten suffix to the writer until all input is accepted or a call fails.

It:

- Succeeds for empty input without calling `write()`.
- Retries successful short writes.
- Includes bytes accepted by a final failing call in `bytesWritten`.
- Returns `WriteFailed` if a writer reports an impossible count or successful zero progress.
- Does not flush or close the writer.

`writeAllText()` applies the same contract to the exact bytes in a string view, including embedded NUL bytes. It does not validate UTF-8.

## MemoryReader

`MemoryReader` is a non-owning view over contiguous bytes.

- The source must remain alive and at a stable address while the reader is used.
- Direct temporary `std::string` and byte-vector construction is deleted.
- Caller-created dangling spans and string views remain the caller's responsibility.
- Reads use overlap-safe copying, so the destination may overlap the source.
- Seeking is bounded to the inclusive range from position zero through end-of-stream.
- An invalid origin returns `InvalidArgument`; an out-of-range target returns `SeekFailed`.
- `close()` is idempotent and never modifies the source.
- After close, read, size, position, and seek operations return `NotOpen`; `canSeek()` returns false.

## MemoryWriter

`MemoryWriter` owns append-only `std::vector<std::byte>` storage.

- Writes from a valid subspan of the writer's current `bytes()` view are supported, including when appending reallocates storage.
- `flush()` validates its mode and otherwise performs no physical work.
- `close()` is idempotent and preserves collected output.
- After close, `write()`, `flush()`, and `position()` return `NotOpen`.
- `bytes()`, `text()`, `takeBytes()`, `size()`, `capacity()`, `empty()`, `reserve()`, and `clear()` remain available after close.
- `clear()` preserves capacity.
- `takeBytes()` transfers ownership, leaves the writer empty, preserves open/closed state, and may discard reserved capacity.

`bytes()` returns a non-owning view into writer-owned storage. Do not retain it across operations that can modify storage or ownership. A write or reserve may reallocate; clear, take, move assignment, destruction, and ownership transfer invalidate the prior logical view.

`text()` returns an independent copy and preserves embedded NUL bytes. It performs no UTF-8 validation.

## Exceptions

Expected I/O failures use statuses, but the virtual transfer and capability functions are not globally `noexcept`.

- Exceptions thrown by a custom `Reader` or `Writer` may propagate through generic helpers.
- The helpers convert their own documented allocation failures to `OutOfMemory`; they do not catch arbitrary backend exceptions.
- `MemoryWriter` construction, `reserve()`, and `text()` may throw standard allocation or length exceptions.
- Destructors must not throw. Call `close()` explicitly when close failure must be observed.

## Thread-safety and blocking

Different reader or writer objects may be used concurrently. The same object is not thread-safe unless its concrete implementation explicitly says otherwise.

IO itself performs no operating-system calls. Blocking behavior belongs to the concrete backend. `MemoryReader` and `MemoryWriter` are not internally synchronized and do not block on operating-system I/O.

## Non-goals

IO does not provide file opening, terminal access, network I/O, asynchronous I/O, memory mapping, file watching, parsing, or format decoding. Resource-owning libraries implement those concerns behind the shared contracts.

## Related pages

- @ref io_public_api
- @ref io_error_model
- @ref io_runtime_performance
- @ref io_examples
